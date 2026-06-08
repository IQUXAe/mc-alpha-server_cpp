use crate::random::JavaRandom;
use crate::noise::{NoiseGeneratorOctaves, NoiseGeneratorOctaves2};
use crate::biome::{MobSpawnerBase, get_biome_from_lookup};
use crate::caves::MapGenCaves;
use crate::decorators::{WorldAccessor, alpha_decorate_chunk};
use libc::{c_char, c_int, size_t};
use std::cell::Cell;

thread_local! {
    static CURRENT_DECORATOR_WORLD: Cell<*mut std::ffi::c_void> = Cell::new(std::ptr::null_mut());
}

#[repr(C)]
pub struct RustChunkData {
    pub blocks: *mut u8,
    pub metadata: *mut u8,
    pub x: i32,
    pub z: i32,
}

#[repr(C)]
pub struct RustChunkDataBatch {
    pub chunks: [RustChunkData; 4],
}

struct DecoratorWorldState {
    blocks: [*mut u8; 4],
    metadata: [*mut u8; 4],
    chunk_x: i32,
    chunk_z: i32,
    fallback_accessor: WorldAccessor,
}

extern "C" fn local_get_block_id(x: i32, y: i32, z: i32) -> u8 {
    unsafe {
        let state_ptr = CURRENT_DECORATOR_WORLD.with(|cell| cell.get()) as *mut DecoratorWorldState;
        if state_ptr.is_null() {
            return 0;
        }
        let state = &*state_ptr;

        let rel_x = x - state.chunk_x * 16;
        let rel_z = z - state.chunk_z * 16;
        if rel_x < 0 || rel_x >= 32 || rel_z < 0 || rel_z >= 32 {
            return (state.fallback_accessor.get_block_id)(x, y, z);
        }
        let cx = (rel_x >> 4) as usize;
        let cz = (rel_z >> 4) as usize;
        let lx = (rel_x & 15) as usize;
        let lz = (rel_z & 15) as usize;
        let idx = (lx << 11) | (lz << 7) | (y as usize);
        let blocks_ptr = state.blocks[cx * 2 + cz];
        *blocks_ptr.add(idx)
    }
}

extern "C" fn local_set_block_id(x: i32, y: i32, z: i32, id: u8) {
    unsafe {
        let state_ptr = CURRENT_DECORATOR_WORLD.with(|cell| cell.get()) as *mut DecoratorWorldState;
        if state_ptr.is_null() {
            return;
        }
        let state = &mut *state_ptr;

        // Chest (54) or Mob Spawner (52) need C++ TileEntity creation
        if id == 54 || id == 52 {
            (state.fallback_accessor.set_block_id)(x, y, z, id);
            return;
        }

        let rel_x = x - state.chunk_x * 16;
        let rel_z = z - state.chunk_z * 16;
        if rel_x < 0 || rel_x >= 32 || rel_z < 0 || rel_z >= 32 {
            (state.fallback_accessor.set_block_id)(x, y, z, id);
            return;
        }
        let cx = (rel_x >> 4) as usize;
        let cz = (rel_z >> 4) as usize;
        let lx = (rel_x & 15) as usize;
        let lz = (rel_z & 15) as usize;
        let idx = (lx << 11) | (lz << 7) | (y as usize);
        let blocks_ptr = state.blocks[cx * 2 + cz];
        *blocks_ptr.add(idx) = id;
    }
}

extern "C" fn local_get_block_meta(x: i32, y: i32, z: i32) -> u8 {
    unsafe {
        let state_ptr = CURRENT_DECORATOR_WORLD.with(|cell| cell.get()) as *mut DecoratorWorldState;
        if state_ptr.is_null() {
            return 0;
        }
        let state = &*state_ptr;

        let rel_x = x - state.chunk_x * 16;
        let rel_z = z - state.chunk_z * 16;
        if rel_x < 0 || rel_x >= 32 || rel_z < 0 || rel_z >= 32 {
            return (state.fallback_accessor.get_block_meta)(x, y, z);
        }
        let cx = (rel_x >> 4) as usize;
        let cz = (rel_z >> 4) as usize;
        let lx = (rel_x & 15) as usize;
        let lz = (rel_z & 15) as usize;
        let idx = (lx << 11) | (lz << 7) | (y as usize);
        let metadata_ptr = state.metadata[cx * 2 + cz];

        let byte = *metadata_ptr.add(idx >> 1);
        if (idx & 1) != 0 {
            (byte >> 4) & 0xF
        } else {
            byte & 0xF
        }
    }
}

extern "C" fn local_set_block_meta(x: i32, y: i32, z: i32, meta: u8) {
    unsafe {
        let state_ptr = CURRENT_DECORATOR_WORLD.with(|cell| cell.get()) as *mut DecoratorWorldState;
        if state_ptr.is_null() {
            return;
        }
        let state = &mut *state_ptr;

        let rel_x = x - state.chunk_x * 16;
        let rel_z = z - state.chunk_z * 16;
        if rel_x < 0 || rel_x >= 32 || rel_z < 0 || rel_z >= 32 {
            (state.fallback_accessor.set_block_meta)(x, y, z, meta);
            return;
        }
        let cx = (rel_x >> 4) as usize;
        let cz = (rel_z >> 4) as usize;
        let lx = (rel_x & 15) as usize;
        let lz = (rel_z & 15) as usize;
        let idx = (lx << 11) | (lz << 7) | (y as usize);
        let metadata_ptr = state.metadata[cx * 2 + cz];

        let byte_idx = idx >> 1;
        let val = metadata_ptr.add(byte_idx);
        if (idx & 1) != 0 {
            *val = (*val & 0x0F) | ((meta & 0xF) << 4);
        } else {
            *val = (*val & 0xF0) | (meta & 0xF);
        }
    }
}

extern "C" fn local_allows_attachment(x: i32, y: i32, z: i32) -> bool {
    unsafe {
        let state_ptr = CURRENT_DECORATOR_WORLD.with(|cell| cell.get()) as *mut DecoratorWorldState;
        if state_ptr.is_null() {
            return false;
        }
        ((*state_ptr).fallback_accessor.allows_attachment)(x, y, z)
    }
}

extern "C" fn local_is_block_solid(x: i32, y: i32, z: i32) -> bool {
    unsafe {
        let state_ptr = CURRENT_DECORATOR_WORLD.with(|cell| cell.get()) as *mut DecoratorWorldState;
        if state_ptr.is_null() {
            return false;
        }
        ((*state_ptr).fallback_accessor.is_block_solid)(x, y, z)
    }
}

extern "C" fn local_get_height_value(x: i32, z: i32) -> i32 {
    unsafe {
        let state_ptr = CURRENT_DECORATOR_WORLD.with(|cell| cell.get()) as *mut DecoratorWorldState;
        if state_ptr.is_null() {
            return 0;
        }
        let state = &*state_ptr;

        let rel_x = x - state.chunk_x * 16;
        let rel_z = z - state.chunk_z * 16;
        if rel_x < 0 || rel_x >= 32 || rel_z < 0 || rel_z >= 32 {
            return (state.fallback_accessor.get_height_value)(x, z);
        }
        let cx = (rel_x >> 4) as usize;
        let cz = (rel_z >> 4) as usize;
        let lx = (rel_x & 15) as usize;
        let lz = (rel_z & 15) as usize;

        let blocks_ptr = state.blocks[cx * 2 + cz];
        for y in (0..128).rev() {
            let idx = (lx << 11) | (lz << 7) | y;
            if *blocks_ptr.add(idx) != 0 {
                return (y + 1) as i32;
            }
        }
        0
    }
}

pub struct RustChunkProviderGenerate {
    pub world_seed: i64,
    pub field_705_k: Box<NoiseGeneratorOctaves>,
    pub field_704_l: Box<NoiseGeneratorOctaves>,
    pub field_703_m: Box<NoiseGeneratorOctaves>,
    pub field_702_n: Box<NoiseGeneratorOctaves>,
    pub field_701_o: Box<NoiseGeneratorOctaves>,
    pub field_715_a: Box<NoiseGeneratorOctaves>,
    pub field_714_b: Box<NoiseGeneratorOctaves>,
    pub field_713_c: Box<NoiseGeneratorOctaves>,
    
    // Biome generators (WorldChunkManager)
    pub temp_noise_gen: Box<NoiseGeneratorOctaves2>,
    pub humid_noise_gen: Box<NoiseGeneratorOctaves2>,
    pub noise_gen3: Box<NoiseGeneratorOctaves2>,
}

impl RustChunkProviderGenerate {
    pub fn new(seed: i64) -> Self {
        let mut rand = JavaRandom::new(seed);
        let field_705_k = Box::new(NoiseGeneratorOctaves::new(&mut rand, 16));
        let field_704_l = Box::new(NoiseGeneratorOctaves::new(&mut rand, 16));
        let field_703_m = Box::new(NoiseGeneratorOctaves::new(&mut rand, 8));
        let field_702_n = Box::new(NoiseGeneratorOctaves::new(&mut rand, 4));
        let field_701_o = Box::new(NoiseGeneratorOctaves::new(&mut rand, 4));
        let field_715_a = Box::new(NoiseGeneratorOctaves::new(&mut rand, 10));
        let field_714_b = Box::new(NoiseGeneratorOctaves::new(&mut rand, 16));
        let field_713_c = Box::new(NoiseGeneratorOctaves::new(&mut rand, 8));

        let temp_noise_gen = Box::new(NoiseGeneratorOctaves2::new(&mut JavaRandom::new(seed.wrapping_mul(9871)), 4));
        let humid_noise_gen = Box::new(NoiseGeneratorOctaves2::new(&mut JavaRandom::new(seed.wrapping_mul(39811)), 4));
        let noise_gen3 = Box::new(NoiseGeneratorOctaves2::new(&mut JavaRandom::new(seed.wrapping_mul(543321)), 2));

        Self {
            world_seed: seed,
            field_705_k,
            field_704_l,
            field_703_m,
            field_702_n,
            field_701_o,
            field_715_a,
            field_714_b,
            field_713_c,
            temp_noise_gen,
            humid_noise_gen,
            noise_gen3,
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn rust_chunk_provider_generate_create(seed: i64) -> *mut RustChunkProviderGenerate {
    Box::into_raw(Box::new(RustChunkProviderGenerate::new(seed)))
}

#[no_mangle]
pub unsafe extern "C" fn rust_chunk_provider_generate_destroy(ptr: *mut RustChunkProviderGenerate) {
    if !ptr.is_null() {
        let _ = Box::from_raw(ptr);
    }
}

#[no_mangle]
pub unsafe extern "C" fn rust_chunk_provider_generate_chunk(
    ptr: *mut RustChunkProviderGenerate,
    chunk_x: i32,
    chunk_z: i32,
    out_blocks: *mut u8,
    out_biomes: *mut MobSpawnerBase,
    out_temps: *mut f64,
    out_humids: *mut f64,
) {
    let gen = &mut *ptr;
    let blocks = std::slice::from_raw_parts_mut(out_blocks, 32768);
    let biomes = std::slice::from_raw_parts_mut(out_biomes, 256);
    let temperatures = std::slice::from_raw_parts_mut(out_temps, 256);
    let humidities = std::slice::from_raw_parts_mut(out_humids, 256);

    let mut rand = JavaRandom::new(
        (chunk_x as i64)
            .wrapping_mul(341873128712)
            .wrapping_add((chunk_z as i64).wrapping_mul(132897987541)),
    );

    // 1. Load block generator data (biomes & temperatures)
    gen.temp_noise_gen.func_4101_a(temperatures, (chunk_x * 16) as f64, (chunk_z * 16) as f64, 16, 16, 0.025, 0.025, 0.25);
    gen.humid_noise_gen.func_4101_a(humidities, (chunk_x * 16) as f64, (chunk_z * 16) as f64, 16, 16, 0.05, 0.05, 1.0 / 3.0);

    let mut field_4257_c = vec![0.0; 256];
    gen.noise_gen3.func_4101_a(&mut field_4257_c, (chunk_x * 16) as f64, (chunk_z * 16) as f64, 16, 16, 0.25, 0.25, 0.5882352941176471);

    for idx in 0..256 {
        let noise = field_4257_c[idx] * 1.1 + 0.5;

        let d1_t = 0.01;
        let d2_t = 1.0 - d1_t;
        let mut temp = (temperatures[idx] * 0.15 + 0.7) * d2_t + noise * d1_t;

        let d1_h = 0.002;
        let d2_h = 1.0 - d1_h;
        let mut humid = (humidities[idx] * 0.15 + 0.5) * d2_h + noise * d1_h;

        temp = 1.0 - (1.0 - temp) * (1.0 - temp);
        if temp < 0.0 { temp = 0.0; }
        if humid < 0.0 { humid = 0.0; }
        if temp > 1.0 { temp = 1.0; }
        if humid > 1.0 { humid = 1.0; }

        temperatures[idx] = temp;
        humidities[idx] = humid;
        biomes[idx] = get_biome_from_lookup(temp, humid);
    }

    // 2. Generate terrain
    let var6 = 4;
    let var7 = 64;
    let var8 = var6 + 1;
    let var9 = 17;
    let var10 = var6 + 1;

    let mut density_field = vec![0.0; (var8 * var9 * var10) as usize];
    crate::density::alpha_density_generate_field(
        density_field.as_mut_ptr(),
        density_field.len(),
        chunk_x * var6,
        0,
        chunk_z * var6,
        var8,
        var9,
        var10,
        temperatures.as_ptr(),
        humidities.as_ptr(),
        gen.field_715_a.as_mut() as *mut NoiseGeneratorOctaves,
        gen.field_714_b.as_mut() as *mut NoiseGeneratorOctaves,
        gen.field_703_m.as_mut() as *mut NoiseGeneratorOctaves,
        gen.field_705_k.as_mut() as *mut NoiseGeneratorOctaves,
        gen.field_704_l.as_mut() as *mut NoiseGeneratorOctaves,
    );

    for var11 in 0..var6 {
        for var12 in 0..var6 {
            for var13 in 0..16 {
                let var14 = 0.125;
                let mut var16 = density_field[(((var11 + 0) * var10 + var12 + 0) * var9 + var13 + 0) as usize];
                let mut var18 = density_field[(((var11 + 0) * var10 + var12 + 1) * var9 + var13 + 0) as usize];
                let mut var20 = density_field[(((var11 + 1) * var10 + var12 + 0) * var9 + var13 + 0) as usize];
                let mut var22 = density_field[(((var11 + 1) * var10 + var12 + 1) * var9 + var13 + 0) as usize];
                let var24 = (density_field[(((var11 + 0) * var10 + var12 + 0) * var9 + var13 + 1) as usize] - var16) * var14;
                let var26 = (density_field[(((var11 + 0) * var10 + var12 + 1) * var9 + var13 + 1) as usize] - var18) * var14;
                let var28 = (density_field[(((var11 + 1) * var10 + var12 + 0) * var9 + var13 + 1) as usize] - var20) * var14;
                let var30 = (density_field[(((var11 + 1) * var10 + var12 + 1) * var9 + var13 + 1) as usize] - var22) * var14;

                for var32 in 0..8 {
                    let var33 = 0.25;
                    let mut var35 = var16;
                    let mut var37 = var18;
                    let var39 = (var20 - var16) * var33;
                    let var41 = (var22 - var18) * var33;

                    for var43 in 0..4 {
                        let mut var44 = ((var43 + var11 * 4) << 11) | ((var12 * 4) << 7) | (var13 * 8 + var32);
                        let var45 = 128;
                        let var46 = 0.25;
                        let mut var48 = var35;
                        let var50 = (var37 - var35) * var46;

                        for var52 in 0..4 {
                            let temp_val = temperatures[((var11 * 4 + var43) * 16 + var12 * 4 + var52) as usize];
                            let mut var55 = 0;
                            if var13 * 8 + var32 < var7 {
                                if temp_val < 0.5 && var13 * 8 + var32 >= var7 - 1 {
                                    var55 = 79; // ice
                                } else {
                                    var55 = 8; // waterMoving
                                }
                            }

                            if var48 > 0.0 {
                                var55 = 1; // stone
                            }

                            blocks[var44 as usize] = var55;
                            var44 += var45;
                            var48 += var50;
                        }

                        var35 += var39;
                        var37 += var41;
                    }

                    var16 += var24;
                    var18 += var26;
                    var20 += var28;
                    var22 += var30;
                }
            }
        }
    }

    // 3. Replace blocks for biomes
    let var5_biome = 64;
    let var6_biome = 1.0 / 32.0;

    let mut field_698_r = vec![0.0; 256];
    gen.field_702_n.func_648_a(&mut field_698_r, (chunk_x * 16) as f64, (chunk_z * 16) as f64, 0.0, 16, 16, 1, var6_biome, var6_biome, 1.0);

    let mut field_697_s = vec![0.0; 256];
    gen.field_702_n.func_648_a(&mut field_697_s, (chunk_z * 16) as f64, 109.0134, (chunk_x * 16) as f64, 16, 1, 16, var6_biome, 1.0, var6_biome);

    let mut field_696_t = vec![0.0; 256];
    gen.field_701_o.func_648_a(&mut field_696_t, (chunk_x * 16) as f64, (chunk_z * 16) as f64, 0.0, 16, 16, 1, var6_biome * 2.0, var6_biome * 2.0, var6_biome * 2.0);

    for var8_b in 0..16 {
        for var9_b in 0..16 {
            let var10_b = &biomes[(var8_b * 16 + var9_b) as usize];
            let var11_b = field_698_r[(var8_b + var9_b * 16) as usize] + rand.next_double() * 0.2 > 0.0;
            let var12_b = field_697_s[(var8_b + var9_b * 16) as usize] + rand.next_double() * 0.2 > 3.0;
            let var13_b = (field_696_t[(var8_b + var9_b * 16) as usize] / 3.0 + 3.0 + rand.next_double() * 0.25) as i32;
            let mut var14_b = -1;
            let mut var15_b = var10_b.top_block;
            let mut var16_b = var10_b.filler_block;

            for var17_b in (0..=127).rev() {
                let var18_b = ((var8_b * 16 + var9_b) * 128 + var17_b) as usize;
                if var17_b <= rand.next_int_bound(5) {
                    blocks[var18_b] = 7; // bedrock
                } else {
                    let var19_b = blocks[var18_b];
                    if var19_b == 0 {
                        var14_b = -1;
                    } else if var19_b == 1 { // stone
                        if var14_b == -1 {
                            if var13_b <= 0 {
                                var15_b = 0;
                                var16_b = 1;
                            } else if var17_b >= var5_biome - 4 && var17_b <= var5_biome + 1 {
                                var15_b = var10_b.top_block;
                                var16_b = var10_b.filler_block;
                                if var12_b { var15_b = 0; }
                                if var12_b { var16_b = 13; } // gravel
                                if var11_b { var15_b = 12; } // sand
                                if var11_b { var16_b = 12; } // sand
                            }

                            if var17_b < var5_biome && var15_b == 0 {
                                var15_b = 8; // waterMoving
                            }

                            var14_b = var13_b;
                            if var17_b >= var5_biome - 1 {
                                blocks[var18_b] = var15_b;
                            } else {
                                blocks[var18_b] = var16_b;
                            }
                        } else if var14_b > 0 {
                            var14_b -= 1;
                            blocks[var18_b] = var16_b;
                        }
                    }
                }
            }
        }
    }

    // 4. Cave generation (Rust caves gen)
    let mut gen_caves = MapGenCaves::new();
    gen_caves.generate(gen.world_seed, chunk_x, chunk_z, blocks);
}

#[no_mangle]
pub unsafe extern "C" fn rust_chunk_provider_populate_batch(
    generator: *mut RustChunkProviderGenerate,
    batch: *const RustChunkDataBatch,
    accessor: WorldAccessor,
    chunk_x: i32,
    chunk_z: i32,
    biome_type_raw: i32,
    temperatures: *const f64,
) {
    let mut state = DecoratorWorldState {
        blocks: [
            (*batch).chunks[0].blocks,
            (*batch).chunks[1].blocks,
            (*batch).chunks[2].blocks,
            (*batch).chunks[3].blocks,
        ],
        metadata: [
            (*batch).chunks[0].metadata,
            (*batch).chunks[1].metadata,
            (*batch).chunks[2].metadata,
            (*batch).chunks[3].metadata,
        ],
        chunk_x,
        chunk_z,
        fallback_accessor: accessor,
    };

    // Set thread local state
    CURRENT_DECORATOR_WORLD.with(|cell| cell.set(&mut state as *mut DecoratorWorldState as *mut std::ffi::c_void));

    // Build local accessor
    let local_accessor = WorldAccessor {
        get_block_id: local_get_block_id,
        set_block_id: local_set_block_id,
        get_block_meta: local_get_block_meta,
        set_block_meta: local_set_block_meta,
        allows_attachment: local_allows_attachment,
        is_block_solid: local_is_block_solid,
        get_height_value: local_get_height_value,
    };

    // Call the existing decorator logic
    alpha_decorate_chunk(
        local_accessor,
        (*generator).world_seed,
        chunk_x,
        chunk_z,
        biome_type_raw,
        (*generator).field_713_c.as_mut() as *mut NoiseGeneratorOctaves,
        temperatures,
    );

    // Clear thread local state
    CURRENT_DECORATOR_WORLD.with(|cell| cell.set(std::ptr::null_mut()));
}
