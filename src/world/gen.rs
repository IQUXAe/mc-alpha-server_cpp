//! On-demand chunk generation/population on [`World`].
//! Split out of `world.rs`; behavior unchanged.

use crate::chunk::Chunk;
use crate::world::World;
use crate::world::tiles::TileData;

impl World {
    /// Generate and populate one chunk on demand (mirrors the C++ provider
    /// path): raw terrain for the 2x2 canvas, decoration of the requested
    /// chunk with the world as fallback accessor, then insert/write-back
    /// with fresh height and sky maps. Populated chunks are left alone;
    /// each chunk is decorated once (present-but-raw chunks decorate on
    /// request).
    /// Decorate-time meta notify/mark is skipped (the C++
    /// fallback does mark+notify; nothing reacts pre-tick here).
    pub fn ensure_chunk(&mut self, cx: i32, cz: i32) {
        if self.chunks.get(&(cx, cz)).map(|c| c.is_terrain_populated).unwrap_or(false) {
            return;
        }
        use crate::biome::MobSpawnerBase;
        use crate::generator::{
            RustChunkData, RustChunkDataBatch, rust_chunk_provider_generate_chunk,
            rust_chunk_provider_populate_batch,
        };
        // 1. Stage the 2x2 canvas (existing chunks copied, missing generated).
        // Populate-time climate is always recomputed fresh like Java
        // `populate` (biome at the far corner, snow temperatures at +8):
        // the center chunk often already sits staged as a raw neighbor,
        // so falling back to defaults here decorated whole regions as
        // snowy plains (no trees, no cacti, snow in deserts).
        let (center_biome, center_temps) = {
            let gen = self.generator();
            let mut center_temps = [0.0f64; 256];
            crate::generator::chunk_temperatures(gen, cx * 16 + 8, cz * 16 + 8, &mut center_temps);
            let center_biome = crate::generator::point_biome(gen, cx * 16 + 16, cz * 16 + 16);
            (center_biome, center_temps)
        };
        let mut stage_blocks = [[[0u8; 32768]; 2]; 2];
        let mut stage_meta = [[[0u8; 32768]; 2]; 2];
        for dx in 0..2usize {
            for dz in 0..2usize {
                let (nx, nz) = (cx + dx as i32, cz + dz as i32);
                if let Some(c) = self.chunks.get(&(nx, nz)) {
                    c.fill_arrays(&mut stage_blocks[dx][dz], &mut stage_meta[dx][dz]);
                } else {
                    let gen = self.generator();
                    let mut blocks = [0u8; 32768];
                    let mut biomes = [MobSpawnerBase::DEFAULT; 256];
                    let mut temps = [0.0f64; 256];
                    let mut humids = [0.0f64; 256];
                    rust_chunk_provider_generate_chunk(
                        gen,
                        nx,
                        nz,
                        &mut blocks,
                        &mut biomes,
                        &mut temps,
                        &mut humids,
                    );
                    stage_blocks[dx][dz] = blocks;
                }
            }
        }
        // 2. Decorate the requested chunk over the canvas.
        let batch = RustChunkDataBatch {
            chunks: [
                RustChunkData {
                    blocks: stage_blocks[0][0].as_mut_ptr(),
                    metadata: stage_meta[0][0].as_mut_ptr(),
                    x: cx,
                    z: cz,
                },
                RustChunkData {
                    blocks: stage_blocks[0][1].as_mut_ptr(),
                    metadata: stage_meta[0][1].as_mut_ptr(),
                    x: cx,
                    z: cz + 1,
                },
                RustChunkData {
                    blocks: stage_blocks[1][0].as_mut_ptr(),
                    metadata: stage_meta[1][0].as_mut_ptr(),
                    x: cx + 1,
                    z: cz,
                },
                RustChunkData {
                    blocks: stage_blocks[1][1].as_mut_ptr(),
                    metadata: stage_meta[1][1].as_mut_ptr(),
                    x: cx + 1,
                    z: cz + 1,
                },
            ],
        };
        let dungeon_loot = {
            // Decoration sets bypass skylight regen (mirrors C++
            // isPopulating); the write-back below regenerates instead.
            self.populating = true;
            // Field-split borrows: the generator tables and the chunk map
            // are disjoint, so the live map can back the canvas fallback
            // explicitly (no thread-local bridge).
            if self.generator.is_none() {
                let seed = self.seed;
                self.generator =
                    Some(crate::generator::RustChunkProviderGenerate::new(seed));
            }
            let gen = self.generator.as_mut().unwrap();
            let mut fallback = crate::decorators::WorldAccess {
                chunks: &mut self.chunks,
                populating: self.populating,
            };
            let loot = rust_chunk_provider_populate_batch(
                gen,
                &batch,
                &mut fallback,
                cx,
                cz,
                center_biome.biome_type as i32,
                &center_temps,
            );
            self.populating = false;
            loot
        };
        // 3. Write back: insert missing, refresh present (tree spillover);
        // only the requested chunk is flagged (canvas neighbors decorate
        // on their own request, exactly once each).
        for dx in 0..2usize {
            for dz in 0..2usize {
                let (nx, nz) = (cx + dx as i32, cz + dz as i32);
                let requested = dx == 0 && dz == 0;
                match self.chunks.get_mut(&(nx, nz)) {
                    Some(c) => {
                        c.load_arrays(&stage_blocks[dx][dz], &stage_meta[dx][dz]);
                        c.generate_skylight_map();
                        if requested {
                            c.is_terrain_populated = true;
                        }
                    }
                    None => {
                        let mut c = Chunk::new(nx, nz);
                        c.load_arrays(&stage_blocks[dx][dz], &stage_meta[dx][dz]);
                        c.generate_skylight_map();
                        c.is_terrain_populated = requested;
                        self.chunks.insert((nx, nz), c);
                    }
                }
            }
        }
        // 4. Dungeon-chest loot: the canvas holds no tiles, so materialize
        // chest rows for placed chests and deal the buffered stacks.
        for (lx, ly, lz, slot, item, count) in dungeon_loot {
            if self.get_block_id(lx, ly, lz) != 54 {
                continue;
            }
            let tile = self.tiles.entry((lx, ly, lz)).or_insert_with(|| {
                TileData::Chest(crate::tile_entity_chest::chest_create())
            });
            if let TileData::Chest(ch) = tile {
                if slot >= 0 && (slot as usize) < ch.slots.len() {
                    ch.slots[slot as usize] = crate::inventory::FfiItemStack {
                        stack_size: count,
                        animations_to_go: 0,
                        item_id: item,
                        item_damage: 0,
                    };
                }
            }
        }
    }

    /// Lazily built terrain generator (eleven octave tables; tests that
    /// never generate pay nothing).
    fn generator(&mut self) -> &mut crate::generator::RustChunkProviderGenerate {
        if self.generator.is_none() {
            let seed = self.seed;
            self.generator =
                Some(crate::generator::RustChunkProviderGenerate::new(seed));
        }
        self.generator.as_mut().unwrap()
    }

    /// Generate a square of chunks around a center (join/respawn path for
    /// the network slice).
    pub fn ensure_area(&mut self, cx: i32, cz: i32, radius: i32) {
        for dx in -radius..=radius {
            for dz in -radius..=radius {
                self.ensure_chunk(cx + dx, cz + dz);
            }
        }
    }
}
