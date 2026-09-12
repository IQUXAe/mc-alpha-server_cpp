//! Native world state: the Rust-owned replacement for the C++ `World`
//! block storage, clock, and spawn point (mirrors Java `World` storage
//! semantics). Chunks reuse the `chunk` module, block facts come from the
//! `block` table, and materials from `material`.
//!
//! v1 scope: chunk map, block access, material/light/height queries,
//! collision-box gathering for physics, plus the entity table and tracker
//! for tick integration. Generation, persistence, lighting updates, and
//! networking arrive in later slices.

use std::collections::{BTreeMap, HashMap};
use crate::aabb::AxisAlignedBB;
use crate::block::{BlockMaterial, BlockType, alpha_block_properties_get};
use crate::block_fire::{FireWorld, block_fire_added, block_fire_neighbor, block_fire_tick};
use crate::block_ticks::{
    BlockTickWorld, block_base_drop, block_cactus_added, block_cactus_neighbor, block_cactus_tick,
    block_crops_added, block_crops_neighbor, block_crops_tick, block_flower_neighbor,
    block_flower_tick, block_fluid_added, block_fluid_neighbor, block_fluid_tick,
    block_leaves_added, block_leaves_neighbor, block_leaves_tick, block_mushroom_neighbor,
    block_reed_added, block_reed_neighbor, block_reed_tick, block_sand_added, block_sand_neighbor,
    block_sand_tick, block_sapling_added, block_sapling_neighbor, block_sapling_tick,
    block_soil_added, block_soil_neighbor, block_soil_tick, block_torch_added, block_torch_neighbor,
};
use crate::chunk::Chunk;
use crate::entity_ai::{
    alpha_ai_animal_path_weight, alpha_ai_mob_path_weight, chase_speed, face_run, steer_run,
    wander_pick,
};
use crate::entity_living::{HeadingIo, MoveFeedback, alpha_living_fall_damage, living_heading_run};
use crate::entity_physics::{PushOut, alpha_entity_push};
use crate::entity_table::{
    AnimalKind, Body, Entity, EntityId, EntityTable, LivingBody, MobKind, mob_attack_reach,
    mob_burns_in_daylight,
};
use crate::tile_entity_chest::FfiChestState;
use crate::tile_entity_furnace::FfiFurnaceState;
use crate::tile_entity_sign::FfiSignState;
use crate::material::Material;
use crate::math_helper::{floor_double, sqrt_float};
use crate::pathfinder::find_path_native;
use crate::random::JavaRandom;
use crate::tracker::Tracker;

pub const WORLD_HEIGHT: i32 = 128;

/// Block ids with no collision box (mirrors the `null` returns from
/// `getCollisionBoundingBoxFromPool`: fluids, plants, torches, saplings,
/// crops, fire by type, plus rails/plates/buttons/signs/snow/reed/portal
/// by id — all `null` in Java but `Normal` in our table).
pub(crate) fn has_collision_box(block_type: u8) -> bool {
    !matches!(
        block_type,
        x if x == BlockType::Fluid as u8
            || x == BlockType::Flower as u8
            || x == BlockType::TallGrass as u8
            || x == BlockType::Mushroom as u8
            || x == BlockType::Torch as u8
            || x == BlockType::Sapling as u8
            || x == BlockType::Crops as u8
            || x == BlockType::Fire as u8
    )
}

/// Id-level no-collision extras (Java `null` boxes our table types as Normal).
pub(crate) fn has_collision_id(bid: u8) -> bool {
    !matches!(bid, 55 | 63 | 65 | 66 | 68 | 69 | 70 | 72 | 75 | 76 | 77 | 78 | 83 | 90)
}

/// True when a block id has air material (mirrors the `== &Material::air`
/// identity check: `Material` compares by capability flags, so all-false
/// materials like circuits would alias `Material::AIR` — compare the
/// material id byte instead).
fn is_air_material(bid: u8) -> bool {
    alpha_block_properties_get(bid as u32).material == BlockMaterial::Air as u8
}

/// Material for a block-table material id (mirrors `materialFromId`).
pub fn material_of(material_id: u8) -> Material {
    match material_id {
        x if x == BlockMaterial::Air as u8 => Material::AIR,
        x if x == BlockMaterial::Ground as u8 => Material::GROUND,
        x if x == BlockMaterial::Wood as u8 => Material::WOOD,
        x if x == BlockMaterial::Rock as u8 => Material::ROCK,
        x if x == BlockMaterial::Iron as u8 => Material::IRON,
        x if x == BlockMaterial::Water as u8 => Material::WATER,
        x if x == BlockMaterial::Lava as u8 => Material::LAVA,
        x if x == BlockMaterial::Leaves as u8 => Material::LEAVES,
        x if x == BlockMaterial::Plants as u8 => Material::PLANTS,
        x if x == BlockMaterial::Sponge as u8 => Material::SPONGE,
        x if x == BlockMaterial::Cloth as u8 => Material::CLOTH,
        x if x == BlockMaterial::Fire as u8 => Material::FIRE,
        x if x == BlockMaterial::Sand as u8 => Material::SAND,
        x if x == BlockMaterial::Circuits as u8 => Material::CIRCUITS,
        x if x == BlockMaterial::Glass as u8 => Material::GLASS,
        x if x == BlockMaterial::Tnt as u8 => Material::TNT,
        x if x == BlockMaterial::Ice as u8 => Material::ICE,
        x if x == BlockMaterial::Snow as u8 => Material::SNOW,
        x if x == BlockMaterial::BuiltSnow as u8 => Material::BUILT_SNOW,
        x if x == BlockMaterial::Cactus as u8 => Material::CACTUS,
        x if x == BlockMaterial::Clay as u8 => Material::CLAY,
        x if x == BlockMaterial::Pumpkin as u8 => Material::PUMPKIN,
        x if x == BlockMaterial::Portal as u8 => Material::PORTAL,
        _ => Material::AIR,
    }
}
pub struct World {
    pub seed: i64,
    pub time: i64,
    pub spawn: [i32; 3],
    /// Level name from `level-name` (used for `level.dat` LevelName).
    pub level_name: String,
    /// Peaceful/easy/normal/hard (0..3, mirrors server difficulty;
    /// scales mob-vs-player damage; default normal like the C++ server).
    pub difficulty: i32,
    /// Spawn switches (mirror `isSpawnMonsters/isSpawnAnimals`).
    pub spawn_monsters: bool,
    pub spawn_animals: bool,
    /// Scheduled block updates keyed by (time, x, y, z) like the C++
    /// `scheduledTicks` set (duplicate keys keep the FIRST entry).
    scheduled: BTreeMap<(i64, i32, i32, i32), u8>,
    /// Leaves-decay search guard (mirrors the singleton `BlockLeaves`
    /// instance field threaded through the decay drivers).
    leaves_guard: i32,
    /// Chunk unload radius in chunks (mirrors view distance + 2).
    pub unload_radius: i32,
    /// Unloaded chunks with their entity spill (mirrors the leveldb
    /// round-trip: evicted here, thawed back on recall; disk eviction
    /// arrives with the persistence slice).
    unloaded: HashMap<(i32, i32), Chunk>,
    /// Block-entity storage by cell (mirrors the chunk `TileEntity` map;
    /// furnaces tick in [`World::tick_furnaces`], NBT here).
    pub tiles: HashMap<(i32, i32, i32), TileData>,
    /// Cells whose furnace block flipped 61 <-> 62 on a recent tick
    /// (mirrors the C++ `markBlockNeedsUpdate` after
    /// `updateFurnaceBlockState`); the server tick drains these and fans
    /// out block changes to chunk-loaded players.
    pub furnace_updates: Vec<[i32; 3]>,
    /// Full pickups since the last server tick as `(item, player)` pairs
    /// (mirrors the collect packet in `EntityPlayerMP.onUpdate`); the
    /// server tick drains these into `Packet22Collect` fan-out plus an
    /// inventory sync for the picker. Recorded only on full takes, like
    /// vanilla (partial merges leave the item down with no packet).
    pub item_pickups: Vec<(EntityId, EntityId)>,
    /// Deaths since the last server tick (mirrors the status-3 broadcast
    /// in `EntityLiving.onDeath`); the server tick drains these before
    /// the tracker retires the rows, so the animation precedes destroy.
    pub death_events: Vec<EntityId>,
    /// Primed TNT pending blasts as `(x, y, z, ticks_left)` (our stand-in
    /// for `EntityTNTPrimed`: the block is already air, the blast lands at
    /// radius 4 when the fuse runs out — 80 ticks hand-lit, 10..30 chained).
    pub pending_tnt: Vec<(i32, i32, i32, i32)>,
    /// Population guard (mirrors `World::isPopulating`): decoration
    /// sets bypass skylight regen exactly like the C++ populate path
    /// (the write-back regenerates explicitly instead).
    populating: bool,
    /// Terrain generator, built lazily (eleven octave tables; tests that
    /// never generate pay nothing; skipped in `Debug` dumps).
    generator: Option<crate::generator::RustChunkProviderGenerate>,
    chunks: HashMap<(i32, i32), Chunk>,
    pub entities: EntityTable,
    pub tracker: Tracker,
    rng: JavaRandom,
}

impl std::fmt::Debug for World {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("World")
            .field("seed", &self.seed)
            .field("time", &self.time)
            .field("spawn", &self.spawn)
            .field("difficulty", &self.difficulty)
            .field("chunks", &self.chunks.len())
            .field("entities", &self.entities.len())
            .field("scheduled", &self.scheduled.len())
            .finish_non_exhaustive()
    }
}

impl Default for World {
    fn default() -> Self {
        World::new(0)
    }
}

impl World {
    pub fn new(seed: i64) -> Self {
        World {
            seed,
            time: 0,
            spawn: [0, 64, 0],
            level_name: "world".to_string(),
            difficulty: 2,
            spawn_monsters: true,
            spawn_animals: true,
            scheduled: BTreeMap::new(),
            leaves_guard: 0,
            unload_radius: 10,
            unloaded: HashMap::new(),
            tiles: HashMap::new(),
            furnace_updates: Vec::new(),
            item_pickups: Vec::new(),
            death_events: Vec::new(),
            pending_tnt: Vec::new(),
            populating: false,
            generator: None,
            chunks: HashMap::new(),
            entities: EntityTable::new(),
            tracker: Tracker::new(),
            rng: JavaRandom::new(seed),
        }
    }

    pub fn insert_chunk(&mut self, chunk: Chunk) {
        self.chunks.insert((chunk.x_position, chunk.z_position), chunk);
    }

    pub fn chunk_count(&self) -> usize {
        self.chunks.len()
    }

    /// Crate-visible chunk lookup for persistence.
    pub(crate) fn chunk_ref(&self, cx: i32, cz: i32) -> Option<&Chunk> {
        self.chunks.get(&(cx, cz))
    }

    /// Crate-visible mutable chunk lookup (the server clears the
    /// save-dirty flag after flushing a chunk to the store).
    pub(crate) fn chunk_ref_mut(&mut self, cx: i32, cz: i32) -> Option<&mut Chunk> {
        self.chunks.get_mut(&(cx, cz))
    }

    /// Loaded chunk coordinates (mirrors the `chunks_` snapshot at the
    /// head of the C++ `saveWorld`).
    pub(crate) fn loaded_chunk_coords(&self) -> Vec<(i32, i32)> {
        self.chunks.keys().copied().collect()
    }

    /// Reseed the world RNG stream from the current seed (mirrors the C++
    /// world loading its seed before use; `load_level_from` calls this so
    /// a loaded world does not keep the constructor stream).
    pub(crate) fn reseed(&mut self) {
        self.rng = JavaRandom::new(self.seed);
    }

    /// Crate-visible RNG draws (driver shims share the world stream).
    pub(crate) fn rng_next_int(&mut self, bound: i32) -> i32 {
        if bound <= 0 {
            return 0;
        }
        self.rng.next_int_bound(bound)
    }

    pub(crate) fn rng_next_f64(&mut self) -> f64 {
        self.rng.next_double()
    }

    pub(crate) fn rng_next_f32(&mut self) -> f32 {
        self.rng.next_float()
    }

    pub fn has_chunk(&self, cx: i32, cz: i32) -> bool {
        self.chunks.contains_key(&(cx, cz))
    }

    fn chunk_of(x: i32, z: i32) -> (i32, i32, i32, i32) {
        (x.div_euclid(16), z.div_euclid(16), x.rem_euclid(16), z.rem_euclid(16))
    }

    pub fn get_block_id(&self, x: i32, y: i32, z: i32) -> u8 {
        Self::block_id_in(&self.chunks, x, y, z)
    }

    /// Chunk-map half of [`World::get_block_id`]: split out so AI closures
    /// can borrow the map while the RNG field is borrowed mutably elsewhere
    /// (disjoint field borrows; same formula, one flow).
    fn block_id_in(chunks: &HashMap<(i32, i32), Chunk>, x: i32, y: i32, z: i32) -> u8 {
        if y < 0 || y >= WORLD_HEIGHT {
            return 0;
        }
        let (cx, cz, lx, lz) = Self::chunk_of(x, z);
        chunks.get(&(cx, cz)).map(|c| c.get_block_id(lx, y, lz)).unwrap_or(0)
    }

    pub fn get_block_meta(&self, x: i32, y: i32, z: i32) -> u8 {
        if y < 0 || y >= WORLD_HEIGHT {
            return 0;
        }
        let (cx, cz, lx, lz) = Self::chunk_of(x, z);
        self.chunks.get(&(cx, cz)).map(|c| c.get_block_metadata(lx, y, lz)).unwrap_or(0)
    }

    /// Missing chunk or out-of-range Y: no-op returning false (mirrors the
    /// NoChunkLoad setters swallowing silently). On change the skylight
    /// follows the C++ `setBlock` path (full regen while a world holds
    /// the chunk); population sets bypass it via [`World::populating`].
    pub fn set_block_id(&mut self, x: i32, y: i32, z: i32, id: u8) -> bool {
        if y < 0 || y >= WORLD_HEIGHT {
            return false;
        }
        let (cx, cz, lx, lz) = Self::chunk_of(x, z);
        let changed = self.chunks.get_mut(&(cx, cz)).map(|c| c.set_block_id(lx, y, lz, id)).unwrap_or(false);
        if changed && !self.populating {
            self.refresh_skylight(x, z);
        }
        changed
    }

    /// Recompute skylight for the touched column's chunk (mirrors the
    /// C++ regen on set; the native BFS pass stays single-chunk, so a
    /// one-cell fringe seam at borders is a known approximation until
    /// the pass learns cross-chunk spread like the C++ one does).
    fn refresh_skylight(&mut self, x: i32, z: i32) {
        let (cx, cz, _, _) = Self::chunk_of(x, z);
        if let Some(c) = self.chunks.get_mut(&(cx, cz)) {
            c.generate_skylight_map();
        }
    }

    pub fn set_block_meta(&mut self, x: i32, y: i32, z: i32, meta: u8) -> bool {
        if y < 0 || y >= WORLD_HEIGHT {
            return false;
        }
        let (cx, cz, lx, lz) = Self::chunk_of(x, z);
        self.chunks
            .get_mut(&(cx, cz))
            .map(|c| {
                c.set_block_metadata(lx, y, lz, meta);
                true
            })
            .unwrap_or(false)
    }

    pub fn material_at(&self, x: i32, y: i32, z: i32) -> Material {
        Self::material_in(&self.chunks, x, y, z)
    }

    /// Chunk-map half of [`World::material_at`] (see `block_id_in`).
    fn material_in(chunks: &HashMap<(i32, i32), Chunk>, x: i32, y: i32, z: i32) -> Material {
        let id = Self::block_id_in(chunks, x, y, z);
        if id == 0 {
            return Material::AIR;
        }
        material_of(alpha_block_properties_get(id as u32).material)
    }

    pub fn is_solid(&self, x: i32, y: i32, z: i32) -> bool {
        // ID list mirrors World::isBlockSolidNoChunkLoad exactly
        // (NOT material-based: torches and the like count as solid here).
        if y < 0 || y >= WORLD_HEIGHT {
            return false;
        }
        match self.get_block_id(x, y, z) {
            0 | 8 | 9 | 10 | 11 | 78 | 37 | 38 | 39 | 40 | 83 | 51 | 6 => false,
            _ => true,
        }
    }

    pub fn is_water(&self, x: i32, y: i32, z: i32) -> bool {
        self.material_at(x, y, z) == Material::WATER
    }

    pub fn is_lava(&self, x: i32, y: i32, z: i32) -> bool {
        self.material_at(x, y, z) == Material::LAVA
    }

    pub fn get_height_value(&self, x: i32, z: i32) -> i32 {
        let (cx, cz, lx, lz) = Self::chunk_of(x, z);
        self.chunks.get(&(cx, cz)).map(|c| c.get_height_value(lx, lz)).unwrap_or(0)
    }

    /// Saved light by type (mirrors `World::getSavedLightValue`:
    /// 0 = sky, 1 = block). Out-of-range and missing chunks read 0,
    /// except above the world where sky reads 15.
    pub fn saved_light_value(&self, kind: u8, x: i32, y: i32, z: i32) -> u8 {
        Self::saved_light_in(&self.chunks, kind, x, y, z)
    }

    /// Chunk-map half of [`World::saved_light_value`] (see `block_id_in`).
    fn saved_light_in(chunks: &HashMap<(i32, i32), Chunk>, kind: u8, x: i32, y: i32, z: i32) -> u8 {
        if y < 0 {
            return 0;
        }
        if y >= WORLD_HEIGHT {
            return if kind == 0 { 15 } else { 0 };
        }
        let (cx, cz, lx, lz) = Self::chunk_of(x, z);
        chunks
            .get(&(cx, cz))
            .map(|c| c.get_saved_light_value(kind as i32, lx, y, lz))
            .unwrap_or(0)
    }

    /// Combined light (mirrors `World::getBlockLightValue`).
    pub fn block_light_value(&self, x: i32, y: i32, z: i32) -> u8 {
        Self::block_light_in(&self.chunks, x, y, z)
    }

    /// Chunk-map half of [`World::block_light_value`] (see `block_id_in`).
    fn block_light_in(chunks: &HashMap<(i32, i32), Chunk>, x: i32, y: i32, z: i32) -> u8 {
        if y < 0 || y >= WORLD_HEIGHT {
            return 0;
        }
        Self::saved_light_in(chunks, 0, x, y, z).max(Self::saved_light_in(chunks, 1, x, y, z))
    }

    /// Collision boxes of blocks overlapping `mask` (mirrors
    /// `World::getCollidingBoundingBoxes` over loaded chunks only).
    pub fn colliding_boxes(&self, mask: &AxisAlignedBB) -> Vec<AxisAlignedBB> {
        let mut out = Vec::new();
        let min_bx = mask.min_x.floor() as i32;
        let max_bx = mask.max_x.floor() as i32;
        let min_by = mask.min_y.floor() as i32;
        let max_by = mask.max_y.floor() as i32;
        let min_bz = mask.min_z.floor() as i32;
        let max_bz = mask.max_z.floor() as i32;
        for x in min_bx..=max_bx {
            for y in min_by..=max_by {
                for z in min_bz..=max_bz {
                    let id = self.get_block_id(x, y, z);
                    if id == 0 {
                        continue;
                    }
                    let props = alpha_block_properties_get(id as u32);
                    if !has_collision_box(props.block_type) || !has_collision_id(id) {
                        continue;
                    }
                    let bb = AxisAlignedBB::get_bounding_box(
                        x as f64 + props.min_x as f64,
                        y as f64 + props.min_y as f64,
                        z as f64 + props.min_z as f64,
                        x as f64 + props.max_x as f64,
                        y as f64 + props.max_y as f64,
                        z as f64 + props.max_z as f64,
                    );
                    if mask.intersects_with(&bb) {
                        out.push(bb);
                    }
                }
            }
        }
        out
    }

    /// Closest living-or-not player within range, like `getClosestPlayer`
    /// (strict `<`, first minimum wins; ids resolved by the caller).
    pub fn closest_player(&self, x: f64, y: f64, z: f64, max_dist: f64) -> Option<EntityId> {
        let mut best: Option<(EntityId, f64)> = None;
        let mut ids: Vec<EntityId> = self.entities.alive_ids();
        ids.sort_unstable();
        for id in ids {
            let e = self.entities.get(id)?;
            if !matches!(e, crate::entity_table::Entity::Player(_)) {
                continue;
            }
            let d = e.body().distance_sq(x, y, z);
            if d < max_dist * max_dist && best.map(|(_, b)| d < b).unwrap_or(true) {
                best = Some((id, d));
            }
        }
        best.map(|(id, _)| id)
    }
}

/// Block types C++ reports as replaceable (mirrors the `isReplaceable`
/// overrides: flower, tall grass, torch, reed, sapling, crops — notably
/// NOT mushroom, cactus, leaves, soil, or fluids).
pub fn is_replaceable(block_id: u8) -> bool {
    if block_id == 0 {
        return false;
    }
    matches!(
        alpha_block_properties_get(block_id as u32).block_type,
        x if x == BlockType::Flower as u8
            || x == BlockType::TallGrass as u8
            || x == BlockType::Torch as u8
            || x == BlockType::Reed as u8
            || x == BlockType::Sapling as u8
            || x == BlockType::Crops as u8
    )
}

impl World {
    /// Spawn a loose item into the table (mirrors the common drop shape:
    /// default 10-tick pickup delay).
    pub fn spawn_item_entity(&mut self, item_id: i32, count: i32, damage: i32, x: f64, y: f64, z: f64) -> EntityId {
        use crate::entity_table::ItemEnt;
        let id = self.entities.alloc_id();
        let mut b = Body::new(id, 0.25, 0.25, 0.125);
        b.set_position(x, y, z);
        self.entities.insert(Entity::Item(ItemEnt {
            body: b,
            item_id,
            count,
            damage,
            age: 0,
            pickup_delay: 10,
        }));
        id
    }

    /// Y-X-Z collision move for one body (mirrors `Entity::moveEntity`
    /// without the soil-walking sound, which arrives with the block phase).
    /// Returns the fall event distance when `onFall` must fire.
    pub fn move_body(&mut self, id: EntityId, dx: f64, dy: f64, dz: f64) -> Option<f32> {
        let (orig, no_clip, step, was_ground, suppress, fall, sneaking) = match self.entities.get(id) {
            Some(e) => {
                let b = e.body();
                let sneak = match e {
                    Entity::Player(p) => p.living.sneaking,
                    _ => false,
                };
                (
                    b.bounding_box.clone(),
                    b.no_clip,
                    b.step_height,
                    b.on_ground,
                    b.suppress_fall_state,
                    b.fall_distance,
                    sneak,
                )
            }
            None => return None,
        };
        let (old_x, old_y, old_z) = (dx, dy, dz);
        let (mut mx, mut my, mut mz) = (dx, dy, dz);
        // Sneak edge-stop (Java Entity.moveEntity:212-234): on ground while
        // sneaking, trim each horizontal axis in 0.05 steps so the destination
        // still has ground below.
        if was_ground && sneaking {
            let mut nx = mx;
            while nx != 0.0 && self.colliding_boxes(&orig.get_offset_bounding_box(nx, -1.0, 0.0)).is_empty() {
                if nx < 0.05 && nx >= -0.05 {
                    nx = 0.0;
                } else if nx > 0.0 {
                    nx -= 0.05;
                } else {
                    nx += 0.05;
                }
            }
            mx = nx;
            let mut nz = mz;
            while nz != 0.0 && self.colliding_boxes(&orig.get_offset_bounding_box(0.0, -1.0, nz)).is_empty() {
                if nz < 0.05 && nz >= -0.05 {
                    nz = 0.0;
                } else if nz > 0.0 {
                    nz -= 0.05;
                } else {
                    nz += 0.05;
                }
            }
            mz = nz;
        }
        let mut work = orig.clone();
        if no_clip {
            if let Some(e) = self.entities.get_mut(id) {
                let b = e.body_mut();
                let (px, py, pz) = (b.pos[0] + mx, b.pos[1] + my, b.pos[2] + mz);
                b.set_position(px, py, pz);
            }
        } else {
            let boxes = self.colliding_boxes(&work.add_coord(mx, my, mz));
            for cb in &boxes {
                my = cb.calculate_y_offset(&work, my);
            }
            work.offset(0.0, my, 0.0);
            for cb in &boxes {
                mx = cb.calculate_x_offset(&work, mx);
            }
            work.offset(mx, 0.0, 0.0);
            for cb in &boxes {
                mz = cb.calculate_z_offset(&work, mz);
            }
            work.offset(0.0, 0.0, mz);

            if step > 0.0 && (was_ground || (old_y != my && old_y < 0.0)) && (old_x != mx || old_z != mz) {
                let boxes = self.colliding_boxes(&orig.add_coord(old_x, step as f64, old_z));
                let (mut sx, mut sy, mut sz) = (old_x, step as f64, old_z);
                let mut sbox = orig.clone();
                for cb in &boxes {
                    sy = cb.calculate_y_offset(&sbox, sy);
                }
                sbox.offset(0.0, sy, 0.0);
                for cb in &boxes {
                    sx = cb.calculate_x_offset(&sbox, sx);
                }
                sbox.offset(sx, 0.0, 0.0);
                for cb in &boxes {
                    sz = cb.calculate_z_offset(&sbox, sz);
                }
                sbox.offset(0.0, 0.0, sz);
                if sx * sx + sz * sz > mx * mx + mz * mz {
                    work = sbox;
                    mx = sx;
                    my = sy;
                    mz = sz;
                }
            }

            let falling = {
                let e = self.entities.get_mut(id)?;
                let b = e.body_mut();
                b.bounding_box = work.clone();
                b.pos[0] = (work.min_x + work.max_x) / 2.0;
                b.pos[1] = work.min_y + b.y_offset as f64;
                b.pos[2] = (work.min_z + work.max_z) / 2.0;
                b.collided_horiz = old_x != mx || old_z != mz;
                b.collided_vert = old_y != my;
                b.on_ground = old_y != my && old_y < 0.0;
                if old_x != mx {
                    b.motion[0] = 0.0;
                }
                if old_y != my {
                    b.motion[1] = 0.0;
                }
                if old_z != mz {
                    b.motion[2] = 0.0;
                }
                (b.on_ground, my)
            };
            // Contact damage (mirrors the `onEntityCollidedWithBlock`
            // sweep at the tail of `Entity.moveEntity`): any cactus in
            // the post-move box deals 1 through the pipeline.
            self.cactus_contact(id);
            if !suppress {
                let mut ev = -1.0f32;
                let nd =
                    crate::entity_physics::alpha_entity_fall_step(falling.0, falling.1, fall, &mut ev);
                if let Some(e) = self.entities.get_mut(id) {
                    e.body_mut().fall_distance = nd;
                }
                if ev >= 0.0 {
                    return Some(ev);
                }
            }
        }
        None
    }

    /// Cactus prickles for one living body (mirrors
    /// `BlockCactus.onEntityCollidedWithBlock` as fired from
    /// `Entity.moveEntity`): any cactus cell intersecting the box deals
    /// 1 damage through the attack pipeline (resist window included,
    /// like vanilla). Only mobs, animals and players qualify.
    fn cactus_contact(&mut self, id: EntityId) {
        let bbox = match self.entities.get(id) {
            Some(Entity::Mob(m)) => m.living.body.bounding_box,
            Some(Entity::Animal(a)) => a.living.body.bounding_box,
            Some(Entity::Player(p)) => p.living.body.bounding_box,
            _ => return,
        };
        let (x0, y0, z0) = (
            floor_double(bbox.min_x),
            floor_double(bbox.min_y),
            floor_double(bbox.min_z),
        );
        let (x1, y1, z1) = (
            floor_double(bbox.max_x),
            floor_double(bbox.max_y),
            floor_double(bbox.max_z),
        );
        for x in x0..=x1 {
            for y in y0..=y1 {
                for z in z0..=z1 {
                    if self.get_block_id(x, y, z) == 81 {
                        self.attack_living(id, 1, None);
                        return;
                    }
                }
            }
        }
    }

    /// Item push-out from solid rock (mirrors `pushOutOfBlocks`).
    pub fn item_push_out(&mut self, id: EntityId) {
        let (ix, iy, iz, lx, ly, lz) = match self.entities.get(id) {
            Some(Entity::Item(e)) => {
                let (x, y, z) = (e.body.pos[0], e.body.pos[1], e.body.pos[2]);
                (
                    x.floor() as i32,
                    y.floor() as i32,
                    z.floor() as i32,
                    x - (x.floor()),
                    y - (y.floor()),
                    z - (z.floor()),
                )
            }
            _ => return,
        };
        if !self.is_solid(ix, iy, iz) {
            return;
        }
        let side = crate::entity_misc::alpha_item_push_side(
            !self.is_solid(ix - 1, iy, iz),
            !self.is_solid(ix + 1, iy, iz),
            !self.is_solid(ix, iy - 1, iz),
            !self.is_solid(ix, iy + 1, iz),
            !self.is_solid(ix, iy, iz - 1),
            !self.is_solid(ix, iy, iz + 1),
            lx,
            ly,
            lz,
        );
        if side < 0 {
            return;
        }
        let impulse = self.rng.next_double() * 0.2 + 0.1;
        if let Some(Entity::Item(e)) = self.entities.get_mut(id) {
            match side {
                0 => e.body.motion[0] = -impulse,
                1 => e.body.motion[0] = impulse,
                2 => e.body.motion[1] = -impulse,
                3 => e.body.motion[1] = impulse,
                4 => e.body.motion[2] = -impulse,
                _ => e.body.motion[2] = impulse,
            }
        }
    }

    /// Loose-item tick (mirrors `EntityItem::tick`).
    pub fn tick_item(&mut self, id: EntityId) {
        self.entities.tick_base(id);
        let alive = match self.entities.get_mut(id) {
            Some(Entity::Item(e)) => {
                if e.pickup_delay > 0 {
                    e.pickup_delay -= 1;
                }
                e.age += 1;
                if e.age >= 6000 {
                    e.body.dead = true;
                    return;
                }
                e.body.motion[1] -= 0.04;
                (e.body.motion[0], e.body.motion[1], e.body.motion[2])
            }
            _ => return,
        };
        self.item_push_out(id);
        self.move_body(id, alive.0, alive.1, alive.2);
        if let Some(Entity::Item(e)) = self.entities.get_mut(id) {
            let mut m = crate::entity_misc::ItemMotion { mx: e.body.motion[0], my: e.body.motion[1], mz: e.body.motion[2] };
            crate::entity_misc::alpha_item_damp(e.body.on_ground, &mut m);
            e.body.motion = [m.mx, m.my, m.mz];
        }
    }

        /// Falling-sand tick (mirrors `EntityFallingSand::tick`).
    pub fn tick_falling(&mut self, id: EntityId) {        self.entities.tick_base(id);
        let (block_id, motion) = match self.entities.get_mut(id) {
            Some(Entity::Falling(e)) => {
                if e.block_id == 0 {
                    e.body.dead = true;
                    return;
                }
                e.fall_time += 1;
                e.body.motion[1] -= 0.04;
                (e.block_id, (e.body.motion[0], e.body.motion[1], e.body.motion[2]))
            }
            _ => return,
        };
        self.move_body(id, motion.0, motion.1, motion.2);
        if let Some(Entity::Falling(e)) = self.entities.get_mut(id) {
            e.body.motion[0] *= 0.98;
            e.body.motion[1] *= 0.98;
            e.body.motion[2] *= 0.98;
        }
        let (on_ground, by, px, py, pz) = match self.entities.get(id) {
            Some(Entity::Falling(e)) => {
                (e.body.on_ground, e.body.pos[1].floor() as i32, e.body.pos[0], e.body.pos[1], e.body.pos[2])
            }
            _ => return,
        };
        let bx = px.floor() as i32;
        let bz = pz.floor() as i32;
        let land_id = self.get_block_id(bx, by, bz) as i32;
        let fall_time = match self.entities.get(id) {
            Some(Entity::Falling(e)) => e.fall_time,
            _ => return,
        };
        let action = crate::entity_misc::alpha_falling_land(
            block_id, on_ground, by, land_id,
            is_replaceable(land_id as u8),
            (1..256).contains(&block_id),
            fall_time,
        );
        match action {
            1 => {
                if let Some(Entity::Falling(e)) = self.entities.get_mut(id) {
                    e.body.dead = true;
                }
                self.set_block_id(bx, by, bz, block_id as u8);
            }
            2 => {
                let drop_y = if on_ground { py + 0.5 } else { py };
                if let Some(Entity::Falling(e)) = self.entities.get_mut(id) {
                    e.body.dead = true;
                }
                self.spawn_item_entity(block_id, 1, 0, px, drop_y, pz);
            }
            _ => {}
        }
    }

    /// Boat damage (mirrors `EntityBoat::attackEntityFrom`): rock the boat,
    /// break past 40 damage with plank/stick drops. Returns true when the
    /// boat broke.
    pub fn damage_boat(&mut self, id: EntityId, amount: i32) -> bool {
        if amount <= 0 {
            return false;
        }
        let broke = match self.entities.get_mut(id) {
            Some(Entity::Boat(b)) => {
                if b.body.dead {
                    return false;
                }
                b.forward_dir = -b.forward_dir;
                b.time_since_hit = 10;
                b.damage_taken += amount * 10;
                b.damage_taken > 40
            }
            _ => return false,
        };
        if !broke {
            return false;
        }
        // Eject the rider like C++ before dropping materials.
        let rider = self.entities.get(id).map(|e| e.body().ridden_by).unwrap_or(-1);
        if rider >= 0 {
            self.entities.mount(rider, None);
        }
        let (px, py, pz) = match self.entities.get(id) {
            Some(e) => (e.body().pos[0], e.body().pos[1], e.body().pos[2]),
            None => return true,
        };
        for _ in 0..3 {
            self.spawn_item_entity(5, 1, 0, px, py, pz);
        }
        for _ in 0..2 {
            self.spawn_item_entity(280, 1, 0, px, py, pz);
        }
        if let Some(Entity::Boat(b)) = self.entities.get_mut(id) {
            b.body.dead = true;
        }
        true
    }

    /// Boat tick (mirrors `EntityBoat::tick`).
    pub fn tick_boat(&mut self, id: EntityId) {
        self.entities.tick_base(id);
        let alive = match self.entities.get_mut(id) {
            Some(Entity::Boat(b)) => {
                if b.body.dead {
                    return;
                }
                if b.time_since_hit > 0 {
                    b.time_since_hit -= 1;
                }
                if b.damage_taken > 0 {
                    b.damage_taken -= 1;
                }
                // Eject non-player riders (custom-logic edge case).
                let rider = b.body.ridden_by;
                (rider, b.body.motion[0], b.body.motion[1], b.body.motion[2])
            }
            _ => return,
        };
        if alive.0 >= 0 {
            let is_player = matches!(self.entities.get(alive.0), Some(Entity::Player(_)));
            if !is_player {
                self.entities.mount(alive.0, None);
            }
        }
        // Buoyancy from the water fraction under the hull.
        let (min_x, min_y, min_z, max_x, max_y, max_z) = match self.entities.get(id) {
            Some(e) => {
                let b = e.body();
                (b.bounding_box.min_x, b.bounding_box.min_y, b.bounding_box.min_z,
                 b.bounding_box.max_x, b.bounding_box.max_y, b.bounding_box.max_z)
            }
            None => return,
        };
        let fraction = crate::entity_misc::water_fraction_scan(min_x, min_y, min_z, max_x, max_y, max_z, |x, y, z| {
            self.is_water(x, y, z)
        });
        // Rider drive.
        let rider_motion = match self.entities.get(id) {
            Some(e) => {
                let r = e.body().ridden_by;
                if r >= 0 {
                    self.entities.get(r).map(|re| (re.body().motion[0], re.body().motion[2]))
                } else {
                    None
                }
            }
            None => return,
        };
        if let Some(Entity::Boat(b)) = self.entities.get_mut(id) {
            b.body.motion[1] += 0.04 * (fraction * 2.0 - 1.0);
            if let Some((rx, rz)) = rider_motion {
                b.body.motion[0] += rx * 0.2;
                b.body.motion[2] += rz * 0.2;
            }
            b.body.motion[0] = b.body.motion[0].clamp(-0.4, 0.4);
            b.body.motion[2] = b.body.motion[2].clamp(-0.4, 0.4);
            if b.body.on_ground {
                b.body.motion[0] *= 0.5;
                b.body.motion[1] *= 0.5;
                b.body.motion[2] *= 0.5;
            }
        }
        let motion = match self.entities.get(id) {
            Some(e) => (e.body().motion[0], e.body().motion[1], e.body().motion[2]),
            None => return,
        };
        self.move_body(id, motion.0, motion.1, motion.2);
        // Crash: eject, drop, die.
        let crash = match self.entities.get(id) {
            Some(Entity::Boat(b)) => {
                let speed =
                    (b.body.motion[0] * b.body.motion[0] + b.body.motion[2] * b.body.motion[2]).sqrt();
                b.body.collided_horiz && speed > 0.15
            }
            _ => return,
        };
        if crash {
            let rider = self.entities.get(id).map(|e| e.body().ridden_by).unwrap_or(-1);
            if rider >= 0 {
                self.entities.mount(rider, None);
            }
            let (px, py, pz) = match self.entities.get(id) {
                Some(e) => (e.body().pos[0], e.body().pos[1], e.body().pos[2]),
                None => return,
            };
            for _ in 0..3 {
                self.spawn_item_entity(5, 1, 0, px, py, pz);
            }
            for _ in 0..2 {
                self.spawn_item_entity(280, 1, 0, px, py, pz);
            }
            if let Some(Entity::Boat(b)) = self.entities.get_mut(id) {
                b.body.dead = true;
            }
            return;
        }
        if let Some(Entity::Boat(b)) = self.entities.get_mut(id) {
            b.body.motion[0] *= 0.99;
            b.body.motion[1] *= 0.95;
            b.body.motion[2] *= 0.99;
        }
        // Yaw follows travel direction.
        let (dx, dz, yaw) = match self.entities.get(id) {
            Some(e) => {
                let b = e.body();
                (b.pos[0] - b.prev_pos[0], b.pos[2] - b.prev_pos[2], b.yaw)
            }
            None => return,
        };
        // NOTE: C++ reads prevPos AFTER move (already synced by tick_base
        // at the START of next tick); here prev holds the pre-move value
        // from this tick's tick_base, which matches because C++ compares
        // post-move pos against the same pre-move snapshot.
        let mut new_yaw = yaw;
        crate::entity_misc::alpha_boat_steer(dx, dz, yaw, &mut new_yaw);
        if let Some(Entity::Boat(b)) = self.entities.get_mut(id) {
            b.body.yaw = new_yaw;
            b.body.pitch = 0.0;
        }
        // Boat-on-boat shoves.
        let boats: Vec<EntityId> = self
            .entities
            .alive_ids()
            .into_iter()
            .filter(|oid| {
                *oid != id && matches!(self.entities.get(*oid), Some(Entity::Boat(_)))
            })
            .collect();
        for oid in boats {
            let (ax, az, bx, bz) = match (self.entities.get(id), self.entities.get(oid)) {
                (Some(a), Some(b)) => (a.body().pos[0], a.body().pos[2], b.body().pos[0], b.body().pos[2]),
                _ => continue,
            };
            let mut push = crate::entity_physics::PushOut { dvx1: 0.0, dvz1: 0.0, dvx2: 0.0, dvz2: 0.0 };
            let ok = crate::entity_physics::alpha_entity_push(ax, az, bx, bz, true, true, &mut push);
            if !ok {
                continue;
            }
            if let Some(Entity::Boat(b)) = self.entities.get_mut(id) {
                b.body.motion[0] += push.dvx1;
                b.body.motion[2] += push.dvz1;
            }
            if let Some(Entity::Boat(o)) = self.entities.get_mut(oid) {
                o.body.motion[0] += push.dvx2;
                o.body.motion[2] += push.dvz2;
            }
        }
        self.entities.update_rider_position(id);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::entity_table::{LivingBody, MobEnt, PlayerEnt};

    fn world_with_floor() -> World {
        let mut w = World::new(1234);
        let mut c = Chunk::new(0, 0);
        for x in 0..16 {
            for z in 0..16 {
                c.set_block_id(x, 63, z, 1); // stone floor
            }
        }
        c.generate_height_map();
        w.insert_chunk(c);
        w
    }

    fn add_player(w: &mut World, name: &str, x: f64, y: f64, z: f64) -> EntityId {
        let id = w.entities.alloc_id();
        let mut p = PlayerEnt::new(id, name);
        p.living.body.set_position(x, y, z);
        p.respawn_ticks = 0; // tests fight immediately; spawns get immunity
        w.entities.insert(crate::entity_table::Entity::Player(p));
        id
    }

    #[test]
    fn test_block_access_and_missing_chunks() {
        let mut w = world_with_floor();
        assert_eq!(w.get_block_id(3, 63, 4), 1);
        assert_eq!(w.get_block_id(1000, 63, 1000), 0);
        assert_eq!(w.get_block_id(0, 200, 0), 0);
        assert!(w.set_block_id(3, 64, 4, 5));
        assert_eq!(w.get_block_id(3, 64, 4), 5);
        assert!(!w.set_block_id(1000, 64, 1000, 5));
        assert!(!w.set_block_id(0, 200, 0, 5));
        assert_eq!(w.get_height_value(3, 4), 65);
    }

    #[test]
    fn test_set_block_regenerates_skylight() {
        let mut w = world_with_floor();
        // Lone pillar: the stone goes dark and the cell below gets
        // sideways leak (mirrors the C++ setBlock skylight regen; the
        // fresh world starts fully dark, so any light proves the pass).
        w.set_block_id(3, 70, 4, 1);
        assert_eq!(w.saved_light_value(0, 3, 70, 4), 0);
        assert_eq!(w.saved_light_value(0, 3, 69, 4), 14);
        // Pull the pillar: full sky again.
        w.set_block_id(3, 70, 4, 0);
        assert_eq!(w.saved_light_value(0, 3, 69, 4), 15);
        assert_eq!(w.saved_light_value(0, 3, 127, 4), 15);
    }

    fn ceiling_world() -> World {
        // Two floored chunks with a two-wide stone lid at y=100 hugging
        // the border from the center side (x=14,15).
        let mut w = World::new(99);
        for (cx, cz) in [(0, 0), (1, 0)] {
            let mut c = Chunk::new(cx, cz);
            for x in 0..16 {
                for z in 0..16 {
                    c.set_block_id(x, 63, z, 1);
                }
            }
            c.generate_height_map();
            w.insert_chunk(c);
        }
        for lx in [14, 15] {
            w.chunks.get_mut(&(0, 0)).unwrap().set_block_id(lx, 100, 8, 1);
        }
        for (cx, cz) in [(0, 0), (1, 0)] {
            w.chunks.get_mut(&(cx, cz)).unwrap().generate_skylight_map();
        }
        w
    }

    #[test]
    fn test_set_block_on_border_refreshes_center() {
        // Same border edit two ways: the world path regenerates the
        // center chunk (fringe opens 14 -> 15); the chunk-direct path
        // leaves it stale. The neighbor chunk agrees on both paths by
        // design: the native BFS stays single-chunk (documented seam;
        // C++ spreads across, a future pass may teach it).
        let mut a = ceiling_world();
        a.set_block_id(15, 100, 8, 0);
        let mut b = ceiling_world();
        b.chunks.get_mut(&(0, 0)).unwrap().set_block_id(15, 100, 8, 0);
        assert_eq!(a.saved_light_value(0, 15, 99, 8), 15);
        assert_eq!(b.saved_light_value(0, 15, 99, 8), 14);
        assert_eq!(a.saved_light_value(0, 16, 99, 8), 15);
        assert_eq!(b.saved_light_value(0, 16, 99, 8), 15);
    }

    #[test]
    fn test_material_queries() {
        let w = world_with_floor();
        assert!(w.is_solid(3, 63, 4));
        // ID-list rule, not material: torch counts as solid here.
        assert!(!w.is_solid(3, 70, 4));
        assert!(!w.is_water(3, 63, 4));
        assert_eq!(w.material_at(9, 9, 9), Material::AIR);
    }

    #[test]
    fn test_light_defaults_match_cpp() {
        let w = world_with_floor();
        assert_eq!(w.saved_light_value(0, 1000, 64, 1000), 0);
        assert_eq!(w.saved_light_value(1, 1000, 64, 1000), 0);
        assert_eq!(w.saved_light_value(0, 0, 200, 0), 15);
        assert_eq!(w.saved_light_value(1, 0, 200, 0), 0);
        assert_eq!(w.saved_light_value(0, 0, -5, 0), 0);
        assert_eq!(w.block_light_value(1000, 64, 1000), 0);
    }

    #[test]
    fn test_colliding_boxes_floor() {
        let w = world_with_floor();
        // Box straddling the floor top picks up the stone cells beneath.
        let mask = AxisAlignedBB::get_bounding_box(3.2, 63.5, 4.2, 3.8, 64.5, 4.8);
        let boxes = w.colliding_boxes(&mask);
        assert!(!boxes.is_empty());
        assert!(boxes.iter().all(|b| b.max_y <= 64.0 + 1e-9));
        // Air mask collects nothing.
        let air = AxisAlignedBB::get_bounding_box(3.2, 70.0, 4.2, 3.8, 71.0, 4.8);
        assert!(w.colliding_boxes(&air).is_empty());
    }

    #[test]
    fn test_closest_player_strict_range() {
        let mut w = World::new(7);
        let a = add_player(&mut w, "a", 0.0, 64.0, 0.0);
        let _b = add_player(&mut w, "b", 100.0, 64.0, 0.0);
        assert_eq!(w.closest_player(3.0, 64.0, 4.0, 24.0), Some(a));
        assert_eq!(w.closest_player(3.0, 64.0, 4.0, 4.0), None);
        // Boundary is exclusive like C++ (strict <).
        assert_eq!(w.closest_player(24.0, 64.0, 0.0, 24.0), None);
    }

    fn add_item(w: &mut World, x: f64, y: f64, z: f64) -> EntityId {
        w.spawn_item_entity(35, 1, 0, x, y, z)
    }

    #[test]
    fn test_move_body_lands_on_floor() {
        let mut w = world_with_floor();
        let id = add_item(&mut w, 3.5, 70.0, 4.5);
        // Fall until resting: big steps converge on y=64 top face.
        for _ in 0..40 {
            w.move_body(id, 0.0, -3.0, 0.0);
        }
        let b = w.entities.get(id).unwrap().body().clone();
        assert!(b.on_ground);
        assert!((b.pos[1] - 64.125).abs() < 1e-6);
    }

    #[test]
    fn test_tick_item_falls_and_ages_out() {
        let mut w = world_with_floor();
        let id = add_item(&mut w, 3.5, 66.0, 4.5);
        for _ in 0..60 {
            w.tick_item(id);
            if w.entities.get(id).unwrap().body().on_ground {
                break;
            }
        }
        assert!(w.entities.get(id).unwrap().body().on_ground);
        // Age-out kills at 6000 regardless of rest.
        if let Some(crate::entity_table::Entity::Item(e)) = w.entities.get_mut(id) {
            e.age = 5999;
        }
        w.tick_item(id);
        assert!(w.entities.get(id).unwrap().body().dead);
    }

    #[test]
    fn test_tick_falling_places_on_landing() {
        use crate::entity_table::{Body, FallingEnt};
        let mut w = world_with_floor();
        let id = w.entities.alloc_id();
        let mut b = Body::new(id, 0.98, 0.98, 0.49);
        b.set_position(3.5, 70.0, 4.5);
        w.entities.insert(crate::entity_table::Entity::Falling(FallingEnt {
            body: b,
            block_id: 12,
            fall_time: 0,
        }));
        for _ in 0..60 {
            w.tick_falling(id);
            if w.entities.get(id).unwrap().body().dead {
                break;
            }
        }
        assert!(w.entities.get(id).unwrap().body().dead);
        // Landed on the floor top (y=64) and placed sand there.
        assert_eq!(w.get_block_id(3, 64, 4), 12);
    }

    #[test]
    fn test_is_replaceable_table() {
        assert!(is_replaceable(37));
        assert!(is_replaceable(83));
        assert!(is_replaceable(50));
        assert!(!is_replaceable(39));
        assert!(!is_replaceable(12));
        assert!(!is_replaceable(0));
    }

    fn add_zombie(w: &mut World, x: f64, y: f64, z: f64) -> EntityId {
    use crate::entity_table::{LivingBody, MobEnt};
        let id = w.entities.alloc_id();
        let mut l = LivingBody::new(id, 0.6, 1.9, 0.0);
        l.body.set_position(x, y, z);
        w.entities.insert(crate::entity_table::Entity::Mob(MobEnt {
            living: l,
            kind: crate::entity_table::MobKind::Zombie,
            target: None,
            attack_cooldown: 0,
            target_timer: 0,
            burn_ticks: 0,
            age: 0,
            path: Vec::new(),
            path_index: 0,
            swell_time: 0,
            swell_dir: -1,
        }));
        id
    }

    #[test]
    fn test_attack_kills_and_drops() {
        let mut w = world_with_floor();
        let id = add_zombie(&mut w, 3.5, 65.0, 4.5);
        let before = w.entities.len();
        w.attack_living(id, 100, None);
        assert!(w.entities.get(id).unwrap().body().dead);
        // Zombie drops 0..2 feathers: all spawns are items.
        let after = w.entities.len();
        assert!(after >= before && after <= before + 2);
        for oid in w.entities.alive_ids() {
            if oid == id {
                continue;
            }
            assert!(matches!(
                w.entities.get(oid).unwrap(),
                crate::entity_table::Entity::Item(_)
            ));
        }
    }

    #[test]
    fn test_attack_resist_and_knockback() {
        let mut w = world_with_floor();
        let id = add_zombie(&mut w, 3.5, 65.0, 4.5);
        let atk = add_zombie(&mut w, 8.5, 65.0, 4.5);
        w.attack_living(id, 6, Some(atk));
        let l = match w.entities.get(id).unwrap() {
            crate::entity_table::Entity::Mob(m) => m.living.clone(),
            _ => unreachable!(),
        };
        assert_eq!((l.health, l.last_damage, l.hurt_time), (14, 6, 10));
        // Knocked away from the attacker (attacker east => push west).
        assert!(l.body.motion[0] < 0.0);
    }

    #[test]
    fn test_tick_living_drowns() {
        let mut w = world_with_floor();
        // Water column instead of air above the floor.
        for y in 64..68 {
            w.set_block_id(3, y, 4, 8);
        }
        let id = add_zombie(&mut w, 3.5, 65.0, 4.5);
        // Force air to the edge: one tick must drown for 2 damage.
        if let Some(crate::entity_table::Entity::Mob(m)) = w.entities.get_mut(id) {
            m.living.body.air = -19;
        }
        let hp_before = match w.entities.get(id).unwrap() {
            crate::entity_table::Entity::Mob(m) => m.living.health,
            _ => unreachable!(),
        };
        w.tick_living(id);
        let hp_after = match w.entities.get(id).unwrap() {
            crate::entity_table::Entity::Mob(m) => m.living.health,
            _ => unreachable!(),
        };
        assert_eq!(hp_before - hp_after, 2);
    }

    fn add_boat(w: &mut World, x: f64, y: f64, z: f64) -> EntityId {
        use crate::entity_table::BoatEnt;
        let id = w.entities.alloc_id();
        let mut b = Body::new(id, 1.5, 0.6, 0.3);
        b.set_position(x, y, z);
        w.entities.insert(crate::entity_table::Entity::Boat(BoatEnt {
            body: b,
            time_since_hit: 0,
            damage_taken: 0,
            forward_dir: 1,
        }));
        id
    }

    fn add_water_pool(w: &mut World) {
        // 4x2x4 pool at y 63..64 inside the floor chunk.
        for x in 6..10 {
            for z in 6..10 {
                w.set_block_id(x, 62, z, 1);
                w.set_block_id(x, 63, z, 8);
                w.set_block_id(x, 64, z, 8);
            }
        }
    }

    #[test]
    fn test_boat_floats_in_water() {
        let mut w = world_with_floor();
        add_water_pool(&mut w);
        let id = add_boat(&mut w, 8.0, 64.0, 8.0);
        w.tick_boat(id);
        let b = w.entities.get(id).unwrap().body().clone();
        assert!(!b.dead);
        assert!(b.motion[1] > 0.0);
    }

    #[test]
    fn test_boat_crash_drops_and_dies() {
        let mut w = world_with_floor();
        // Wall column east of the boat.
        for y in 64..67 {
            w.set_block_id(10, y, 8, 1);
        }
        // Motion clamps to ±0.4 before moving: start close enough to hit.
        let id = add_boat(&mut w, 9.0, 65.0, 8.0);
        if let Some(crate::entity_table::Entity::Boat(b)) = w.entities.get_mut(id) {
            b.body.motion = [3.0, 0.0, 3.0];
        }
        let before = w.entities.len();
        w.tick_boat(id);
        assert!(w.entities.get(id).unwrap().body().dead);
        // 3 planks + 2 sticks spawned.
        assert_eq!(w.entities.len(), before + 5);
    }

    #[test]
    fn test_boat_damage_breaks_past_40() {
        let mut w = world_with_floor();
        let id = add_boat(&mut w, 8.0, 65.0, 8.0);
        assert!(!w.damage_boat(id, 3));
        let b = w.entities.get(id).unwrap();
        let (dir, time) = match b {
            crate::entity_table::Entity::Boat(b) => (b.forward_dir, b.time_since_hit),
            _ => unreachable!(),
        };
        assert_eq!((dir, time), (-1, 10));
        assert!(w.damage_boat(id, 5));
        assert!(w.entities.get(id).unwrap().body().dead);
    }

    use crate::entity_table::{AnimalEnt, AnimalKind, MobKind};

    fn add_mob(w: &mut World, kind: MobKind, x: f64, y: f64, z: f64) -> EntityId {
        let id = w.entities.alloc_id();
        let mut m = MobEnt::new(id, kind);
        m.living.body.set_position(x, y, z);
        w.entities.insert(crate::entity_table::Entity::Mob(m));
        id
    }

    fn add_animal(w: &mut World, kind: AnimalKind, x: f64, y: f64, z: f64) -> EntityId {
        let id = w.entities.alloc_id();
        let mut a = AnimalEnt::new(id, kind);
        a.living.body.set_position(x, y, z);
        w.entities.insert(crate::entity_table::Entity::Animal(a));
        id
    }

    fn add_floor_chunk(w: &mut World, cx: i32, cz: i32) {
        let mut c = Chunk::new(cx, cz);
        for x in 0..16 {
            for z in 0..16 {
                c.set_block_id(x, 63, z, 1);
            }
        }
        c.generate_height_map();
        w.insert_chunk(c);
    }

    fn set_sky(w: &mut World, x: i32, y: i32, z: i32, v: u8) {
        if let Some(c) = w.chunks.get_mut(&(x.div_euclid(16), z.div_euclid(16))) {
            c.set_light_value(0, x.rem_euclid(16), y, z.rem_euclid(16), v);
        }
    }

    fn mob_health(w: &World, id: EntityId) -> i16 {
        match w.entities.get(id).unwrap() {
            crate::entity_table::Entity::Mob(m) => m.living.health,
            _ => unreachable!(),
        }
    }

    #[test]
    fn test_dayclock_and_sky_queries() {
        let mut w = world_with_floor();
        w.time = 0;
        assert!(w.is_daytime());
        w.time = 11999;
        assert!(w.is_daytime());
        w.time = 12000;
        assert!(!w.is_daytime());
        w.time = 18000;
        assert!(!w.is_daytime());
        // Floor top (y=64) sees sky, the stone itself does not.
        assert!(w.can_see_sky(3, 64, 4));
        assert!(!w.can_see_sky(3, 63, 4));
        assert!(!w.can_see_sky(1000, 64, 1000));
        assert!(w.can_see_sky(3, 200, 3));
        assert!(!w.can_see_sky(3, -1, 4));
        // Fresh chunks are dark; setting skylight lifts brightness to full.
        assert_eq!(w.brightness(3, 64, 4), 0.0);
        set_sky(&mut w, 3, 64, 4, 15);
        assert_eq!(w.brightness(3, 64, 4), 1.0);
    }

    #[test]
    fn test_mob_acquires_and_chases_player() {
        let mut w = world_with_floor();
        let player = add_player(&mut w, "steve", 10.5, 64.0, 4.5);
        let zombie = add_mob(&mut w, MobKind::Zombie, 3.5, 64.0, 4.5);
        w.tick_mob(zombie);
        assert_eq!(
            match w.entities.get(zombie).unwrap() {
                crate::entity_table::Entity::Mob(m) => m.target,
                _ => unreachable!(),
            },
            Some(player)
        );
        for _ in 0..40 {
            w.tick_mob(zombie);
        }
        let x = w.entities.get(zombie).unwrap().body().pos[0];
        assert!(x > 3.5, "zombie should walk east toward the player, x={x}");
    }

    #[test]
    fn test_mob_wanders_without_target() {
        let mut w = world_with_floor();
        let zombie = add_mob(&mut w, MobKind::Zombie, 8.5, 64.0, 8.5);
        for _ in 0..300 {
            w.tick_mob(zombie);
        }
        let p = w.entities.get(zombie).unwrap().body().pos;
        let moved = (p[0] - 8.5).abs() + (p[2] - 8.5).abs();
        assert!(moved > 0.3, "targetless zombie should wander, moved={moved}");
        assert_eq!(mob_health(&w, zombie), 20);
    }

    #[test]
    fn test_mob_burn_schedule_and_small_fire_rule() {
        let mut w = world_with_floor();
        let zombie = add_mob(&mut w, MobKind::Zombie, 3.5, 64.0, 4.5);
        if let Some(crate::entity_table::Entity::Mob(m)) = w.entities.get_mut(zombie) {
            m.burn_ticks = 21;
        }
        w.tick_mob(zombie);
        let (burn, fire) = match w.entities.get(zombie).unwrap() {
            crate::entity_table::Entity::Mob(m) => (m.burn_ticks, m.living.body.fire),
            _ => unreachable!(),
        };
        assert_eq!((burn, fire), (20, 20));
        assert_eq!(mob_health(&w, zombie), 20); // 21 % 20 != 0: no hit yet
        w.tick_mob(zombie);
        assert_eq!(mob_health(&w, zombie), 19); // 20 % 20 == 0: one burn damage
        // Small fires go out once the burn ends.
        if let Some(crate::entity_table::Entity::Mob(m)) = w.entities.get_mut(zombie) {
            m.burn_ticks = 0;
            m.living.body.fire = 10;
        }
        w.tick_mob(zombie);
        assert_eq!(
            match w.entities.get(zombie).unwrap() {
                crate::entity_table::Entity::Mob(m) => m.living.body.fire,
                _ => unreachable!(),
            },
            0
        );
    }

    #[test]
    fn test_zombie_ignites_in_daylight() {
        let mut w = world_with_floor();
        w.time = 6000; // noon
        set_sky(&mut w, 3, 64, 4, 15);
        let zombie = add_mob(&mut w, MobKind::Zombie, 3.5, 64.0, 4.5);
        for _ in 0..500 {
            // Pin to the lit column: untethered it wanders off into the
            // dark, which is correct AI but a useless ignition test.
            if let Some(crate::entity_table::Entity::Mob(m)) = w.entities.get_mut(zombie) {
                m.living.body.set_position(3.5, 64.0, 4.5);
            }
            w.tick_mob(zombie);
            let burn = match w.entities.get(zombie).unwrap() {
                crate::entity_table::Entity::Mob(m) => m.burn_ticks,
                _ => unreachable!(),
            };
            if burn > 0 {
                return;
            }
        }
        panic!("daylit zombie should have caught fire within 500 ticks");
    }

    #[test]
    fn test_spider_hunts_only_in_dark() {
        let mut w = world_with_floor();
        let _player = add_player(&mut w, "steve", 6.5, 64.0, 4.5);
        let spider = add_mob(&mut w, MobKind::Spider, 3.5, 64.0, 4.5);
        set_sky(&mut w, 3, 64, 4, 15);
        w.tick_mob(spider);
        assert_eq!(
            match w.entities.get(spider).unwrap() {
                crate::entity_table::Entity::Mob(m) => m.target,
                _ => unreachable!(),
            },
            None
        );
        // Night falls: the next acquire (5-tick refresh) locks on.
        set_sky(&mut w, 3, 64, 4, 0);
        for _ in 0..6 {
            w.tick_mob(spider);
        }
        assert!(
            match w.entities.get(spider).unwrap() {
                crate::entity_table::Entity::Mob(m) => m.target,
                _ => unreachable!(),
            }
            .is_some()
        );
    }

    #[test]
    fn test_mob_takes_fall_damage() {
        let mut w = world_with_floor();
        let zombie = add_mob(&mut w, MobKind::Zombie, 8.5, 75.0, 8.5);
        for _ in 0..200 {
            w.tick_mob(zombie);
            if w.entities.get(zombie).unwrap().body().on_ground {
                break;
            }
        }
        assert!(w.entities.get(zombie).unwrap().body().on_ground);
        assert!(mob_health(&w, zombie) < 20);
    }

    #[test]
    fn test_chicken_lays_eggs_and_ignores_fall() {
        let mut w = world_with_floor();
        let chicken = add_animal(&mut w, AnimalKind::Chicken, 8.5, 70.0, 8.5);
        if let Some(crate::entity_table::Entity::Animal(a)) = w.entities.get_mut(chicken) {
            a.egg_timer = 2;
        }
        for _ in 0..200 {
            w.tick_animal(chicken);
            if w.entities.get(chicken).unwrap().body().on_ground {
                break;
            }
        }
        assert!(w.entities.get(chicken).unwrap().body().on_ground);
        // Fell ~6 blocks: a zombie would be hurt, the chicken is unharmed
        // (chicken max HP is 4 per Java EntityChicken.java:16).
        assert_eq!(
            match w.entities.get(chicken).unwrap() {
                crate::entity_table::Entity::Animal(a) => a.living.health,
                _ => unreachable!(),
            },
            4
        );
        // ...and laid an egg somewhere along the way.
        let eggs = w
            .entities
            .alive_ids()
            .into_iter()
            .filter(|oid| {
                matches!(
                    w.entities.get(*oid).unwrap(),
                    crate::entity_table::Entity::Item(e) if e.item_id == 344
                )
            })
            .count();
        assert!(eggs >= 1);
        assert!(
            match w.entities.get(chicken).unwrap() {
                crate::entity_table::Entity::Animal(a) => a.egg_timer,
                _ => unreachable!(),
            } >= 6000
        );
    }

    #[test]
    fn test_push_neighbors_shoves() {
        let mut w = world_with_floor();
        let z1 = add_mob(&mut w, MobKind::Zombie, 3.5, 64.0, 4.5);
        let z2 = add_mob(&mut w, MobKind::Zombie, 3.7, 64.0, 4.5);
        w.tick_mob(z1);
        let m2 = w.entities.get(z2).unwrap().body().motion;
        assert!(
            m2[0] != 0.0 || m2[2] != 0.0,
            "overlapping neighbor should be shoved, motion={m2:?}"
        );
    }



    #[test]
    fn test_native_path_points() {
        let mut w = world_with_floor();
        let zombie = add_mob(&mut w, MobKind::Zombie, 3.5, 64.0, 4.5);
        let pts = w.path_block_points(zombie, [10, 64, 4], 16.0);
        assert!(!pts.is_empty());
        // Path to our own block is empty (start == end, like nullptr).
        assert!(w.path_block_points(zombie, [3, 64, 4], 16.0).is_empty());
    }

    #[test]
    fn test_tick_world_advances_and_dispatches() {
        let mut w = world_with_floor();
        let item = w.spawn_item_entity(3, 1, 0, 3.5, 66.0, 4.5);
        w.tick_world();
        assert_eq!(w.time, 1);
        // The loose item ticked (gravity pulled it down).
        assert!(w.entities.get(item).unwrap().body().pos[1] < 66.0);
    }

    #[test]
    fn test_tick_world_pickup_and_purge() {
        let mut w = world_with_floor();
        let player = add_player(&mut w, "steve", 3.5, 64.0, 4.5);
        let item = w.spawn_item_entity(3, 5, 0, 3.5, 64.2, 4.5);
        if let Some(crate::entity_table::Entity::Item(e)) = w.entities.get_mut(item) {
            e.pickup_delay = 0;
        }
        let zombie = add_mob(&mut w, MobKind::Zombie, 8.5, 64.0, 4.5);
        w.attack_living(zombie, 100, None);
        assert!(w.entities.get(zombie).unwrap().body().dead);
        w.tick_world();
        // Dirt merged into the held slot, the item row is gone...
        assert_eq!(
            w.player_held(player).map(|s| (s.item_id, s.stack_size)),
            Some((3, 5))
        );
        let dirt: i32 = match w.entities.get(player).unwrap() {
            crate::entity_table::Entity::Player(p) => {
                p.inventory.main.iter().filter_map(|s| *s).map(|s| s.stack_size).sum()
            }
            _ => unreachable!(),
        };
        assert_eq!(dirt, 5);
        // ...and the corpse row was purged.
        assert!(w.entities.get(zombie).is_none());
        assert!(w.entities.get(item).is_none());
        // The full take was recorded for the server tick's collect fan-out.
        assert_eq!(w.item_pickups, vec![(item, player)]);
    }

    #[test]
    fn test_hand_hit_knocks_pig_back() {
        // Hand hit from the east must shove the pig west immediately and
        // displace it on the next tick (knockback pipeline + heading).
        let mut w = world_with_floor();
        let player = add_player(&mut w, "steve", 5.5, 64.0, 4.5);
        let pig = add_animal(&mut w, AnimalKind::Pig, 3.5, 64.0, 4.5);
        w.attack_living(pig, 1, Some(player));
        let motion = match w.entities.get(pig).unwrap() {
            crate::entity_table::Entity::Animal(a) => a.living.body.motion,
            _ => unreachable!(),
        };
        assert!(motion[0] < -0.3, "westward shove, got {motion:?}");
        assert_eq!(motion[1], 0.4);
        w.tick_world();
        let after = match w.entities.get(pig).unwrap() {
            crate::entity_table::Entity::Animal(a) => a.living.body.pos[0],
            _ => unreachable!(),
        };
        assert!(after < 3.5 - 0.15, "pig displaced west, now at {after}");
    }

    #[test]
    fn test_native_drop_ids_spot_checks() {
        // Vanilla idDropped/quantityDropped spot checks (Java Block*).
        assert_eq!(World::native_drop_ids(63), (323, 1, 0));
        assert_eq!(World::native_drop_ids(68), (323, 1, 0));
        assert_eq!(World::native_drop_ids(62), (61, 1, 0));
        assert_eq!(World::native_drop_ids(82), (337, 4, 0));
        assert_eq!(World::native_drop_ids(43), (44, 1, 0));
        assert_eq!(World::native_drop_ids(1), (4, 1, 0));
        assert_eq!(World::native_drop_ids(16), (263, 1, 0));
        assert_eq!(World::native_drop_ids(56), (264, 1, 0));
        assert_eq!(World::native_drop_ids(39), (39, 1, 0));
        assert_eq!(World::native_drop_ids(40), (40, 1, 0));
        for bid in [20, 47, 52, 79] {
            assert_eq!(World::native_drop_ids(bid).1, 0, "block {bid} drops nothing");
        }
        // Snow harvest: layer -> 1 snowball, block -> 4 snowballs.
        assert_eq!(World::native_drop_ids(78), (332, 1, 0));
        assert_eq!(World::native_drop_ids(80), (332, 4, 0));
    }

    #[test]
    fn test_rolled_drops_doors_redstone_gravel_leaves() {
        let mut w = world_with_floor();
        // Doors: upper half nothing, lower wood 324 / iron 330.
        assert_eq!(w.rolled_drop_ids(64, 8), (0, 0));
        assert_eq!(w.rolled_drop_ids(64, 0), (324, 1));
        assert_eq!(w.rolled_drop_ids(71, 0), (330, 1));
        // Redstone dust comes 4-5.
        for _ in 0..20 {
            let (d, q) = w.rolled_drop_ids(73, 0);
            assert_eq!(d, 331);
            assert!((4..=5).contains(&q), "{q}");
        }
        // Gravel flints or stays gravel; leaves sapling or nothing.
        for _ in 0..50 {
            assert!(matches!(w.rolled_drop_ids(13, 0), (318, 1) | (13, 1)));
            assert!(matches!(w.rolled_drop_ids(18, 0), (6, 1) | (0, 0)));
        }
    }

    #[test]
    fn test_cactus_contact_hurts() {
        // Standing in cactus deals 1 through the pipeline (BlockCactus).
        let mut w = world_with_floor();
        let player = add_player(&mut w, "steve", 3.5, 64.0, 4.5);
        w.set_block_id(3, 64, 4, 81);
        w.move_body(player, 0.0, 0.0, 0.0);
        let hp = match w.entities.get(player).unwrap() {
            crate::entity_table::Entity::Player(p) => p.living.health,
            _ => unreachable!(),
        };
        assert_eq!(hp, 19);
    }

    fn furnace_tile_with(input: (i32, i32), fuel: (i32, i32)) -> TileData {
        use crate::inventory::FfiItemStack;
        let mut s = crate::tile_entity_furnace::furnace_create();
        s.slots[0] = FfiItemStack {
            stack_size: input.1,
            animations_to_go: 0,
            item_id: input.0,
            item_damage: 0,
        };
        s.slots[1] = FfiItemStack {
            stack_size: fuel.1,
            animations_to_go: 0,
            item_id: fuel.0,
            item_damage: 0,
        };
        TileData::Furnace(s)
    }

    #[test]
    fn test_tick_furnaces_swaps_idle_to_lit() {
        let mut w = world_with_floor();
        w.set_block_id(2, 64, 2, 61);
        w.set_block_meta(2, 64, 2, 3);
        w.tiles.insert((2, 64, 2), furnace_tile_with((4, 1), (5, 1)));
        w.tick_furnaces();
        // Lit, facing (meta 3) preserved, update queued for broadcast.
        assert_eq!(w.get_block_id(2, 64, 2), 62);
        assert_eq!(w.get_block_meta(2, 64, 2), 3);
        assert_eq!(w.furnace_updates, vec![[2, 64, 2]]);
        // Tile row survives the swap (the C++ no-notify set).
        assert!(matches!(w.tiles.get(&(2, 64, 2)), Some(TileData::Furnace(_))));
        // Steady burn: no further swap, no new update.
        w.tick_furnaces();
        assert_eq!(w.get_block_id(2, 64, 2), 62);
        assert_eq!(w.furnace_updates.len(), 1);
    }

    #[test]
    fn test_tick_furnaces_swaps_lit_to_idle_when_fuel_runs_out() {
        let mut w = world_with_floor();
        w.set_block_id(2, 64, 2, 62);
        w.tiles.insert((2, 64, 2), furnace_tile_with((4, 1), (280, 1)));
        // Light it (stick: 100 ticks of burn).
        w.tick_furnaces();
        assert_eq!(w.furnace_updates.len(), 0); // already lit: no flip
        for _ in 0..200 {
            w.tick_furnaces();
        }
        // Burnt out mid-run: back to idle, one update queued.
        assert_eq!(w.get_block_id(2, 64, 2), 61);
        assert_eq!(w.furnace_updates, vec![[2, 64, 2]]);
    }

    #[test]
    fn test_tick_furnaces_skips_chest_and_sign_tiles() {
        let mut w = world_with_floor();
        w.set_block_id(2, 64, 2, 54);
        w.tiles.insert(
            (2, 64, 2),
            TileData::Chest(crate::tile_entity_chest::chest_create()),
        );
        w.tick_furnaces();
        assert_eq!(w.get_block_id(2, 64, 2), 54);
        assert!(w.furnace_updates.is_empty());
    }

    #[test]
    fn test_tick_world_runs_furnaces() {
        let mut w = world_with_floor();
        w.set_block_id(2, 64, 2, 61);
        w.tiles.insert((2, 64, 2), furnace_tile_with((12, 1), (263, 1)));
        w.tick_world();
        assert_eq!(w.get_block_id(2, 64, 2), 62);
        assert_eq!(w.furnace_updates, vec![[2, 64, 2]]);
    }

    #[test]
    fn test_tick_player_decays_respawn() {
        let mut w = world_with_floor();
        let player = add_player(&mut w, "steve", 3.5, 64.0, 4.5);
        if let Some(crate::entity_table::Entity::Player(p)) = w.entities.get_mut(player) {
            p.respawn_ticks = 5;
        }
        w.tick_player(player);
        assert_eq!(
            match w.entities.get(player).unwrap() {
                crate::entity_table::Entity::Player(p) => p.respawn_ticks,
                _ => unreachable!(),
            },
            4
        );
    }

    #[test]
    fn test_spawn_hostile_mobs() {
        let mut w = world_with_floor();
        let _p = add_player(&mut w, "steve", 8.5, 64.0, 8.5);
        // Far, dark floor chunks: inside the 24-block exclusion near the
        // player nothing may spawn, so the pens sit out at x 64+.
        for cx in 4..8 {
            for cz in -2..3 {
                add_floor_chunk(&mut w, cx, cz);
            }
        }
        let mut spawned = 0;
        for _ in 0..600 {
            spawned += w.spawn_hostile_mobs();
            if spawned > 0 {
                break;
            }
        }
        assert!(spawned > 0, "dark pens should yield hostile spawns");
        for oid in w.entities.alive_ids() {
            if !matches!(w.entities.get(oid).unwrap(), crate::entity_table::Entity::Player(_)) {
                assert!(matches!(
                    w.entities.get(oid).unwrap(),
                    crate::entity_table::Entity::Mob(_)
                ));
            }
        }
    }

    #[test]
    fn test_spawn_passive_mobs() {
        let mut w = world_with_floor();
        let _p = add_player(&mut w, "steve", 8.5, 64.0, 8.5);
        for cx in 4..8 {
            for cz in -2..3 {
                add_floor_chunk(&mut w, cx, cz);
            }
        }
        // Lit grass pens for the herds. Origins must sit exactly one
        // above the grass (y is drawn uniform over 128), so allow a wide
        // pass budget; the seeded stream keeps it deterministic.
        for x in 64..96 {
            for z in -16..16 {
                w.set_block_id(x, 63, z, 2);
                set_sky(&mut w, x, 64, z, 15);
                set_sky(&mut w, x, 65, z, 15);
            }
        }
        let mut spawned = 0;
        for _ in 0..3000 {
            spawned += w.spawn_passive_mobs();
            if spawned > 0 {
                break;
            }
        }
        assert!(spawned > 0, "lit grass pens should yield passive spawns");
    }

    #[test]
    fn test_sand_falls_on_schedule() {
        let mut w = world_with_floor();
        // Schedules need loaded surroundings (radius 8 like vanilla).
        for cx in -1..=0 {
            for cz in -1..=0 {
                if cx == 0 && cz == 0 {
                    continue;
                }
                add_floor_chunk(&mut w, cx, cz);
            }
        }
        w.set_block_id(3, 66, 4, 12);
        w.schedule_block_update(3, 66, 4, 12, 1);
        w.tick_world();
        // The tick fired: sand left and a falling row took over.
        assert_eq!(w.get_block_id(3, 66, 4), 0);
        assert!(w.entities.alive_ids().into_iter().any(|oid| matches!(
            w.entities.get(oid).unwrap(),
            crate::entity_table::Entity::Falling(f) if f.block_id == 12
        )));
    }

    #[test]
    fn test_scheduled_queue_gates_and_stales() {
        let mut w = world_with_floor();
        for cx in -1..=0 {
            for cz in -1..=0 {
                if cx == 0 && cz == 0 {
                    continue;
                }
                add_floor_chunk(&mut w, cx, cz);
            }
        }
        w.set_block_id(3, 66, 4, 12);
        // Future entry waits.
        w.schedule_block_update(3, 66, 4, 12, 5);
        assert_eq!(w.scheduled.len(), 1);
        w.tick_world();
        assert_eq!(w.scheduled.len(), 1);
        assert_eq!(w.get_block_id(3, 66, 4), 12);
        // Stale entry (wrong id) is dropped without effect; the future
        // entry from above is still queued.
        w.schedule_block_update(5, 66, 5, 12, 0);
        w.tick_world();
        assert_eq!(w.scheduled.len(), 1);
        assert_eq!(w.get_block_id(5, 66, 5), 0);
        // Duplicate keys keep the first entry like the C++ set.
        w.schedule_block_update(7, 66, 7, 13, 2);
        w.schedule_block_update(7, 66, 7, 12, 2);
        let dups: Vec<u8> = w
            .scheduled
            .iter()
            .filter(|((_, x, y, z), _)| (*x, *y, *z) == (7, 66, 7))
            .map(|(_, v)| *v)
            .collect();
        assert_eq!(dups, vec![13]);
    }



    #[test]
    fn test_torch_pops_when_dug() {
        let mut w = world_with_floor();
        w.set_block_id(3, 64, 4, 50);
        w.set_block_meta(3, 64, 4, 5); // floor mount, like placement sets
        w.apply_set_notify(3, 63, 4, 0);
        assert_eq!(w.get_block_id(3, 64, 4), 0);
        assert!(w.entities.alive_ids().into_iter().any(|oid| matches!(
            w.entities.get(oid).unwrap(),
            crate::entity_table::Entity::Item(e) if e.item_id == 50
        )));
    }

    #[test]
    fn test_sign_pops_without_support() {
        let mut w = world_with_floor();
        // Wall sign facing +z support.
        w.set_block_id(3, 65, 5, 1);
        w.set_block_id(3, 65, 4, 68);
        w.set_block_meta(3, 65, 4, 2);
        w.apply_set_notify(3, 65, 5, 0);
        assert_eq!(w.get_block_id(3, 65, 4), 0);
        assert!(w.entities.alive_ids().into_iter().any(|oid| matches!(
            w.entities.get(oid).unwrap(),
            crate::entity_table::Entity::Item(e) if e.item_id == 323
        )));
        // Supported post sign stays.
        w.set_block_id(5, 64, 5, 63);
        w.neighbor_changed(5, 64, 5);
        assert_eq!(w.get_block_id(5, 64, 5), 63);
    }

    #[test]
    fn test_flowers_pop_in_dark_random_ticks() {
        let mut w = world_with_floor();
        let _p = add_player(&mut w, "steve", 8.5, 64.0, 8.5);
        for x in 0..16 {
            for z in 0..16 {
                w.set_block_id(x, 64, z, 37);
            }
        }
        for _ in 0..300 {
            w.random_block_ticks();
        }
        let left: usize = (0..16)
            .flat_map(|x| (0..16).map(move |z| (x, z)))
            .filter(|(x, z)| w.get_block_id(*x, 64, *z) == 37)
            .count();
        assert!(left < 256, "dark flowers should pop under random ticks, left={left}");
    }

    #[test]
    fn test_sapling_grows_or_restores() {
        let mut w = world_with_floor();
        w.set_block_id(8, 63, 8, 3);
        for x in 0..16 {
            for z in 0..16 {
                for y in 64..80 {
                    set_sky(&mut w, x, y, z, 15);
                }
            }
        }
        let mut grew = 0;
        for seed in 0..20 {
            w.set_block_id(8, 64, 8, 6);
            w.grow_sapling(8, 64, 8, 6, seed);
            let logs: usize = (0..16)
                .flat_map(|x| (0..16).flat_map(move |z| (64..96).map(move |y| (x, y, z))))
                .filter(|(x, y, z)| w.get_block_id(*x, *y, *z) == 17)
                .count();
            if logs > 0 {
                grew += 1;
            } else {
                // Failed generation restores the sapling.
                assert_eq!(w.get_block_id(8, 64, 8), 6);
            }
            // Scrub the tree for the next seed.
            for x in 0..16 {
                for z in 0..16 {
                    for y in 64..96 {
                        let id = w.get_block_id(x, y, z);
                        if id == 17 || id == 18 {
                            w.set_block_id(x, y, z, 0);
                        }
                    }
                }
            }
        }
        assert!(grew > 0, "open lit saplings should grow trees");
    }

    #[test]
    fn test_unload_spills_and_recalls() {
        let mut w = world_with_floor();
        let _p = add_player(&mut w, "steve", 8.5, 64.0, 8.5);
        // Tighten the unload radius so entities can sit in an unloading
        // chunk yet stay inside the 128-block despawn range (vanilla kills
        // far mobs before unload would ever spill them).
        w.unload_radius = 5;
        add_floor_chunk(&mut w, 7, 0);
        let item = w.spawn_item_entity(3, 2, 0, 7.0 * 16.0 + 8.5, 65.0, 8.5);
        let zombie = add_mob(&mut w, MobKind::Zombie, 7.0 * 16.0 + 8.5, 65.0, 8.5);
        w.time = 99;
        w.tick_world();
        assert_eq!(w.time, 100);
        assert!(!w.has_chunk(7, 0));
        assert!(w.entities.get(item).is_none());
        assert!(w.entities.get(zombie).is_none());
        // Spawn chunks stay put.
        assert!(w.has_chunk(0, 0));
        assert!(w.recall_chunk(7, 0));
        assert!(w.has_chunk(7, 0));
        let items = w
            .entities
            .alive_ids()
            .into_iter()
            .filter(|oid| {
                matches!(
                    w.entities.get(*oid).unwrap(),
                    crate::entity_table::Entity::Item(e) if e.item_id == 3 && e.count == 2
                )
            })
            .count();
        let mobs = w
            .entities
            .alive_ids()
            .into_iter()
            .filter(|oid| {
                matches!(
                    w.entities.get(*oid).unwrap(),
                    crate::entity_table::Entity::Mob(m) if m.kind == MobKind::Zombie
                )
            })
            .count();
        assert_eq!((items, mobs), (1, 1));
    }

    #[test]
    fn test_ensure_chunk_builds_terrain() {
        let mut w = World::new(1234);
        assert!(!w.has_chunk(0, 0));
        w.ensure_chunk(0, 0);
        assert!(w.has_chunk(0, 0));
        let h = w.get_height_value(8, 8);
        assert!((1..127).contains(&h), "generated column should have terrain, h={h}");
        // Stone body under the surface.
        let mut stone = false;
        for y in 0..h {
            if w.get_block_id(8, y, 8) == 1 {
                stone = true;
                break;
            }
        }
        assert!(stone);
        assert!(w.chunks.get(&(0, 0)).unwrap().is_terrain_populated);
    }

    #[test]
    fn test_ensure_chunk_deterministic_and_idempotent() {
        let sample = |w: &mut World| -> Vec<u8> {
            w.ensure_chunk(3, -2);
            let mut v = Vec::new();
            for x in (0..16).step_by(3) {
                for z in (0..16).step_by(5) {
                    for y in (0..128).step_by(7) {
                        v.push(w.get_block_id(3 * 16 + x, y, -2 * 16 + z));
                    }
                }
            }
            v
        };
        let mut a = World::new(777);
        let va = sample(&mut a);
        let mut b = World::new(777);
        assert_eq!(sample(&mut b), va);
        // Second ensure leaves blocks untouched (no double decoration).
        assert_eq!(sample(&mut a), va);
    }

    #[test]
    fn test_ensure_area_populates_square() {
        let mut w = World::new(4242);
        w.ensure_area(0, 0, 1);
        for dx in -1..=1 {
            for dz in -1..=1 {
                assert!(w.has_chunk(dx, dz));
                assert!(w.chunks.get(&(dx, dz)).unwrap().is_terrain_populated);
            }
        }
    }

    fn player_health(w: &World, id: EntityId) -> i16 {
        match w.entities.get(id).unwrap() {
            crate::entity_table::Entity::Player(p) => p.living.health,
            _ => unreachable!(),
        }
    }

    #[test]
    fn test_ray_trace_clear() {
        let mut w = world_with_floor();
        // Open air reads visible.
        assert!(w.ray_trace_clear([3.5, 66.0, 4.5], [10.5, 66.0, 4.5]));
        // Stone wall blocks.
        for y in 64..68 {
            w.set_block_id(7, y, 4, 1);
        }
        assert!(!w.ray_trace_clear([3.5, 66.0, 4.5], [10.5, 66.0, 4.5]));
        // Fluids let sight through; flowers block (canCollideCheck is only
        // false for BlockFluid).
        w.set_block_id(7, 66, 4, 8);
        assert!(w.ray_trace_clear([3.5, 66.0, 4.5], [10.5, 66.0, 4.5]));
        w.set_block_id(7, 66, 4, 37);
        assert!(!w.ray_trace_clear([3.5, 66.0, 4.5], [10.5, 66.0, 4.5]));
        // NaN reads visible like the C++ early-out.
        assert!(w.ray_trace_clear([f64::NAN, 66.0, 4.5], [10.5, 66.0, 4.5]));
    }

    #[test]
    fn test_zombie_punches_player() {
        let mut w = world_with_floor();
        let player = add_player(&mut w, "steve", 4.5, 64.0, 4.5);
        let zombie = add_mob(&mut w, MobKind::Zombie, 3.5, 64.0, 4.5);
        w.tick_mob(zombie);
        assert_eq!(player_health(&w, player), 15);
        assert_eq!(
            match w.entities.get(zombie).unwrap() {
                crate::entity_table::Entity::Mob(m) => m.attack_cooldown,
                _ => unreachable!(),
            },
            20
        );
        // Cooldown gates the next punch.
        w.tick_mob(zombie);
        assert_eq!(player_health(&w, player), 15);
    }

    #[test]
    fn test_skeleton_looses_arrow() {
        let mut w = world_with_floor();
        let _player = add_player(&mut w, "steve", 10.5, 64.0, 4.5);
        let skel = add_mob(&mut w, MobKind::Skeleton, 3.5, 64.0, 4.5);
        w.tick_mob(skel);
        let arrows: Vec<EntityId> = w
            .entities
            .alive_ids()
            .into_iter()
            .filter(|oid| matches!(w.entities.get(*oid).unwrap(), crate::entity_table::Entity::Arrow(_)))
            .collect();
        assert_eq!(arrows.len(), 1);
        let (shooter, start) = match w.entities.get(arrows[0]).unwrap() {
            crate::entity_table::Entity::Arrow(a) => (a.shooter_id, a.body.pos),
            _ => unreachable!(),
        };
        assert_eq!(shooter, skel);
        for _ in 0..5 {
            w.tick_arrow(arrows[0]);
        }
        let end = w.entities.get(arrows[0]).unwrap().body().pos;
        assert!(end[0] > start[0], "arrow should fly east, {start:?} -> {end:?}");
    }

    #[test]
    fn test_spider_pounces_in_range() {
        let mut w = world_with_floor();
        let player = add_player(&mut w, "steve", 7.5, 64.0, 4.5);
        let spider = add_mob(&mut w, MobKind::Spider, 3.5, 64.0, 4.5);
        if let Some(crate::entity_table::Entity::Mob(m)) = w.entities.get_mut(spider) {
            m.living.body.on_ground = true;
        }
        let mut snap = w.creature_snapshot(spider).unwrap();
        for _ in 0..200 {
            w.mob_attack(spider, MobKind::Spider, player, 4.0, &mut snap);
            let my = w.entities.get(spider).unwrap().body().motion[1];
            if my == 0.4 {
                return; // pounce fired (rng(10) gate passed)
            }
        }
        panic!("spider should pounce from 4 blocks within 200 tries");
    }

    #[test]
    fn test_creeper_explodes_next_to_player() {
        // Deterministic ballistics: blast directly at d=1 with clear LOS.
        // Java formula: v=(1-1/3)=2/3 -> (v²+v)/2*8*3+1 = 14 damage.
        let mut w = world_with_floor();
        let player = add_player(&mut w, "steve", 4.5, 64.0, 4.5);
        let creeper = add_mob(&mut w, MobKind::Creeper, 3.5, 64.0, 4.5);
        w.blast(3.5, 64.0, 4.5, 3.0, Some(creeper));
        assert_eq!(player_health(&w, player), 6);
        // The fuse path still kills the creeper (fresh world, no knockback).
        let mut w2 = world_with_floor();
        let _p2 = add_player(&mut w2, "steve", 4.5, 64.0, 4.5);
        let c2 = add_mob(&mut w2, MobKind::Creeper, 3.5, 64.0, 4.5);
        for _ in 0..60 {
            // Pin: knockback from the eventual blast must not matter here;
            // the fuse needs dist<3 to light and <7 to hold.
            if let Some(e) = w2.entities.get_mut(c2) {
                if e.body().dead {
                    break;
                }
                e.body_mut().set_position(3.5, 64.0, 4.5);
            }
            w2.tick_mob(c2);
            if w2.entities.get(c2).unwrap().body().dead {
                break;
            }
        }
        assert!(w2.entities.get(c2).unwrap().body().dead);
    }

    #[test]
    fn test_creeper_blast_scatters_container() {
        let mut w = world_with_floor();
        // Stocked chest with the creeper inside its cell (d=0 destroys
        // with chance 1, no RNG involved).
        w.set_block_id(4, 64, 4, 54);
        let mut ch = crate::tile_entity_chest::chest_create();
        ch.slots[0] = stk(3, 7, 0);
        w.tiles.insert((4, 64, 4), TileData::Chest(ch));
        let creeper = add_mob(&mut w, MobKind::Creeper, 4.5, 64.0, 4.5);
        w.creeper_explode(creeper);
        // Chest block and tile row are gone, dirt scattered as items
        // (mirrors the C++ removal hook on the blast path).
        assert_eq!(w.get_block_id(4, 64, 4), 0);
        assert!(w.tiles.get(&(4, 64, 4)).is_none());
        assert!(w.entities.alive_ids().iter().any(|oid| matches!(
            w.entities.get(*oid),
            Some(crate::entity_table::Entity::Item(e)) if e.item_id == 3
        )));
    }

    #[test]
    fn test_arrow_sticks_in_wall_and_pops_out() {
        use crate::entity_table::{ArrowEnt, Body};
        let mut w = world_with_floor();
        for y in 64..67 {
            w.set_block_id(10, y, 8, 1);
        }
        let id = w.entities.alloc_id();
        let mut b = Body::new(id, 0.5, 0.5, 0.0);
        b.set_position(8.5, 65.0, 8.5);
        b.motion = [1.0, 0.0, 0.0];
        w.entities.insert(crate::entity_table::Entity::Arrow(ArrowEnt {
            body: b,
            in_ground: false,
            shake: 0,
            ticks_in_ground: 0,
            ticks_in_air: 0,
            shooter_id: -1,
            tile: [-1, -1, -1],
            in_tile: 0,
        }));
        w.tick_arrow(id);
        w.tick_arrow(id);
        let (stuck, tile, shake) = match w.entities.get(id).unwrap() {
            crate::entity_table::Entity::Arrow(a) => (a.in_ground, a.tile, a.shake),
            _ => unreachable!(),
        };
        assert!(stuck);
        // Boundary graze: the shared face clips y=64 first and strict `<`
        // keeps it, exactly like the C++ sweep order.
        assert_eq!((tile, shake), ([10, 64, 8], 7));
        // Removing the block pops the arrow back out.
        w.set_block_id(10, 64, 8, 0);
        w.tick_arrow(id);
        assert!(!matches!(
            w.entities.get(id).unwrap(),
            crate::entity_table::Entity::Arrow(a) if a.in_ground
        ));
    }

    #[test]
    fn test_sheep_shear_on_living_hit() {
        let mut w = world_with_floor();
        let sheep = add_animal(&mut w, AnimalKind::Sheep, 3.5, 64.0, 4.5);
        let zombie = add_mob(&mut w, MobKind::Zombie, 5.5, 64.0, 4.5);
        w.attack_living(sheep, 3, Some(zombie));
        assert!(matches!(
            w.entities.get(sheep).unwrap(),
            crate::entity_table::Entity::Animal(a) if a.sheared
        ));
        let wool = w
            .entities
            .alive_ids()
            .into_iter()
            .filter(|oid| {
                matches!(
                    w.entities.get(*oid).unwrap(),
                    crate::entity_table::Entity::Item(e) if e.item_id == 35
                )
            })
            .count();
        assert!((1..=3).contains(&wool), "shear drops 1..3 wool, got {wool}");
        // No living attacker, no shear.
        let sheep2 = add_animal(&mut w, AnimalKind::Sheep, 8.5, 64.0, 8.5);
        w.attack_living(sheep2, 3, None);
        assert!(matches!(
            w.entities.get(sheep2).unwrap(),
            crate::entity_table::Entity::Animal(a) if !a.sheared
        ));
    }

    #[test]
    fn test_player_damage_and_empty_death() {
        let mut w = world_with_floor();
        let player = add_player(&mut w, "steve", 3.5, 64.0, 4.5);
        let zombie = add_mob(&mut w, MobKind::Zombie, 8.5, 64.0, 4.5);
        w.attack_living(player, 5, Some(zombie));
        assert_eq!(player_health(&w, player), 15);
        let before = w.entities.len();
        w.attack_living(player, 100, Some(zombie));
        assert!(w.entities.get(player).unwrap().body().dead);
        // Empty inventory scatters nothing.
        assert_eq!(w.entities.len(), before);
    }

    fn stk(item_id: i32, count: i32, damage: i32) -> crate::inventory::FfiItemStack {
        crate::inventory::FfiItemStack {
            stack_size: count,
            animations_to_go: 0,
            item_id,
            item_damage: damage,
        }
    }

    fn set_slot(w: &mut World, id: EntityId, bank: u8, slot: usize, s: crate::inventory::FfiItemStack) {
        if let Some(crate::entity_table::Entity::Player(p)) = w.entities.get_mut(id) {
            let bank = match bank {
                0 => &mut p.inventory.main[..],
                1 => &mut p.inventory.armor[..],
                _ => &mut p.inventory.crafting[..],
            };
            bank[slot] = Some(s);
        }
    }

    fn player_items(w: &World) -> Vec<(i32, i32, i32)> {
        // (item_id, count, pickup_delay) of every live loose item.
        let mut out: Vec<(i32, i32, i32)> = w
            .entities
            .alive_ids()
            .into_iter()
            .filter_map(|oid| match w.entities.get(oid).unwrap() {
                crate::entity_table::Entity::Item(e) => Some((e.item_id, e.count, e.pickup_delay)),
                _ => None,
            })
            .collect();
        out.sort_unstable();
        out
    }

    #[test]
    fn test_player_death_scatters_inventory() {
        let mut w = world_with_floor();
        let player = add_player(&mut w, "steve", 3.5, 64.0, 4.5);
        let zombie = add_mob(&mut w, MobKind::Zombie, 8.5, 64.0, 4.5);
        set_slot(&mut w, player, 0, 0, stk(3, 10, 0)); // dirt x10
        set_slot(&mut w, player, 1, 0, stk(306, 1, 0)); // iron helm
        set_slot(&mut w, player, 2, 2, stk(280, 5, 0)); // sticks x5
        w.attack_living(player, 100, Some(zombie));
        assert!(w.entities.get(player).unwrap().body().dead);
        assert_eq!(player_items(&w), vec![(3, 10, 40), (280, 5, 40), (306, 1, 40)]);
        // Spawn height is feet + 0.5 with an upward toss.
        for oid in w.entities.alive_ids() {
            if let crate::entity_table::Entity::Item(e) = w.entities.get(oid).unwrap() {
                assert_eq!(e.body.pos[1], 64.5);
                assert!(e.body.motion[1] > 0.0);
            }
        }
        // All banks cleared.
        assert!(matches!(
            w.entities.get(player).unwrap(),
            crate::entity_table::Entity::Player(p)
                if p.inventory.main.iter().all(|s| s.is_none())
                    && p.inventory.armor.iter().all(|s| s.is_none())
                    && p.inventory.crafting.iter().all(|s| s.is_none())
        ));
    }

    #[test]
    fn test_player_respawn_immunity() {
        let mut w = world_with_floor();
        let player = add_player(&mut w, "steve", 3.5, 64.0, 4.5);
        let zombie = add_mob(&mut w, MobKind::Zombie, 4.5, 64.0, 4.5);
        if let Some(crate::entity_table::Entity::Player(p)) = w.entities.get_mut(player) {
            p.respawn_ticks = 10;
        }
        w.attack_living(player, 5, Some(zombie));
        assert_eq!(player_health(&w, player), 20);
    }

    #[test]
    fn test_player_armor_absorbs_and_wears() {
        let mut w = world_with_floor();
        let player = add_player(&mut w, "steve", 3.5, 64.0, 4.5);
        let zombie = add_mob(&mut w, MobKind::Zombie, 8.5, 64.0, 4.5);
        // Full iron: 3 + 8 + 6 + 3 = 20 points at full durability.
        set_slot(&mut w, player, 1, 0, stk(306, 1, 0));
        set_slot(&mut w, player, 1, 1, stk(307, 1, 0));
        set_slot(&mut w, player, 1, 2, stk(308, 1, 0));
        set_slot(&mut w, player, 1, 3, stk(309, 1, 0));
        w.attack_living(player, 10, Some(zombie));
        // scaled = 10 * (25 - 20) = 50 -> 2 damage through, carry 0.
        assert_eq!(player_health(&w, player), 18);
        assert!(matches!(
            w.entities.get(player).unwrap(),
            crate::entity_table::Entity::Player(p)
                if p.armor_carry == 0
                    && p.inventory.armor.iter().all(|s| s.map(|x| x.item_damage) == Some(10))
        ));
    }

    #[test]
    fn test_player_peaceful_ignores_mob_hit() {
        let mut w = world_with_floor();
        w.difficulty = 0;
        let player = add_player(&mut w, "steve", 3.5, 64.0, 4.5);
        let zombie = add_mob(&mut w, MobKind::Zombie, 4.5, 64.0, 4.5);
        w.attack_living(player, 5, Some(zombie));
        assert_eq!(player_health(&w, player), 20);
    }

    #[test]
    fn test_player_pickup_merges_and_overflows() {
        let mut w = world_with_floor();
        let player = add_player(&mut w, "steve", 3.5, 64.0, 4.5);
        assert_eq!(w.player_add_item(player, stk(3, 10, 0)), 0);
        assert_eq!(w.player_add_item(player, stk(3, 60, 0)), 0);
        let main: Vec<Option<(i32, i32)>> = match w.entities.get(player).unwrap() {
            crate::entity_table::Entity::Player(p) => {
                p.inventory.main.iter().take(3).map(|s| s.map(|x| (x.item_id, x.stack_size))).collect()
            }
            _ => unreachable!(),
        };
        assert_eq!(main, vec![Some((3, 64)), Some((3, 6)), None]);
        // Bad ids refuse.
        assert_eq!(w.player_add_item(player, stk(0, 5, 0)), 5);
        assert_eq!(w.player_add_item(player, stk(32000, 5, 0)), 5);
        assert_eq!(w.player_add_item(player, stk(3, 0, 0)), 0);
    }

    #[test]
    fn test_player_held_slot() {
        let mut w = world_with_floor();
        let player = add_player(&mut w, "steve", 3.5, 64.0, 4.5);
        assert_eq!(w.player_held(player), None);
        set_slot(&mut w, player, 0, 2, stk(5, 3, 0));
        if let Some(crate::entity_table::Entity::Player(p)) = w.entities.get_mut(player) {
            p.inventory.current = 2;
        }
        assert_eq!(w.player_held(player).map(|s| (s.item_id, s.stack_size)), Some((5, 3)));
        if let Some(crate::entity_table::Entity::Player(p)) = w.entities.get_mut(player) {
            p.inventory.current = 99;
        }
        assert_eq!(w.player_held(player), None);
    }
}

impl World {
    fn living_eye_height(e: &Entity) -> f64 {
        match e {
            Entity::Player(_) => 1.62,
            _ => e.body().height as f64 * 0.85,
        }
    }

    /// Crate-visible eye height for sessions.
    pub(crate) fn living_eye_height_pub(e: &Entity) -> f64 {
        Self::living_eye_height(e)
    }

    /// Crate-visible chunk dirty flag for tile updates.
    pub(crate) fn mark_chunk_modified(&mut self, cx: i32, cz: i32) {
        if let Some(c) = self.chunks.get_mut(&(cx, cz)) {
            c.is_modified = true;
        }
    }

    /// Damage pipeline on a native living row (mirrors
    /// `EntityLiving::attackEntityFrom` + `onDeath` with mob/animal drops).
    /// `attacker` supplies knockback direction; `None` skips it. Players
    /// route through respawn immunity, difficulty scaling, and armor like
    /// `EntityPlayerMP::attackEntityFrom` (death message and the health
    /// packet are the network slice's).
    pub fn attack_living(&mut self, id: EntityId, amount: i32, attacker: Option<EntityId>) {
        use crate::entity_living::{AttackInput, living_attack_run};
        self.sheep_shear(id, attacker);
        let amount = match self.entities.get(id) {
            Some(Entity::Player(p)) if p.respawn_ticks > 0 => return,
            Some(Entity::Player(_)) => match self.player_armored_damage(id, amount, attacker) {
                Some(scaled) => scaled,
                None => return,
            },
            _ => amount,
        };
        let input = match self.entities.get(id) {
            Some(Entity::Mob(_)) | Some(Entity::Animal(_)) | Some(Entity::Player(_)) => {
                let (l, px, pz) = match self.entities.get(id) {
                    Some(Entity::Mob(m)) => (&m.living, m.living.body.pos[0], m.living.body.pos[2]),
                    Some(Entity::Animal(a)) => (&a.living, a.living.body.pos[0], a.living.body.pos[2]),
                    Some(Entity::Player(p)) => (&p.living, p.living.body.pos[0], p.living.body.pos[2]),
                    _ => unreachable!(),
                };
                let (ax, az, has) = match attacker.and_then(|a| self.entities.get(a)) {
                    Some(a) => (a.body().pos[0], a.body().pos[2], true),
                    None => (0.0, 0.0, false),
                };
                AttackInput {
                    health: l.health,
                    hurt_resist: l.hurt_resist,
                    max_hurt_resist: l.max_hurt_resist,
                    last_damage: l.last_damage,
                    hurt_time_in: l.hurt_time,
                    attack_time_in: l.attack_time,
                    dead: l.body.dead,
                    amount,
                    has_attacker: has,
                    self_x: px,
                    self_z: pz,
                    atk_x: ax,
                    atk_z: az,
                    motion_x: l.body.motion[0],
                    motion_y: l.body.motion[1],
                    motion_z: l.body.motion[2],
                }
            }
            _ => return,
        };
        let result = {
            let rng = &mut self.rng;
            living_attack_run(&input, &mut || rng.next_double())
        };
        let r = match result {
            Some(r) => r,
            None => return,
        };
        let died = r.died;
        match self.entities.get_mut(id) {
            Some(Entity::Mob(m)) => {
                m.living.health = r.health;
                m.living.last_damage = r.last_damage;
                m.living.hurt_resist = r.hurt_resist;
                m.living.hurt_time = r.hurt_time;
                m.living.attack_time = r.attack_time;
                if r.knocked {
                    m.living.body.motion = [r.kmx, r.kmy, r.kmz];
                }
            }
            Some(Entity::Animal(a)) => {
                a.living.health = r.health;
                a.living.last_damage = r.last_damage;
                a.living.hurt_resist = r.hurt_resist;
                a.living.hurt_time = r.hurt_time;
                a.living.attack_time = r.attack_time;
                if r.knocked {
                    a.living.body.motion = [r.kmx, r.kmy, r.kmz];
                }
            }
            Some(Entity::Player(p)) => {
                p.living.health = r.health;
                p.living.last_damage = r.last_damage;
                p.living.hurt_resist = r.hurt_resist;
                p.living.hurt_time = r.hurt_time;
                p.living.attack_time = r.attack_time;
                if r.knocked {
                    p.living.body.motion = [r.kmx, r.kmy, r.kmz];
                }
            }
            _ => return,
        }
        if died {
            self.kill_living(id);
        }
    }

    /// Sheep shear (mirrors `EntitySheep::attackEntityFrom`): a living
    /// attacker scares 1..3 wool off first, independent of the damage
    /// pipeline that follows. Wool drops match the C++ shape exactly
    /// (spawn height, per-axis jitter, default pickup delay).
    fn sheep_shear(&mut self, id: EntityId, attacker: Option<EntityId>) {
        let (px, py, pz, h) = match self.entities.get(id) {
            Some(Entity::Animal(a))
                if a.kind == AnimalKind::Sheep && !a.sheared =>
            {
                (a.living.body.pos[0], a.living.body.pos[1], a.living.body.pos[2], a.living.body.height)
            }
            _ => return,
        };
        let living_attacker = attacker
            .and_then(|x| self.entities.get(x))
            .map(|e| matches!(e, Entity::Mob(_) | Entity::Animal(_) | Entity::Player(_)))
            .unwrap_or(false);
        if !living_attacker {
            return;
        }
        if let Some(Entity::Animal(a)) = self.entities.get_mut(id) {
            a.sheared = true;
        }
        let count = 1 + self.rng.next_int_bound(3);
        for _ in 0..count {
            let draws = [
                self.rng.next_double(),
                self.rng.next_double(),
                self.rng.next_double(),
                self.rng.next_double(),
                self.rng.next_double(),
            ];
            let wid = self.spawn_item_entity(35, 1, 0, px, py + h as f64 * 0.75, pz);
            if let Some(Entity::Item(e)) = self.entities.get_mut(wid) {
                e.body.motion[0] += (draws[1] - draws[2]) * 0.1;
                e.body.motion[1] += draws[0] * 0.05;
                e.body.motion[2] += (draws[3] - draws[4]) * 0.1;
            }
        }
    }

    /// Death: dismount both sides, spawn kind drops, mark dead (mirrors
    /// `EntityLiving::onDeath` + mob/animal `onDeath`). Players dismount
    /// and die with no drops yet (inventory scatter is the player slice).
    pub fn kill_living(&mut self, id: EntityId) {
        let (px, py, pz) = match self.entities.get(id) {
            Some(Entity::Mob(m)) => (m.living.body.pos[0], m.living.body.pos[1], m.living.body.pos[2]),
            Some(Entity::Animal(a)) => (a.living.body.pos[0], a.living.body.pos[1], a.living.body.pos[2]),
            Some(Entity::Player(p)) => (p.living.body.pos[0], p.living.body.pos[1], p.living.body.pos[2]),
            _ => return,
        };
        // Dismount rider and vehicle.
        let (riding, ridden_by) = match self.entities.get(id) {
            Some(e) => (e.body().riding, e.body().ridden_by),
            None => return,
        };
        if riding >= 0 {
            self.entities.mount(id, None);
        }
        if ridden_by >= 0 {
            self.entities.mount(ridden_by, None);
        }
        // Kind drops for mobs/animals (counts mirror the C++ getDropCount
        // formulas); players scatter their inventory instead.
        if matches!(self.entities.get(id), Some(Entity::Mob(_)) | Some(Entity::Animal(_))) {
            let (drop_id, drop_count) = self.living_drops(id);
            for _ in 0..drop_count {
                self.spawn_item_entity(drop_id, 1, 0, px, py, pz);
            }
        }
        if matches!(self.entities.get(id), Some(Entity::Player(_))) {
            self.scatter_player_inventory(id, px, py, pz);
        }
        if let Some(e) = self.entities.get_mut(id) {
            e.body_mut().dead = true;
        }
        self.death_events.push(id);
    }

    /// Player damage scaling (mirrors `EntityPlayerMP::attackEntityFrom`
    /// minus messaging and packets): difficulty scaling plus armor
    /// absorption with carry, damaging worn armor on the way. Returns
    /// `None` when the hit is fully absorbed.
    fn player_armored_damage(
        &mut self,
        id: EntityId,
        amount: i32,
        attacker: Option<EntityId>,
    ) -> Option<i32> {
        use crate::inventory::FfiItemStack;
        use crate::player_combat::alpha_combat_calculate_damage;
        use crate::player_inventory::{inventory_armor_value, inventory_damage_armor};
        let attacker_is_player = attacker
            .and_then(|a| self.entities.get(a))
            .map(|e| matches!(e, Entity::Player(_)))
            .unwrap_or(false);
        let mut tmp = [FfiItemStack {
            stack_size: 0,
            animations_to_go: 0,
            item_id: 0,
            item_damage: 0,
        }; 4];
        let carry = match self.entities.get(id) {
            Some(Entity::Player(p)) => {
                for (i, slot) in p.inventory.armor.iter().enumerate() {
                    tmp[i] = slot.unwrap_or(tmp[i]);
                }
                p.armor_carry
            }
            _ => return Some(amount),
        };
        let res = alpha_combat_calculate_damage(
            amount,
            attacker_is_player,
            self.difficulty,
            inventory_armor_value(&tmp),
            carry,
        );
        if res.scaled_damage <= 0 {
            return None;
        }
        inventory_damage_armor(&mut tmp, res.scaled_damage);
        if let Some(Entity::Player(p)) = self.entities.get_mut(id) {
            p.armor_carry = res.new_armor_damage_carry;
            for (i, slot) in p.inventory.armor.iter_mut().enumerate() {
                *slot = if tmp[i].item_id > 0 && tmp[i].stack_size > 0 {
                    Some(tmp[i])
                } else {
                    None
                };
            }
        }
        Some(res.damage_after_armor)
    }

    /// Death scatter (mirrors `EntityPlayerMP::onDeath` drops): every
    /// non-empty main/armor/crafting stack becomes an item entity at feet
    /// + 0.5 with drop velocity (3 world-RNG draws per stack, like the C++
    /// `playerDropVelocity` call) and a 40-tick pickup delay; all banks
    /// clear. The inventory-resend packet is the network slice's.
    fn scatter_player_inventory(&mut self, id: EntityId, px: f64, py: f64, pz: f64) {
        use crate::entity_player::alpha_player_drop_velocity;
        let stacks: Vec<crate::inventory::FfiItemStack> = match self.entities.get(id) {
            Some(Entity::Player(p)) => p
                .inventory
                .main
                .iter()
                .chain(p.inventory.armor.iter())
                .chain(p.inventory.crafting.iter())
                .filter_map(|s| *s)
                .collect(),
            _ => return,
        };
        if let Some(Entity::Player(p)) = self.entities.get_mut(id) {
            p.inventory.main = [None; 36];
            p.inventory.armor = [None; 4];
            p.inventory.crafting = [None; 4];
        }
        for s in stacks {
            if s.stack_size <= 0 || s.item_id <= 0 {
                continue;
            }
            let (ra, rb, rc) =
                (self.rng.next_double(), self.rng.next_double(), self.rng.next_double());
            let v = alpha_player_drop_velocity(ra, rb, rc);
            let eid = self.spawn_item_entity(s.item_id, s.stack_size, s.item_damage, px, py + 0.5, pz);
            if let Some(Entity::Item(e)) = self.entities.get_mut(eid) {
                e.body.motion = [v.mx, v.my, v.mz];
                e.pickup_delay = 40;
            }
        }
    }

    /// Pick up a stack into main inventory (mirrors
    /// `addItemStackToInventory` acceptance: empty and out-of-range ids
    /// refuse). Returns the leftover count like the C++ remainder write.
    pub fn player_add_item(
        &mut self,
        id: EntityId,
        mut stack: crate::inventory::FfiItemStack,
    ) -> i32 {
        use crate::inventory::FfiItemStack;
        use crate::player_inventory::inventory_add_item_to;
        if stack.stack_size <= 0 {
            return 0;
        }
        if stack.item_id <= 0 || stack.item_id >= 32000 {
            return stack.stack_size;
        }
        match self.entities.get_mut(id) {
            Some(Entity::Player(p)) => {
                let mut tmp = [FfiItemStack {
                    stack_size: 0,
                    animations_to_go: 0,
                    item_id: 0,
                    item_damage: 0,
                }; 36];
                for (i, slot) in p.inventory.main.iter().enumerate() {
                    tmp[i] = slot.unwrap_or(tmp[i]);
                }
                let rem = inventory_add_item_to(&mut tmp, &mut stack, 64);
                for (i, slot) in p.inventory.main.iter_mut().enumerate() {
                    *slot = if tmp[i].item_id > 0 && tmp[i].stack_size > 0 {
                        Some(tmp[i])
                    } else {
                        None
                    };
                }
                rem
            }
            _ => stack.stack_size,
        }
    }

    /// Held stack (mirrors `getCurrentItem`).
    pub fn player_held(&self, id: EntityId) -> Option<crate::inventory::FfiItemStack> {
        match self.entities.get(id) {
            Some(Entity::Player(p)) => p.inventory.held(),
            _ => None,
        }
    }

    fn living_drops(&mut self, id: EntityId) -> (i32, i32) {
        // Java EntityLiving.onDeath:388: nextInt(3) = 0..2 of getDropItemId
        // for EVERY mob/animal. Sheep has no getDropItemId (wool comes only
        // from the attack-shear), so death drops nothing.
        match self.entities.get(id) {
            Some(Entity::Mob(m)) => match m.kind {
                crate::entity_table::MobKind::Spider => (287, self.rng.next_int_bound(3)),
                crate::entity_table::MobKind::Zombie => (288, self.rng.next_int_bound(3)),
                crate::entity_table::MobKind::Skeleton => (262, self.rng.next_int_bound(3)),
                crate::entity_table::MobKind::Creeper => (289, self.rng.next_int_bound(3)),
            },
            Some(Entity::Animal(a)) => match a.kind {
                crate::entity_table::AnimalKind::Sheep => (0, 0),
                crate::entity_table::AnimalKind::Pig => (319, self.rng.next_int_bound(3)),
                crate::entity_table::AnimalKind::Chicken => (288, self.rng.next_int_bound(3)),
                crate::entity_table::AnimalKind::Cow => (334, self.rng.next_int_bound(3)),
            },
            _ => (0, 0),
        }
    }

    /// Shared eye-cell sample for [`World::tick_living`].
    fn living_sample(
        &self,
        l: &LivingBody,
        eye: f64,
    ) -> (bool, bool, bool, i32, i32, i32, i32) {
        let ex = l.body.pos[0].floor() as i32;
        let ey = eye.floor() as i32;
        let ez = l.body.pos[2].floor() as i32;
        (
            !l.body.dead,
            self.is_solid(ex, ey, ez),
            self.material_at(ex, ey, ez) == Material::WATER,
            l.body.air,
            l.hurt_time,
            l.attack_time,
            l.hurt_resist,
        )
    }

    /// Per-tick living maintenance (mirrors `EntityLiving::tick`).
    pub fn tick_living(&mut self, id: EntityId) {
        self.entities.tick_base(id);
        let (alive, opaque, water, air, hurt, attack, resist) = match self.entities.get(id) {
            Some(Entity::Mob(m)) => {
                let l = &m.living;
                let eye = l.body.pos[1] + l.body.height as f64 * 0.85;
                Self::living_sample(self, l, eye)
            }
            Some(Entity::Animal(a)) => {
                let l = &a.living;
                let eye = l.body.pos[1] + l.body.height as f64 * 0.85;
                Self::living_sample(self, l, eye)
            }
            Some(Entity::Player(p)) => {
                let l = &p.living;
                Self::living_sample(self, l, l.body.pos[1] + 1.62)
            }
            _ => return,
        };
        let t = crate::entity_living::alpha_living_tick(alive, opaque, water, air, hurt, attack, resist);
        match self.entities.get_mut(id) {
            Some(Entity::Mob(m)) => {
                m.living.body.air = t.air;
                m.living.hurt_time = t.hurt_time;
                m.living.attack_time = t.attack_time;
                m.living.hurt_resist = t.hurt_resist;
            }
            Some(Entity::Animal(a)) => {
                a.living.body.air = t.air;
                a.living.hurt_time = t.hurt_time;
                a.living.attack_time = t.attack_time;
                a.living.hurt_resist = t.hurt_resist;
            }
            Some(Entity::Player(p)) => {
                p.living.body.air = t.air;
                p.living.hurt_time = t.hurt_time;
                p.living.attack_time = t.attack_time;
                p.living.hurt_resist = t.hurt_resist;
            }
            _ => return,
        }
        if t.suffocate {
            self.attack_living(id, 1, None);
        }
        if t.drown {
            self.attack_living(id, 2, None);
        }
    }

    /// Player tick (mirrors `EntityPlayerMP::tick` minus digging and arm
    /// swing: living maintenance plus respawn-immunity decay).
    pub fn tick_player(&mut self, id: EntityId) {
        self.tick_living(id);
        if let Some(Entity::Player(p)) = self.entities.get_mut(id) {
            if p.respawn_ticks > 0 {
                p.respawn_ticks -= 1;
            }
        }
    }
}

/// Creature AI tuning constants (mirror the C++ `EntityCreature` /
/// `EntityMob` literals).
/// Target-acquire range (`getTargetRange`).
const CREATURE_TARGET_RANGE: f64 = 16.0;
/// Alpha grass block id (C++ `Block::grass->blockID`).
const GRASS_BLOCK_ID: u8 = 2;
/// Alpha ladder block id (checked by the base `isOnLadder`).
const LADDER_BLOCK_ID: u8 = 65;
/// Alpha egg item id (`Item::egg` is `new Item(88)`, i.e. 256 + 88).
const EGG_ITEM_ID: i32 = 344;

/// Wander weight rule selecting the `getBlockPathWeight` override: mobs
/// score everything 0.0, animals prefer grass, else light minus a half.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum WeightRule {
    Mob,
    Animal,
}

/// Owned AI snapshot of one mob/animal row so the tick can sequence shared
/// world queries and exclusive row updates without holding borrows.
#[derive(Clone, Debug)]
struct CreatureSnap {
    pos: [f64; 3],
    min_y: f64,
    width: f32,
    height: f32,
    yaw: f32,
    pitch: f32,
    collided_horiz: bool,
    move_speed: f32,
    path: Vec<[i32; 3]>,
    path_index: usize,
}

impl World {
    /// Daytime check (mirrors `World::isDaytime`: the first half of the
    /// 24000-tick day).
    pub fn is_daytime(&self) -> bool {
        let t = self.time % 24000;
        t >= 0 && t < 12000
    }

    /// Sky visibility (mirrors `World::canBlockSeeSky`: at/above the height
    /// map is open, missing chunks and below-zero read closed).
    pub fn can_see_sky(&self, x: i32, y: i32, z: i32) -> bool {
        if y < 0 {
            return false;
        }
        if y >= WORLD_HEIGHT {
            return true;
        }
        let (cx, cz, lx, lz) = Self::chunk_of(x, z);
        self.chunks.get(&(cx, cz)).map(|c| y >= c.get_height_value(lx, lz)).unwrap_or(false)
    }

    /// Combined light as a 0.0..1.0 fraction (mirrors the
    /// `getBlockLightValue / 15.0f` brightness in `EntityMob`).
    pub fn brightness(&self, x: i32, y: i32, z: i32) -> f32 {
        self.block_light_value(x, y, z) as f32 / 15.0
    }

    /// Liquid touch (mirrors `EntityLiving::isTouchingLiquid`): liquid
    /// material at the feet or the block above. Missing chunks read air.
    fn touching_liquid(&self, id: EntityId) -> bool {
        let (px, min_y, pz) = match self.entities.get(id) {
            Some(e) => (e.body().pos[0], e.body().bounding_box.min_y, e.body().pos[2]),
            None => return false,
        };
        let (x, y, z) = (floor_double(px), floor_double(min_y), floor_double(pz));
        self.material_at(x, y, z).is_liquid() || self.material_at(x, y + 1, z).is_liquid()
    }

    /// Lava touch (mirrors the lava branch of `EntityLiving.func_148_c`):
    /// lava at the feet or the block above damps 0.5 instead of 0.8.
    fn touching_lava(&self, id: EntityId) -> bool {
        let (px, min_y, pz) = match self.entities.get(id) {
            Some(e) => (e.body().pos[0], e.body().bounding_box.min_y, e.body().pos[2]),
            None => return false,
        };
        let (x, y, z) = (floor_double(px), floor_double(min_y), floor_double(pz));
        self.material_at(x, y, z) == Material::LAVA || self.material_at(x, y + 1, z) == Material::LAVA
    }

    /// Ground slipperiness under the feet (mirrors `Block.slipperiness`
    /// read in `EntityLiving.func_148_c`): 0.98 on ice, 0.6 default.
    fn ground_slipperiness(&self, id: EntityId) -> f32 {
        let (px, min_y, pz) = match self.entities.get(id) {
            Some(e) => (e.body().pos[0], e.body().bounding_box.min_y, e.body().pos[2]),
            None => return 0.6,
        };
        let (x, y, z) = (floor_double(px), floor_double(min_y) - 1, floor_double(pz));
        if self.get_block_id(x, y, z) == 79 {
            0.98
        } else {
            0.6
        }
    }

    /// Ladder grip (mirrors `isOnLadder`): ladders for everyone, any
    /// adjacent solid block for spiders (the Java wall-climb).
    fn ladder_for(&self, id: EntityId) -> bool {
        let row = self.entities.get(id);
        let (px, min_y, pz) = match row {
            Some(e) => (e.body().pos[0], e.body().bounding_box.min_y, e.body().pos[2]),
            None => return false,
        };
        let (x, y, z) = (floor_double(px), floor_double(min_y), floor_double(pz));
        let spider = matches!(row, Some(Entity::Mob(m)) if m.kind == MobKind::Spider);
        if spider {
            self.is_solid(x - 1, y, z)
                || self.is_solid(x + 1, y, z)
                || self.is_solid(x, y, z - 1)
                || self.is_solid(x, y, z + 1)
        } else {
            self.get_block_id(x, y, z) == LADDER_BLOCK_ID
                || self.get_block_id(x, y + 1, z) == LADDER_BLOCK_ID
        }
    }

    /// A target row counts when it is a live player with health left
    /// (mirrors `hasValidTarget`).
    fn target_alive(&self, id: EntityId) -> bool {
        matches!(
            self.entities.get(id),
            Some(Entity::Player(p)) if !p.living.body.dead && p.living.health > 0
        )
    }

    /// Aggro gate (mirrors `shouldAggroPlayer`): spiders only hunt when
    /// their own brightness is below 0.5, everyone else always aggroes.
    fn mob_aggro_ok(&self, kind: MobKind, id: EntityId) -> bool {
        if kind != MobKind::Spider {
            return true;
        }
        let (px, min_y, pz) = match self.entities.get(id) {
            Some(e) => (e.body().pos[0], e.body().bounding_box.min_y, e.body().pos[2]),
            None => return false,
        };
        self.brightness(floor_double(px), floor_double(min_y), floor_double(pz)) < 0.5
    }

    /// Fresh-target scan (mirrors `acquireTarget`): closest live player in
    /// range that passes the aggro gate.
    fn acquire_target(&self, id: EntityId, kind: MobKind) -> Option<EntityId> {
        let (px, py, pz) = match self.entities.get(id) {
            Some(e) => (e.body().pos[0], e.body().pos[1], e.body().pos[2]),
            None => return None,
        };
        let near = self.closest_player(px, py, pz, CREATURE_TARGET_RANGE)?;
        if !self.target_alive(near) || !self.mob_aggro_ok(kind, id) {
            return None;
        }
        // Java EntityMobs.func_158_i: only players the mob can see
        // (eye-to-eye raytrace clear, EntityLiving.func_145_g).
        if !self.can_entity_see(id, near) {
            return None;
        }
        Some(near)
    }

    /// Eye-to-eye visibility (mirrors `EntityLiving.func_145_g`): raytrace
    /// between eye heights (pos + height*0.85) must be clear.
    fn can_entity_see(&self, id: EntityId, target: EntityId) -> bool {
        let (sp, sh) = match self.entities.get(id) {
            Some(e) => (e.body().pos, e.body().height as f64),
            None => return false,
        };
        let (tp, th) = match self.entities.get(target) {
            Some(e) => (e.body().pos, e.body().height as f64),
            None => return false,
        };
        let from = [sp[0], sp[1] + sh * 0.85, sp[2]];
        let to = [tp[0], tp[1] + th * 0.85, tp[2]];
        self.ray_trace_clear(from, to)
    }

    /// Mob target refresh (mirrors the head of `EntityMob::updateAI`):
    /// re-acquire every 5 ticks, drop out-of-`1.5x`-range targets, and
    /// re-check aggro every tick. One deliberate hardening: a missing/dead
    /// target row clears to `None` (C++ keeps the stale pointer but nulls
    /// the effective target, same observable behavior).
    fn refresh_mob_target(&mut self, id: EntityId, kind: MobKind) -> Option<EntityId> {
        let (mut timer, mut target) = match self.entities.get(id) {
            Some(Entity::Mob(m)) => (m.target_timer, m.target),
            _ => return None,
        };
        if target.map(|t| !self.target_alive(t)).unwrap_or(false) {
            target = None;
        }
        timer -= 1;
        if timer <= 0 {
            timer = 5;
            match self.acquire_target(id, kind) {
                Some(fresh) => target = Some(fresh),
                None => {
                    if let Some(cur) = target {
                        let max = CREATURE_TARGET_RANGE * 1.5;
                        let (sp, tp) = match (self.entities.get(id), self.entities.get(cur)) {
                            (Some(s), Some(t)) => (s.body().pos, t.body().pos),
                            _ => return None,
                        };
                        let (dx, dy, dz) = (tp[0] - sp[0], tp[1] - sp[1], tp[2] - sp[2]);
                        if dx * dx + dy * dy + dz * dz > max * max {
                            target = None;
                        }
                    }
                }
            }
        }
        if let Some(cur) = target {
            if !self.target_alive(cur) || !self.mob_aggro_ok(kind, id) {
                target = None;
            }
        }
        if let Some(Entity::Mob(m)) = self.entities.get_mut(id) {
            m.target_timer = timer;
            m.target = target;
        }
        target
    }

    /// Wander destination (mirrors `pickWanderDestination`) through the
    /// shared [`wander_pick`] kernel. The chunk map and the RNG are
    /// disjoint field borrows, so the closures share the exact selection
    /// flow with the FFI path.
    fn wander_destination(&mut self, rule: WeightRule, base: [i32; 3]) -> Option<[i32; 3]> {
        let rng = &mut self.rng;
        let chunks = &self.chunks;
        let mut next = |bound: i32| rng.next_int_bound(bound);
        let mut weight = |x: i32, y: i32, z: i32| match rule {
            WeightRule::Mob => {
                alpha_ai_mob_path_weight(World::block_light_in(chunks, x, y, z) as f32 / 15.0)
            }
            WeightRule::Animal => alpha_ai_animal_path_weight(
                World::block_id_in(chunks, x, y - 1, z) == GRASS_BLOCK_ID,
                World::block_light_in(chunks, x, y, z) as f32 / 15.0,
            ),
        };
        wander_pick(base, &mut next, &mut weight)
    }

    /// A* over the native chunk map through the shared [`find_path_native`]
    /// core (liquid/movement answers mirror the C++ pathfinder shims).
    fn path_points(
        &self,
        start: (f64, f64, f64),
        target: (f64, f64, f64),
        width: f32,
        height: f32,
        max_dist: f32,
    ) -> Vec<[i32; 3]> {
        let chunks = &self.chunks;
        let is_liquid = |x: i32, y: i32, z: i32| World::material_in(chunks, x, y, z).is_liquid();
        let blocks = |x: i32, y: i32, z: i32| World::material_in(chunks, x, y, z).blocks_movement();
        find_path_native(
            &is_liquid, &blocks, start.0, start.1, start.2, target.0, target.1, target.2, width,
            height, max_dist,
        )
        .into_iter()
        .map(|(x, y, z)| [x, y, z])
        .collect()
    }

    /// Path to a block (mirrors `getPathToBlock`: integer target shifted by
    /// half a block like the C++ `createEntityPathTo` int overload).
    fn path_block_points(&self, id: EntityId, dst: [i32; 3], max_dist: f32) -> Vec<[i32; 3]> {
        let (bb, width, height) = match self.entities.get(id) {
            Some(e) => (e.body().bounding_box.clone(), e.body().width, e.body().height),
            None => return Vec::new(),
        };
        self.path_points(
            (bb.min_x, bb.min_y, bb.min_z),
            (dst[0] as f64 + 0.5, dst[1] as f64 + 0.5, dst[2] as f64 + 0.5),
            width,
            height,
            max_dist,
        )
    }

    /// Path to an entity (mirrors the pointer `getPathToEntity` overload:
    /// target feet plus eye height, not the bounding-box floor).
    fn path_target_points(&self, id: EntityId, target: EntityId, max_dist: f32) -> Vec<[i32; 3]> {
        let (bb, width, height) = match self.entities.get(id) {
            Some(e) => (e.body().bounding_box.clone(), e.body().width, e.body().height),
            None => return Vec::new(),
        };
        let tp = match self.entities.get(target) {
            Some(t) => {
                let eye = Self::living_eye_height(t);
                [t.body().pos[0], t.body().pos[1] + eye, t.body().pos[2]]
            }
            None => return Vec::new(),
        };
        self.path_points((bb.min_x, bb.min_y, bb.min_z), (tp[0], tp[1], tp[2]), width, height, max_dist)
    }

    /// Owned AI snapshot of one mob/animal row.
    fn creature_snapshot(&self, id: EntityId) -> Option<CreatureSnap> {
        let (body, move_speed, path, path_index) = match self.entities.get(id) {
            Some(Entity::Mob(m)) => (
                &m.living.body,
                m.living.move_speed,
                m.path.clone(),
                m.path_index,
            ),
            Some(Entity::Animal(a)) => (
                &a.living.body,
                a.living.move_speed,
                a.path.clone(),
                a.path_index,
            ),
            _ => return None,
        };
        Some(CreatureSnap {
            pos: body.pos,
            min_y: body.bounding_box.min_y,
            width: body.width,
            height: body.height,
            yaw: body.yaw,
            pitch: body.pitch,
            collided_horiz: body.collided_horiz,
            move_speed,
            path,
            path_index,
        })
    }

    /// Write back navigation state (yaw/pitch/path/jump flag) to a
    /// mob/animal row.
    fn store_creature_nav(&mut self, id: EntityId, snap: &CreatureSnap, jumping: bool) {
        match self.entities.get_mut(id) {
            Some(Entity::Mob(m)) => {
                m.living.body.yaw = snap.yaw;
                m.living.body.pitch = snap.pitch;
                m.living.jumping = jumping;
                m.path = snap.path.clone();
                m.path_index = snap.path_index;
            }
            Some(Entity::Animal(a)) => {
                a.living.body.yaw = snap.yaw;
                a.living.body.pitch = snap.pitch;
                a.living.jumping = jumping;
                a.path = snap.path.clone();
                a.path_index = snap.path_index;
            }
            _ => {}
        }
    }

    /// Clear a mob target (animals never hold one; no-op for them).
    fn store_mob_target(&mut self, id: EntityId, target: Option<EntityId>) {
        if let Some(Entity::Mob(m)) = self.entities.get_mut(id) {
            m.target = target;
        }
    }

    /// Shared creature phases (mirrors `EntityCreature::updateAI` minus the
    /// attack hook): target validation, wander/re-path selection, and path
    /// following. The `canSee + attackEntityAt` call arrives with the attack
    /// slice; `isAttacking_` is a literal false because C++ sets it false
    /// and never raises it. Returns the effective target.
    #[allow(clippy::too_many_arguments)]
    fn creature_phases(
        &mut self,
        id: EntityId,
        mut target: Option<EntityId>,
        rule: WeightRule,
        mob_kind: Option<MobKind>,
        snap: &mut CreatureSnap,
        in_liquid: bool,
        strafe: &mut f32,
        forward: &mut f32,
        jumping: &mut bool,
    ) -> Option<EntityId> {
        // Phase 1: drop dead/missing targets like the base clear branch,
        // then let visible targets take the per-kind attack.
        if let Some(t) = target {
            if !self.target_alive(t) {
                target = None;
                self.store_mob_target(id, None);
            }
        }
        if let (Some(kind), Some(t)) = (mob_kind, target) {
            let (sp, tp, teye) = match (self.entities.get(id), self.entities.get(t)) {
                (Some(s), Some(te)) => (s.body().pos, te.body().pos, Self::living_eye_height(te)),
                _ => return target,
            };
            let (dx, dy, dz) = (tp[0] - sp[0], tp[1] - sp[1], tp[2] - sp[2]);
            let dist = sqrt_float((dx * dx + dy * dy + dz * dz) as f32);
            let self_eye = snap.height as f64 * 0.85;
            let from = [sp[0], sp[1] + self_eye, sp[2]];
            let to = [tp[0], tp[1] + teye, tp[2]];
            if self.ray_trace_clear(from, to) {
                target = self.mob_attack(id, kind, t, dist, snap);
            }
        }
        // Phase 2: wander when targetless (or when the re-path gate skips),
        // else refresh the chase path. Draw order and short-circuits mirror
        // C++ exactly.
        let had_path = !snap.path.is_empty();
        if target.is_none() || (had_path && self.rng.next_int_bound(20) != 0) {
            if (!had_path && self.rng.next_int_bound(80) == 0) || self.rng.next_int_bound(80) == 0
            {
                let base =
                    [floor_double(snap.pos[0]), floor_double(snap.min_y), floor_double(snap.pos[2])];
                if let Some(dst) = self.wander_destination(rule, base) {
                    snap.path = self.path_block_points(id, dst, 10.0);
                    snap.path_index = 0;
                }
            }
        } else if let Some(t) = target {
            snap.path = self.path_target_points(id, t, CREATURE_TARGET_RANGE as f32);
            snap.path_index = 0;
        }
        // Phase 3: follow the path (mirrors `followPath`).
        *strafe = 0.0;
        *forward = 0.0;
        *jumping = false;
        if !snap.path.is_empty() && self.rng.next_int_bound(100) != 0 {
            // Path-point screen position: integer point plus the truncated
            // `(int)(width + 1) * 0.5` offset, like `PathEntity`.
            let width_offset = ((snap.width + 1.0) as i32) as f64 * 0.5;
            let wide = (snap.width * 2.0f32) as f64;
            let threshold = wide * wide;
            let mut point: Option<[f64; 3]> = None;
            loop {
                if snap.path_index >= snap.path.len() {
                    snap.path.clear();
                    snap.path_index = 0;
                    break;
                }
                let pt = snap.path[snap.path_index];
                let px = pt[0] as f64 + width_offset;
                let py = pt[1] as f64;
                let pz = pt[2] as f64 + width_offset;
                let (dxh, dzh) = (px - snap.pos[0], pz - snap.pos[2]);
                if dxh * dxh + dzh * dzh >= threshold {
                    point = Some([px, py, pz]);
                    break;
                }
                snap.path_index += 1;
                if snap.path_index >= snap.path.len() {
                    snap.path.clear();
                    snap.path_index = 0;
                    break;
                }
            }
            if let Some([px, py, pz]) = point {
                let dx = px - snap.pos[0];
                let dz = pz - snap.pos[2];
                let dy = py - floor_double(snap.min_y) as f64;
                // `moveForward_` was just zeroed, so `forward_in` is 0.0
                // literally like C++.
                let (tdx, tdz) = match target.and_then(|t| self.entities.get(t)) {
                    Some(t) => (t.body().pos[0] - snap.pos[0], t.body().pos[2] - snap.pos[2]),
                    None => (0.0, 0.0),
                };
                let steer =
                    steer_run(dx, dz, dy, snap.yaw, false, target.is_some(), tdx, tdz, 0.0);
                snap.yaw = steer.new_yaw;
                *strafe = steer.strafe;
                *forward = steer.forward;
                if steer.jump {
                    *jumping = true;
                }
            }
            // Face a chase target while closing in (30-degree turn).
            if let Some(t) = target {
                if let Some(te) = self.entities.get(t) {
                    let (dx, dz, dy) = {
                        let tb = te.body();
                        let self_eye = snap.height as f64 * 0.85;
                        let dy = match te {
                            Entity::Player(_)
                            | Entity::Mob(_)
                            | Entity::Animal(_) => {
                                let eye = Self::living_eye_height(te);
                                tb.pos[1] + eye - (snap.pos[1] + self_eye)
                            }
                            _ => {
                                (tb.bounding_box.min_y + tb.bounding_box.max_y) / 2.0
                                    - (snap.pos[1] + self_eye)
                            }
                        };
                        (tb.pos[0] - snap.pos[0], tb.pos[2] - snap.pos[2], dy)
                    };
                    let (ny, np) = face_run(dx, dz, dy, snap.yaw, snap.pitch, 30.0);
                    snap.yaw = ny;
                    snap.pitch = np;
                }
            }
        }
        // Jump over obstacles, paddle in liquid, then walk the path.
        if snap.collided_horiz {
            *jumping = true;
        }
        if self.rng.next_float() < 0.8 && in_liquid {
            *jumping = true;
        }
        if !snap.path.is_empty() {
            *forward = snap.move_speed;
        }
        target
    }

    /// Heading integration for one creature row through the shared
    /// [`living_heading_run`] core. Query and move closures share the world
    /// through a raw pointer: each call reborrows for its own duration only
    /// (the core never retains them), so the accesses never overlap.
    /// Returns the fall event distance when `onFall` must fire.
    fn move_creature_heading(&mut self, id: EntityId, strafe: f32, forward: f32) -> Option<f32> {
        let (jumping, on_ground, yaw, mut io) = match self.entities.get(id) {
            Some(Entity::Mob(m)) => (
                m.living.jumping,
                m.living.body.on_ground,
                m.living.body.yaw,
                HeadingIo {
                    motion_x: m.living.body.motion[0],
                    motion_y: m.living.body.motion[1],
                    motion_z: m.living.body.motion[2],
                    fall_distance: m.living.body.fall_distance,
                },
            ),
            Some(Entity::Animal(a)) => (
                a.living.jumping,
                a.living.body.on_ground,
                a.living.body.yaw,
                HeadingIo {
                    motion_x: a.living.body.motion[0],
                    motion_y: a.living.body.motion[1],
                    motion_z: a.living.body.motion[2],
                    fall_distance: a.living.body.fall_distance,
                },
            ),
            _ => return None,
        };
        let liquid = self.touching_liquid(id);
        let lava = self.touching_lava(id);
        let friction = self.ground_slipperiness(id);
        let world = self as *mut World;
        // SAFETY: re-entrant raw borrows, disjoint by construction:
        // Raw back-channel so the pre-move leg can sync the heading core's
        // fall state (ladder zeroing) into the row before `move_body` runs:
        // mirrors C++ where the zero lands on the entity field ahead of
        // `moveEntity` -> `updateFallState`. The core never touches `io`
        // while the mover runs (motion crosses by value), so the accesses
        // cannot overlap.
        let io_ptr = &mut io as *mut HeadingIo;
        let mut ladder = || unsafe { (*world).ladder_for(id) };
        let mut fall_ev: Option<f32> = None;
        let mut mover = |dx: f64, dy: f64, dz: f64, fb: &mut MoveFeedback| {
            unsafe {
                let w = &mut *world;
                if let Some(e) = w.entities.get_mut(id) {
                    e.body_mut().fall_distance = (*io_ptr).fall_distance;
                }
                fall_ev = w.move_body(id, dx, dy, dz);
                if let Some(e) = w.entities.get(id) {
                    fb.on_ground = e.body().on_ground;
                    fb.collided_vert = e.body().collided_vert;
                    fb.collided_horiz = e.body().collided_horiz;
                    fb.pos_y = e.body().pos[1];
                }
            }
            true
        };
        living_heading_run(strafe, forward, jumping, on_ground, yaw, &mut io, liquid, lava, friction, &mut ladder, &mut mover);
        // Only motion round-trips through `io` now: fall state already
        // lives in the row (synced pre-move, accumulated by `move_body`).
        match self.entities.get_mut(id) {
            Some(Entity::Mob(m)) => {
                m.living.body.motion = [io.motion_x, io.motion_y, io.motion_z];
            }
            Some(Entity::Animal(a)) => {
                a.living.body.motion = [io.motion_x, io.motion_y, io.motion_z];
            }
            _ => {}
        }
        fall_ev
    }

    /// Shove live neighbors apart (mirrors the tail of the mob/animal
    /// ticks: `getEntitiesWithinAABBExcludingEntity` skips the dead, then
    /// `applyEntityCollision` pushes both sides). Id-sorted for
    /// determinism.
    fn push_neighbors(&mut self, id: EntityId) {
        let (mask, self_pos) = match self.entities.get(id) {
            Some(e) => (e.body().bounding_box.expand(0.2, 0.0, 0.2), e.body().pos),
            None => return,
        };
        let self_pushable = !self.entities.get(id).map(|e| e.body().dead).unwrap_or(true);
        let mut others: Vec<(EntityId, f64, f64)> = Vec::new();
        for oid in self.entities.alive_ids() {
            if oid == id {
                continue;
            }
            if let Some(o) = self.entities.get(oid) {
                if mask.intersects_with(&o.body().bounding_box) {
                    others.push((oid, o.body().pos[0], o.body().pos[2]));
                }
            }
        }
        others.sort_by_key(|(oid, _, _)| *oid);
        for (oid, ox, oz) in others {
            let (sx, sz) = match self.entities.get(id) {
                Some(e) => (e.body().pos[0], e.body().pos[2]),
                None => (self_pos[0], self_pos[2]),
            };
            let mut push = PushOut { dvx1: 0.0, dvz1: 0.0, dvx2: 0.0, dvz2: 0.0 };
            let ok = alpha_entity_push(ox, oz, sx, sz, true, self_pushable, &mut push);
            if !ok {
                continue;
            }
            if let Some(o) = self.entities.get_mut(oid) {
                o.body_mut().motion[0] += push.dvx1;
                o.body_mut().motion[2] += push.dvz1;
            }
            if let Some(e) = self.entities.get_mut(id) {
                e.body_mut().motion[0] += push.dvx2;
                e.body_mut().motion[2] += push.dvz2;
            }
        }
    }

    /// Mob daylight ignition (mirrors `checkDaylightBurn`): burning kinds
    /// in daytime with bright sky access catch a 300-tick burn. Note the
    /// two heights: sky access reads `floor(posY)`, brightness reads
    /// `floor(minY)` like C++.
    fn check_daylight_burn(&mut self, id: EntityId, kind: MobKind) {
        if !mob_burns_in_daylight(kind) || !self.is_daytime() {
            return;
        }
        let (px, py, pz, min_y) = match self.entities.get(id) {
            Some(e) => (e.body().pos[0], e.body().pos[1], e.body().pos[2], e.body().bounding_box.min_y),
            None => return,
        };
        let (bx, by, bz) = (floor_double(px), floor_double(py), floor_double(pz));
        let brightness = self.brightness(bx, floor_double(min_y), bz);
        if brightness > 0.5 && self.can_see_sky(bx, by, bz) && self.rng.next_float() * 30.0 < (brightness - 0.4) * 2.0
        {
            if let Some(Entity::Mob(m)) = self.entities.get_mut(id) {
                m.burn_ticks = 300;
                m.living.body.fire = m.living.body.fire.max(300);
            }
        }
    }

    /// Mob AI update (mirrors `EntityMob::updateAI`): target refresh, the
    /// shared creature phases, the chase-speed bonus, and the liquid paddle
    /// gate. Returns the (strafe, forward) pair for the heading move.
    fn update_mob_ai(&mut self, id: EntityId, kind: MobKind, in_liquid: bool) -> (f32, f32) {
        let mut strafe = 0.0f32;
        let mut forward = 0.0f32;
        let mut jumping = false;
        let target = self.refresh_mob_target(id, kind);
        let mut snap = match self.creature_snapshot(id) {
            Some(s) => s,
            None => return (0.0, 0.0),
        };
        let target =
            self.creature_phases(id, target, WeightRule::Mob, Some(kind), &mut snap, in_liquid, &mut strafe, &mut forward, &mut jumping);
        // Chase bonus: full speed plus 20% past attack reach + 1.
        if let Some(t) = target {
            if let (Some(s), Some(te)) = (self.entities.get(id), self.entities.get(t)) {
                let (sp, tp) = (s.body().pos, te.body().pos);
                let (dx, dy, dz) = (tp[0] - sp[0], tp[1] - sp[1], tp[2] - sp[2]);
                let dist = sqrt_float((dx * dx + dy * dy + dz * dz) as f32);
                forward = chase_speed(snap.move_speed, dist, mob_attack_reach(kind));
            }
        }
        if in_liquid && self.rng.next_int_bound(5) != 0 {
            jumping = true;
        }
        self.store_creature_nav(id, &snap, jumping);
        (strafe, forward)
    }

    /// Animal AI update (mirrors `EntityCreature::updateAI` for animals:
    /// targetless shared phases, no chase bonus, no paddle gate).
    fn update_animal_ai(&mut self, id: EntityId, in_liquid: bool) -> (f32, f32) {
        let mut strafe = 0.0f32;
        let mut forward = 0.0f32;
        let mut jumping = false;
        let mut snap = match self.creature_snapshot(id) {
            Some(s) => s,
            None => return (0.0, 0.0),
        };
        self.creature_phases(id, None, WeightRule::Animal, None, &mut snap, in_liquid, &mut strafe, &mut forward, &mut jumping);
        self.store_creature_nav(id, &snap, jumping);
        (strafe, forward)
    }

    /// Despawn (mirrors `EntityLiving.func_152_d`: ++age; dead past 128
    /// blocks from the nearest player, or past age 600 + 1/800 roll past
    /// 32 blocks). Nearest-player search covers all players (the -1.0D
    /// radius means "any"). Returns true when the row died here.
    fn despawn_check(&mut self, id: EntityId) -> bool {
        let (px, py, pz) = match self.entities.get(id) {
            Some(e) if !e.body().dead => (e.body().pos[0], e.body().pos[1], e.body().pos[2]),
            _ => return true,
        };
        let mut best: Option<f64> = None;
        for oid in self.entities.alive_ids() {
            let (qx, qy, qz, is_player) = match self.entities.get(oid) {
                Some(Entity::Player(p)) if !p.living.body.dead => {
                    (p.living.body.pos[0], p.living.body.pos[1], p.living.body.pos[2], true)
                }
                _ => continue,
            };
            if !is_player {
                continue;
            }
            let (dx, dy, dz) = (qx - px, qy - py, qz - pz);
            let d2 = dx * dx + dy * dy + dz * dz;
            best = Some(best.map_or(d2, |b: f64| b.min(d2)));
        }
        let Some(d2) = best else {
            // No live players: still age the row, never despawn.
            match self.entities.get_mut(id) {
                Some(Entity::Mob(m)) => m.age += 1,
                Some(Entity::Animal(a)) => a.age += 1,
                _ => {}
            }
            return false;
        };
        if d2 > 16384.0 {
            if let Some(e) = self.entities.get_mut(id) {
                e.body_mut().dead = true;
            }
            return true;
        }
        let age = match self.entities.get_mut(id) {
            Some(Entity::Mob(m)) => {
                m.age += 1;
                m.age
            }
            Some(Entity::Animal(a)) => {
                a.age += 1;
                a.age
            }
            _ => return true,
        };
        if age > 600 && self.rng.next_int_bound(800) == 0 {
            if d2 < 1024.0 {
                match self.entities.get_mut(id) {
                    Some(Entity::Mob(m)) => m.age = 0,
                    Some(Entity::Animal(a)) => a.age = 0,
                    _ => {}
                }
            } else {
                if let Some(e) = self.entities.get_mut(id) {
                    e.body_mut().dead = true;
                }
                return true;
            }
        }
        false
    }

    /// Mob tick (mirrors `EntityMob::tick`): living maintenance, cooldown
    /// and burn schedule, daylight ignition, AI, heading move with fall
    /// damage, and neighbor shoves. Like C++, the AI and move still run
    /// when burn damage kills mid-tick.
    pub fn tick_mob(&mut self, id: EntityId) {
        // Peaceful (difficulty 0): mobs die instead of ticking
        // (Java EntityMobs.onUpdate: monstersEnabled == 0 -> dead).
        if self.difficulty == 0 {
            if let Some(e) = self.entities.get_mut(id) {
                e.body_mut().dead = true;
            }
            return;
        }
        if self.despawn_check(id) {
            return;
        }
        self.tick_living(id);
        let kind = match self.entities.get(id) {
            Some(Entity::Mob(m)) if !m.living.body.dead => m.kind,
            _ => return,
        };
        let mut burn_hit = false;
        if let Some(Entity::Mob(m)) = self.entities.get_mut(id) {
            if m.attack_cooldown > 0 {
                m.attack_cooldown -= 1;
            }
            if m.burn_ticks <= 0 && m.living.body.fire <= 20 {
                m.living.body.fire = 0;
            }
            if m.burn_ticks > 0 {
                m.living.body.fire = m.living.body.fire.max(20);
                if m.burn_ticks % 20 == 0 {
                    burn_hit = true;
                }
                m.burn_ticks -= 1;
            }
        }
        if burn_hit {
            self.attack_living(id, 1, None);
        }
        self.check_daylight_burn(id, kind);
        let in_liquid = self.touching_liquid(id);
        let (strafe, forward) = self.update_mob_ai(id, kind, in_liquid);
        if let Some(dist) = self.move_creature_heading(id, strafe, forward) {
            let damage = alpha_living_fall_damage(dist);
            if damage > 0 {
                self.attack_living(id, damage, None);
            }
        }
        self.push_neighbors(id);
    }

    /// Animal tick (mirrors `EntityAnimals::tick`): living maintenance, the
    /// shared creature AI, heading move with fall damage (chickens override
    /// `onFall` to a no-op), chicken extras, and neighbor shoves.
    pub fn tick_animal(&mut self, id: EntityId) {
        if self.despawn_check(id) {
            return;
        }
        self.tick_living(id);
        let kind = match self.entities.get(id) {
            Some(Entity::Animal(a)) if !a.living.body.dead => a.kind,
            _ => return,
        };
        let in_liquid = self.touching_liquid(id);
        let (strafe, forward) = self.update_animal_ai(id, in_liquid);
        if let Some(dist) = self.move_creature_heading(id, strafe, forward) {
            if kind != AnimalKind::Chicken {
                let damage = alpha_living_fall_damage(dist);
                if damage > 0 {
                    self.attack_living(id, damage, None);
                }
            }
        }
        if kind == AnimalKind::Chicken {
            self.chicken_extra(id);
        }
        self.push_neighbors(id);
    }

    /// Chicken extras (mirrors `tickExtra`): slow sinking plus the egg
    /// clock. Runs after the move like C++, so the damping shapes the next
    /// tick's motion.
    fn chicken_extra(&mut self, id: EntityId) {
        if let Some(Entity::Animal(a)) = self.entities.get_mut(id) {
            if a.living.body.motion[1] < 0.0 && !a.living.body.on_ground {
                a.living.body.motion[1] *= 0.6;
            }
            a.egg_timer -= 1;
        }
        let lay = matches!(self.entities.get(id), Some(Entity::Animal(a)) if a.egg_timer <= 0);
        if !lay {
            return;
        }
        let (px, py, pz) = match self.entities.get(id) {
            Some(e) => (e.body().pos[0], e.body().pos[1], e.body().pos[2]),
            None => return,
        };
        self.spawn_item_entity(EGG_ITEM_ID, 1, 0, px, py, pz);
        let roll = self.rng.next_int_bound(6000);
        if let Some(Entity::Animal(a)) = self.entities.get_mut(id) {
            a.egg_timer = 6000 + roll;
        }
    }
}

/// Creeper blast radius (mirrors the Alpha inline `explode`).
const CREEPER_BLAST_RADIUS: f32 = 3.0;
/// Fire block id placed by explosions.
const FIRE_BLOCK_ID: u8 = 51;

impl World {
    /// Line of sight (mirrors `canEntitySee` → `rayTraceBlocks` with
    /// `includeLiquids = false`): the same DDA walk, blocked by any
    /// collidable block. Only fluids let sight through (`canCollideCheck`
    /// is false solely for `BlockFluid`; flowers, torches and crops block
    /// like stone). Missing chunks read air like every other native query
    /// (C++ loads/generates there).
    pub fn ray_trace_clear(&self, from: [f64; 3], to: [f64; 3]) -> bool {
        if ![from[0], from[1], from[2], to[0], to[1], to[2]].iter().all(|v| v.is_finite()) {
            return true;
        }
        let (mut cx, mut cy, mut cz) = (from[0], from[1], from[2]);
        let (mut ccx, mut ccy, mut ccz) = (cx.floor() as i32, cy.floor() as i32, cz.floor() as i32);
        let (tx, ty, tz) = (to[0].floor() as i32, to[1].floor() as i32, to[2].floor() as i32);
        for _ in 0..=200 {
            if !cx.is_finite() || !cy.is_finite() || !cz.is_finite() {
                return true;
            }
            if ccx == tx && ccy == ty && ccz == tz {
                return true;
            }
            let (mut nbx, mut nby, mut nbz) = (999.0f64, 999.0f64, 999.0f64);
            if tx > ccx {
                nbx = ccx as f64 + 1.0;
            }
            if tx < ccx {
                nbx = ccx as f64;
            }
            if ty > ccy {
                nby = ccy as f64 + 1.0;
            }
            if ty < ccy {
                nby = ccy as f64;
            }
            if tz > ccz {
                nbz = ccz as f64 + 1.0;
            }
            if tz < ccz {
                nbz = ccz as f64;
            }
            let (dx, dy, dz) = (to[0] - cx, to[1] - cy, to[2] - cz);
            let (mut sx, mut sy, mut sz) = (999.0f64, 999.0f64, 999.0f64);
            if nbx != 999.0 && dx.abs() > 1.0e-7 {
                sx = (nbx - cx) / dx;
            }
            if nby != 999.0 && dy.abs() > 1.0e-7 {
                sy = (nby - cy) / dy;
            }
            if nbz != 999.0 && dz.abs() > 1.0e-7 {
                sz = (nbz - cz) / dz;
            }
            let side: i8;
            if sx < sy && sx < sz {
                side = if tx > ccx { 4 } else { 5 };
                cx = nbx;
                cy += dy * sx;
                cz += dz * sx;
            } else if sy < sz {
                side = if ty > ccy { 0 } else { 1 };
                cx += dx * sy;
                cy = nby;
                cz += dz * sy;
            } else {
                side = if tz > ccz { 2 } else { 3 };
                cx += dx * sz;
                cy += dy * sz;
                cz = nbz;
            }
            ccx = cx.floor() as i32;
            if side == 5 {
                ccx -= 1;
            }
            ccy = cy.floor() as i32;
            if side == 1 {
                ccy -= 1;
            }
            ccz = cz.floor() as i32;
            if side == 3 {
                ccz -= 1;
            }
            let bid = self.get_block_id(ccx, ccy, ccz);
            if bid == 0 {
                continue;
            }
            if alpha_block_properties_get(bid as u32).block_type == BlockType::Fluid as u8 {
                continue;
            }
            return false;
        }
        true
    }

    /// First-hit ray trace with liquids included (mirrors `rayTraceBlocks`
    /// with `includeLiquids = true` for boat aiming): same DDA walk,
    /// returning the first collidable cell. Still fluids block the ray
    /// (flowing water 1..7 lets it through like `canCollideCheck`).
    pub fn ray_trace_hit_liquids(&self, from: [f64; 3], to: [f64; 3]) -> Option<[i32; 3]> {
        if ![from[0], from[1], from[2], to[0], to[1], to[2]].iter().all(|v| v.is_finite()) {
            return None;
        }
        let (mut cx, mut cy, mut cz) = (from[0], from[1], from[2]);
        let (mut ccx, mut ccy, mut ccz) = (cx.floor() as i32, cy.floor() as i32, cz.floor() as i32);
        let (tx, ty, tz) = (to[0].floor() as i32, to[1].floor() as i32, to[2].floor() as i32);
        for _ in 0..=200 {
            if !cx.is_finite() || !cy.is_finite() || !cz.is_finite() {
                return None;
            }
            if ccx == tx && ccy == ty && ccz == tz {
                return None;
            }
            let (mut nbx, mut nby, mut nbz) = (999.0f64, 999.0f64, 999.0f64);
            if tx > ccx {
                nbx = ccx as f64 + 1.0;
            }
            if tx < ccx {
                nbx = ccx as f64;
            }
            if ty > ccy {
                nby = ccy as f64 + 1.0;
            }
            if ty < ccy {
                nby = ccy as f64;
            }
            if tz > ccz {
                nbz = ccz as f64 + 1.0;
            }
            if tz < ccz {
                nbz = ccz as f64;
            }
            let (dx, dy, dz) = (to[0] - cx, to[1] - cy, to[2] - cz);
            let (mut sx, mut sy, mut sz) = (999.0f64, 999.0f64, 999.0f64);
            if nbx != 999.0 && dx.abs() > 1.0e-7 {
                sx = (nbx - cx) / dx;
            }
            if nby != 999.0 && dy.abs() > 1.0e-7 {
                sy = (nby - cy) / dy;
            }
            if nbz != 999.0 && dz.abs() > 1.0e-7 {
                sz = (nbz - cz) / dz;
            }
            let side: i8;
            if sx < sy && sx < sz {
                side = if tx > ccx { 4 } else { 5 };
                cx = nbx;
                cy += dy * sx;
                cz += dz * sx;
            } else if sy < sz {
                side = if ty > ccy { 0 } else { 1 };
                cx += dx * sy;
                cy = nby;
                cz += dz * sy;
            } else {
                side = if tz > ccz { 2 } else { 3 };
                cx += dx * sz;
                cy += dy * sz;
                cz = nbz;
            }
            ccx = cx.floor() as i32;
            if side == 5 {
                ccx -= 1;
            }
            ccy = cy.floor() as i32;
            if side == 1 {
                ccy -= 1;
            }
            ccz = cz.floor() as i32;
            if side == 3 {
                ccz -= 1;
            }
            let bid = self.get_block_id(ccx, ccy, ccz);
            if bid == 0 {
                continue;
            }
            if alpha_block_properties_get(bid as u32).block_type == BlockType::Fluid as u8 {
                // canCollideCheck(meta, true): falling (8+) reads as still,
                // still (0) blocks, flowing (1..7) lets the ray through.
                let mut meta = self.get_block_meta(ccx, ccy, ccz);
                if meta >= 8 {
                    meta = 0;
                }
                if meta != 0 {
                    continue;
                }
            }
            return Some([ccx, ccy, ccz]);
        }
        None
    }

    /// Melee line of sight (mirrors the attack `hasLineOfSight`): the
    /// attacker's eye against three target samples (feet + 0.1, middle,
    /// eye), blocked by swept collision boxes strictly inside the segment
    /// (`+ 1.0E-6 <` like C++).
    pub fn attack_los(&self, attacker_id: EntityId, target_id: EntityId) -> bool {
        use crate::vec3d::Vec3D;
        let (eye, samples) = match (self.entities.get(attacker_id), self.entities.get(target_id)) {
            (Some(a), Some(t)) => {
                let ab = a.body();
                let eye =
                    Vec3D::new(ab.pos[0], ab.pos[1] + Self::living_eye_height(a), ab.pos[2]);
                let tb = t.body();
                let teye = Self::living_eye_height(t);
                (
                    eye,
                    [
                        Vec3D::new(tb.pos[0], tb.bounding_box.min_y + 0.1, tb.pos[2]),
                        Vec3D::new(tb.pos[0], tb.pos[1] + tb.height as f64 * 0.5, tb.pos[2]),
                        Vec3D::new(tb.pos[0], tb.pos[1] + teye, tb.pos[2]),
                    ],
                )
            }
            _ => return false,
        };
        for to in &samples {
            if !self.segment_blocked(&eye, to) {
                return true;
            }
        }
        false
    }

    /// Swept-box segment test for [`World::attack_los`] (mirrors
    /// `rayHitsSolidBlock`): any collidable cell box clipped by the
    /// segment strictly inside counts.
    fn segment_blocked(&self, from: &crate::vec3d::Vec3D, to: &crate::vec3d::Vec3D) -> bool {
        let min_x = floor_double(from.x_coord.min(to.x_coord));
        let min_y = floor_double(from.y_coord.min(to.y_coord));
        let min_z = floor_double(from.z_coord.min(to.z_coord));
        let max_x = floor_double(from.x_coord.max(to.x_coord));
        let max_y = floor_double(from.y_coord.max(to.y_coord));
        let max_z = floor_double(from.z_coord.max(to.z_coord));
        let target_sq = (from.x_coord - to.x_coord).powi(2)
            + (from.y_coord - to.y_coord).powi(2)
            + (from.z_coord - to.z_coord).powi(2);
        for x in min_x..=max_x {
            for y in min_y..=max_y {
                for z in min_z..=max_z {
                    let bid = self.get_block_id(x, y, z);
                    if bid == 0 {
                        continue;
                    }
                    let props = alpha_block_properties_get(bid as u32);
                    if !has_collision_box(props.block_type) || !has_collision_id(bid) {
                        continue;
                    }
                    let bb = AxisAlignedBB::get_bounding_box(
                        x as f64 + props.min_x as f64,
                        y as f64 + props.min_y as f64,
                        z as f64 + props.min_z as f64,
                        x as f64 + props.max_x as f64,
                        y as f64 + props.max_y as f64,
                        z as f64 + props.max_z as f64,
                    );
                    if let Some(hit) = bb.clip(from, to) {
                        let d = (from.x_coord - hit.hit_vec.x_coord).powi(2)
                            + (from.y_coord - hit.hit_vec.y_coord).powi(2)
                            + (from.z_coord - hit.hit_vec.z_coord).powi(2);
                        if d + 1.0e-6 < target_sq {
                            return true;
                        }
                    }
                }
            }
        }
        false
    }

    /// Phase-1 attack dispatch (mirrors `attackEntityAt` → per-kind
    /// `attackTarget`). `dist` is the C++ phase-1 distance
    /// (`sqrt_float(float(full 3D pos distSq))`). Returns the effective
    /// target (spiders drop it in daylight).
    fn mob_attack(
        &mut self,
        id: EntityId,
        kind: MobKind,
        target: EntityId,
        dist: f32,
        snap: &mut CreatureSnap,
    ) -> Option<EntityId> {
        match kind {
            MobKind::Zombie => self.zombie_punch(id, target, dist),
            MobKind::Skeleton => self.skeleton_volley(id, target, dist),
            MobKind::Spider => self.spider_attack(id, target, dist, snap),
            MobKind::Creeper => self.creeper_swell(id, target, dist),
        }
    }

    /// Base melee (mirrors `EntityMob::attackTarget`): in-reach, vertical
    /// overlap, cooldown-gated strength-5 poke.
    fn zombie_punch(&mut self, id: EntityId, target: EntityId, dist: f32) -> Option<EntityId> {
        let (overlap, ready) = match (self.entities.get(id), self.entities.get(target)) {
            (Some(s), Some(t)) => (
                t.body().bounding_box.max_y > s.body().bounding_box.min_y
                    && t.body().bounding_box.min_y < s.body().bounding_box.max_y,
                matches!(s, Entity::Mob(m) if m.attack_cooldown == 0),
            ),
            _ => return Some(target),
        };
        if dist < 2.5 && overlap && ready {
            if let Some(Entity::Mob(m)) = self.entities.get_mut(id) {
                m.attack_cooldown = 20;
            }
            self.attack_living(target, 5, Some(id));
        }
        Some(target)
    }

    /// Skeleton volley (mirrors the override): loose an arrow inside
    /// reach 10 on cooldown 30. NOTE: C++ also writes `moveForward_` here,
    /// but the mob chase tail unconditionally overwrites it right after,
    /// so that store is dead and skipped deliberately.
    fn skeleton_volley(&mut self, id: EntityId, target: EntityId, dist: f32) -> Option<EntityId> {
        let ready = matches!(self.entities.get(id), Some(Entity::Mob(m)) if m.attack_cooldown == 0);
        if dist < 10.0 && ready {
            if let Some(Entity::Mob(m)) = self.entities.get_mut(id) {
                m.attack_cooldown = 30;
            }
            self.spawn_skeleton_arrow(id, target);
        }
        Some(target)
    }

    /// Skeleton arrow (mirrors the ctor + volley sequence): eye-height
    /// start with the 0.16 yaw backoff (quantized tables), the Java-parity
    /// +1.4 lift, aim at the victim's eye minus 0.2 with the f32 range
    /// lift, and 0.6/12.0 launch. The ctor-equivalent spread run is kept
    /// (draws consumed like C++) with its result discarded unless the
    /// volley aim degenerates.
    fn spawn_skeleton_arrow(&mut self, id: EntityId, target: EntityId) {
        use crate::entity_misc::arrow_shoot_run;
        use crate::math_helper::{cos, sin};
        let (sp, yaw, pitch, eye) = match self.entities.get(id) {
            Some(e) => (e.body().pos, e.body().yaw, e.body().pitch, e.body().height as f64 * 0.85),
            None => return,
        };
        let tp = match self.entities.get(target) {
            Some(t) => {
                let te = Self::living_eye_height(t);
                [t.body().pos[0], t.body().pos[1] + te, t.body().pos[2]]
            }
            None => return,
        };
        let rad = yaw / 180.0 * std::f32::consts::PI;
        let (ax, ay, az) =
            (sp[0] - cos(rad) as f64 * 0.16, sp[1] + eye - 0.1 + 1.4, sp[2] - sin(rad) as f64 * 0.16);
        let (dx, dz) = (tp[0] - sp[0], tp[2] - sp[2]);
        let dy = tp[1] - 0.2 - ay;
        let lift = sqrt_float((dx * dx + dz * dz) as f32) * 0.2;
        // Ctor-equivalent spread (result discarded; draws consumed).
        let prad = pitch / 180.0 * std::f32::consts::PI;
        let (myaw, mpitch) = (rad, prad);
        let dir = [
            -sin(myaw) as f64 * cos(mpitch) as f64,
            -sin(mpitch) as f64,
            cos(myaw) as f64 * cos(mpitch) as f64,
        ];
        let ctor_motion = {
            let rng = &mut self.rng;
            arrow_shoot_run(dir[0], dir[1], dir[2], 1.5, 1.0, &mut || rng.next_double())
        };
        let motion = {
            let rng = &mut self.rng;
            arrow_shoot_run(dx, dy + lift as f64, dz, 0.6, 12.0, &mut || rng.next_double())
        };
        let motion = motion.or(ctor_motion).unwrap_or([0.0, 0.0, 0.0]);
        let (mut fy, mut fp) = (yaw, pitch);
        crate::entity_misc::alpha_arrow_face_velocity(motion[0], motion[1], motion[2], &mut fy, &mut fp);
        let nid = self.entities.alloc_id();
        let mut b = Body::new(nid, 0.5, 0.5, 0.0);
        b.set_position(ax, ay, az);
        b.yaw = fy;
        b.pitch = fp;
        b.prev_yaw = fy;
        b.prev_pitch = fp;
        b.motion = motion;
        self.entities.insert(Entity::Arrow(crate::entity_table::ArrowEnt {
            body: b,
            in_ground: false,
            shake: 0,
            ticks_in_ground: 0,
            ticks_in_air: 0,
            shooter_id: id,
            tile: [-1, -1, -1],
            in_tile: 0,
        }));
    }

    /// Spider attack (mirrors the override): drop the target in bright
    /// light sometimes, pounce from 2..6 blocks on the ground, else the
    /// reach-2.5 strength-2 bite with vertical overlap (Java EntityMobs:50).
    fn spider_attack(
        &mut self,
        id: EntityId,
        target: EntityId,
        dist: f32,
        snap: &mut CreatureSnap,
    ) -> Option<EntityId> {
        let (px, min_y, pz, on_ground, motion) = match self.entities.get(id) {
            Some(e) => (
                e.body().pos[0],
                e.body().bounding_box.min_y,
                e.body().pos[2],
                e.body().on_ground,
                e.body().motion,
            ),
            None => return Some(target),
        };
        if self.brightness(floor_double(px), floor_double(min_y), floor_double(pz)) > 0.5
            && self.rng.next_int_bound(100) == 0
        {
            self.store_mob_target(id, None);
            snap.path.clear();
            snap.path_index = 0;
            return None;
        }
        if dist > 2.0 && dist < 6.0 && self.rng.next_int_bound(10) == 0 && on_ground {
            if let (Some(s), Some(t)) = (self.entities.get(id), self.entities.get(target)) {
                let (dx, dz) = (t.body().pos[0] - s.body().pos[0], t.body().pos[2] - s.body().pos[2]);
                let len = (dx * dx + dz * dz).sqrt().max(0.001);
                if let Some(e) = self.entities.get_mut(id) {
                    let b = e.body_mut();
                    b.motion[0] = dx / len * 0.4 + motion[0] * 0.2;
                    b.motion[2] = dz / len * 0.4 + motion[2] * 0.2;
                    b.motion[1] = 0.4;
                }
            }
            return Some(target);
        }
        let ready = matches!(self.entities.get(id), Some(Entity::Mob(m)) if m.attack_cooldown == 0);
        let overlap = match (self.entities.get(id), self.entities.get(target)) {
            (Some(s), Some(t)) => {
                t.body().bounding_box.max_y > s.body().bounding_box.min_y
                    && t.body().bounding_box.min_y < s.body().bounding_box.max_y
            }
            _ => false,
        };
        if dist < 2.5 && overlap && ready {
            if let Some(Entity::Mob(m)) = self.entities.get_mut(id) {
                m.attack_cooldown = 20;
            }
            self.attack_living(target, 2, Some(id));
        }
        Some(target)
    }

    /// Creeper fuse (mirrors the override): swell while close (3 blocks
    /// cold, 7 once lit), decay otherwise, explode at 30. The
    /// `moveForward_` stores are dead like the skeleton's (chase tail
    /// overwrites) and skipped. The fuse hiss has no native audio yet.
    fn creeper_swell(&mut self, id: EntityId, target: EntityId, dist: f32) -> Option<EntityId> {
        let (time, dir) = match self.entities.get(id) {
            Some(Entity::Mob(m)) => (m.swell_time, m.swell_dir),
            _ => return Some(target),
        };
        let (time, dir) = if (dir <= 0 && dist < 3.0) || (dir > 0 && dist < 7.0) {
            let time = time + 1;
            if time >= 30 {
                if let Some(Entity::Mob(m)) = self.entities.get_mut(id) {
                    m.swell_time = time;
                    m.swell_dir = 1;
                }
                self.creeper_explode(id);
                return Some(target);
            }
            (time, 1)
        } else {
            (if time > 0 { time - 1 } else { time }, -1)
        };
        if let Some(Entity::Mob(m)) = self.entities.get_mut(id) {
            m.swell_time = time;
            m.swell_dir = dir;
        }
        Some(target)
    }

    /// Shared blast (vanilla `Explosion` simplified): entity damage with the
    /// Java formula `(v*v+v)/2*8*size+1` and knockback, LOS-gated by the
    /// eye raytrace; blocks in the sphere destroyed unless unbreakable,
    /// drops at 0.3 via the harvest table, TNT cells chain-ignite instead
    /// of dropping. Neither creepers nor TNT set fire in Alpha (only the
    /// ghast fireball does), so no fire phase here.
    fn blast(&mut self, px: f64, py: f64, pz: f64, radius: f32, attacker: Option<EntityId>) {
        // Phase 1: living victims in the radius*2 box.
        let mut victims: Vec<EntityId> = Vec::new();
        for oid in self.entities.alive_ids() {
            let is_living = match self.entities.get(oid) {
                Some(Entity::Mob(m)) if !m.living.body.dead => true,
                Some(Entity::Animal(a)) if !a.living.body.dead => true,
                Some(Entity::Player(p)) if !p.living.body.dead => true,
                _ => false,
            };
            if !is_living {
                continue;
            }
            if Some(oid) == attacker {
                continue;
            }
            let (qx, qy, qz) = match self.entities.get(oid) {
                Some(e) => (e.body().pos[0], e.body().pos[1], e.body().pos[2]),
                None => continue,
            };
            let (dx, dy, dz) = (qx - px, qy - py, qz - pz);
            let d = ((dx * dx + dy * dy + dz * dz) as f32).sqrt();
            if d > radius {
                continue;
            }
            victims.push(oid);
        }
        victims.sort_unstable();
        for v in victims {
            let (qx, qy, qz, h) = match self.entities.get(v) {
                Some(e) => (e.body().pos[0], e.body().pos[1], e.body().pos[2], e.body().height as f64),
                None => continue,
            };
            let (dx, dy, dz) = (qx - px, qy - py, qz - pz);
            let d = ((dx * dx + dy * dy + dz * dz) as f32).sqrt();
            if d > radius {
                continue;
            }
            // LOS from the blast center to the victim's eye.
            if !self.ray_trace_clear([px, py, pz], [qx, qy + h * 0.85, qz]) {
                continue;
            }
            let vfrac = 1.0 - d / radius;
            let damage = ((vfrac as f64 * vfrac as f64 + vfrac as f64) / 2.0 * 8.0 * radius as f64 + 1.0) as i32;
            // Knockback along the blast direction, scaled by exposure.
            let len = (dx * dx + dy * dy + dz * dz).sqrt().max(0.001);
            if let Some(e) = self.entities.get_mut(v) {
                let b = e.body_mut();
                b.motion[0] += dx / len * vfrac as f64;
                b.motion[1] += dy / len * vfrac as f64;
                b.motion[2] += dz / len * vfrac as f64;
            }
            self.attack_living(v, damage.max(1), attacker);
        }
        // Phase 2: blocks in the sphere.
        let (cx, cy, cz) = (px.floor() as i32, py.floor() as i32, pz.floor() as i32);
        let r = radius.ceil() as i32;
        let mut tnt_chain: Vec<(i32, i32, i32)> = Vec::new();
        let mut removals: Vec<(i32, i32, i32, u8, u8)> = Vec::new();
        for dx in -r..=r {
            for dy in -r..=r {
                for dz in -r..=r {
                    let d = ((dx * dx + dy * dy + dz * dz) as f32).sqrt();
                    if d > radius {
                        continue;
                    }
                    let (bx, by, bz) = (cx + dx, cy + dy, cz + dz);
                    let bid = self.get_block_id(bx, by, bz);
                    if bid == 0 {
                        continue;
                    }
                    if alpha_block_properties_get(bid as u32).hardness < 0.0 {
                        continue;
                    }
                    if bid == 46 {
                        tnt_chain.push((bx, by, bz));
                        removals.push((bx, by, bz, bid, self.get_block_meta(bx, by, bz)));
                        continue;
                    }
                    if self.rng.next_float() <= 0.3 {
                        removals.push((bx, by, bz, bid, self.get_block_meta(bx, by, bz)));
                    } else {
                        removals.push((bx, by, bz, 0, 0));
                    }
                }
            }
        }
        for (bx, by, bz, bid, meta) in removals {
            if bid == 0 {
                self.apply_set_notify(bx, by, bz, 0);
                continue;
            }
            if bid == 46 {
                // TNT never drops — it chains with a short fuse.
                self.apply_set_notify(bx, by, bz, 0);
                continue;
            }
            let (drop, qty) = self.rolled_drop_ids(bid, meta);
            self.apply_set_notify(bx, by, bz, 0);
            if drop > 0 && qty > 0 {
                self.spawn_item_entity(drop, qty, 0, bx as f64 + 0.5, by as f64 + 0.5, bz as f64 + 0.5);
            }
        }
        for (bx, by, bz) in tnt_chain {
            let fuse = 10 + self.rng.next_int_bound(21);
            self.pending_tnt.push((bx, by, bz, fuse));
        }
    }

    /// Ignite TNT at a cell (mirrors `BlockTNT.onBlockDestroyedByPlayer` +
    /// `BlockFire.tryToCatchBlockOnFire` for id 46): the block vanishes at
    /// once (no drop) and the radius-4 blast lands when the fuse burns out.
    /// Hand-lit fuses run 80 ticks like `EntityTNTPrimed`; chained ones
    /// pass an explicit short fuse.
    pub fn ignite_tnt(&mut self, x: i32, y: i32, z: i32, fuse: i32) {
        if self.get_block_id(x, y, z) == 46 {
            self.apply_set_notify(x, y, z, 0);
        }
        self.pending_tnt.push((x, y, z, fuse));
    }

    /// Tick primed TNT fuses; expired ones detonate at radius 4.
    /// Call once per world tick before entity ticks.
    pub fn tick_primed_tnt(&mut self) {
        if self.pending_tnt.is_empty() {
            return;
        }
        let mut due: Vec<(f64, f64, f64)> = Vec::new();
        for entry in self.pending_tnt.iter_mut() {
            entry.3 -= 1;
            if entry.3 <= 0 {
                due.push((entry.0 as f64 + 0.5, entry.1 as f64 + 0.5, entry.2 as f64 + 0.5));
            }
        }
        self.pending_tnt.retain(|e| e.3 > 0);
        for (px, py, pz) in due {
            self.blast(px, py, pz, 4.0, None);
        }
    }

    /// Creeper blast at radius 3 (mirrors `EntityCreeper` fuse end): shared
    /// ballistics, then the creeper dies without drops.
    fn creeper_explode(&mut self, id: EntityId) {
        let (px, py, pz) = match self.entities.get(id) {
            Some(e) => (e.body().pos[0], e.body().pos[1], e.body().pos[2]),
            None => return,
        };
        self.blast(px, py, pz, CREEPER_BLAST_RADIUS, Some(id));
        if let Some(e) = self.entities.get_mut(id) {
            e.body_mut().dead = true;
        }
    }

    /// Lava-water contact (mirrors `BlockFluids.func_302_i`): when a lava
    /// cell touches water on any of the 4 sides or above, source lava
    /// (meta 0) becomes obsidian, flowing lava (meta <= 4) becomes
    /// cobblestone. Only lava cells trigger (water cells never do).
    fn fluid_lava_contact(&mut self, x: i32, y: i32, z: i32, bid: u8) {
        if !matches!(bid, 10 | 11) {
            return;
        }
        if self.get_block_id(x, y, z) != bid {
            return;
        }
        let wet = self.material_at(x + 1, y, z) == Material::WATER
            || self.material_at(x - 1, y, z) == Material::WATER
            || self.material_at(x, y, z + 1) == Material::WATER
            || self.material_at(x, y, z - 1) == Material::WATER
            || self.material_at(x, y + 1, z) == Material::WATER;
        if !wet {
            return;
        }
        let meta = self.get_block_meta(x, y, z);
        if meta == 0 {
            self.apply_set_notify(x, y, z, 49);
        } else if meta <= 4 {
            self.apply_set_notify(x, y, z, 4);
        }
    }

    /// Soil check for fire (mirrors `doesBlockAllowAttachment`: solid and
    /// movement-blocking material).
    pub(crate) fn block_allows_attachment(&self, x: i32, y: i32, z: i32) -> bool {        let bid = self.get_block_id(x, y, z);
        if bid == 0 {
            return false;
        }
        let mat = material_of(alpha_block_properties_get(bid as u32).material);
        mat.is_solid() && mat.blocks_movement()
    }

    /// Arrow tick (mirrors `EntityArrow::tick`): face init, shake decay,
    /// stuck-block tracking (1200-tick despawn, pop-out jitter), block
    /// sweep with AABB clips, entity sweep for 4 damage, then ballistic
    /// flight with yaw smoothing and water drag. Fire/cactus contacts
    /// arrive with the env-state slice.
    pub fn tick_arrow(&mut self, id: EntityId) {
        use crate::vec3d::Vec3D;
        self.entities.tick_base(id);
        let alive = match self.entities.get(id) {
            Some(Entity::Arrow(a)) if !a.body.dead => true,
            _ => return,
        };
        let _ = alive;
        // Face from motion while both prev angles are still zero.
        let (mx, my, mz, yaw, pitch, pyaw, ppitch) = match self.entities.get(id) {
            Some(Entity::Arrow(a)) => (
                a.body.motion[0], a.body.motion[1], a.body.motion[2], a.body.yaw, a.body.pitch,
                a.body.prev_yaw, a.body.prev_pitch,
            ),
            _ => return,
        };
        if ppitch == 0.0 && pyaw == 0.0 {
            let (mut fy, mut fp) = (yaw, pitch);
            let faced =
                crate::entity_misc::alpha_arrow_face_velocity(mx, my, mz, &mut fy, &mut fp);
            if faced {
                if let Some(Entity::Arrow(a)) = self.entities.get_mut(id) {
                    a.body.prev_yaw = fy;
                    a.body.yaw = fy;
                    a.body.prev_pitch = fp;
                    a.body.pitch = fp;
                }
            }
        }
        if let Some(Entity::Arrow(a)) = self.entities.get_mut(id) {
            if a.shake > 0 {
                a.shake -= 1;
            }
        }
        // Stuck handling.
        let stuck_outcome = match self.entities.get(id) {
            Some(Entity::Arrow(a)) if a.in_ground => {
                if self.get_block_id(a.tile[0], a.tile[1], a.tile[2]) as i32 == a.in_tile {
                    let old = a.ticks_in_ground;
                    if let Some(Entity::Arrow(x)) = self.entities.get_mut(id) {
                        x.ticks_in_ground = old + 1;
                    }
                    if old + 1 >= 1200 {
                        if let Some(Entity::Arrow(x)) = self.entities.get_mut(id) {
                            x.body.dead = true;
                        }
                    }
                    true // stay (dead or still stuck)
                } else {
                    false // popped out below
                }
            }
            _ => false,
        };
        if stuck_outcome {
            return;
        }
        let popped = matches!(self.entities.get(id), Some(Entity::Arrow(a)) if a.in_ground);
        if popped {
            let (jx, jy, jz) =
                (self.rng.next_double(), self.rng.next_double(), self.rng.next_double());
            if let Some(Entity::Arrow(a)) = self.entities.get_mut(id) {
                a.body.motion[0] *= jx * 0.2;
                a.body.motion[1] *= jy * 0.2;
                a.body.motion[2] *= jz * 0.2;
                a.in_ground = false;
                a.ticks_in_ground = 0;
                a.ticks_in_air = 0;
            }
        } else if let Some(Entity::Arrow(a)) = self.entities.get_mut(id) {
            a.ticks_in_air += 1;
        }
        let (pos, motion, bbox) = match self.entities.get(id) {
            Some(Entity::Arrow(a)) => (a.body.pos, a.body.motion, a.body.bounding_box.clone()),
            _ => return,
        };
        let start = Vec3D::new(pos[0], pos[1], pos[2]);
        let end = Vec3D::new(pos[0] + motion[0], pos[1] + motion[1], pos[2] + motion[2]);
        // Block sweep over the padded flight box (nearest clip wins).
        let sweep = bbox.add_coord(motion[0], motion[1], motion[2]).expand(1.0, 1.0, 1.0);
        let (min_x, min_y, min_z) = (
            floor_double(sweep.min_x), floor_double(sweep.min_y), floor_double(sweep.min_z),
        );
        let (max_x, max_y, max_z) = (
            floor_double(sweep.max_x), floor_double(sweep.max_y), floor_double(sweep.max_z),
        );
        let mut block_hit: Option<(i32, i32, i32, i32, Vec3D, f64)> = None;
        for x in min_x..=max_x {
            for y in min_y..=max_y {
                for z in min_z..=max_z {
                    let bid = self.get_block_id(x, y, z);
                    if bid == 0 {
                        continue;
                    }
                    let props = alpha_block_properties_get(bid as u32);
                    if !has_collision_box(props.block_type) || !has_collision_id(bid) {
                        continue;
                    }
                    let bb = AxisAlignedBB::get_bounding_box(
                        x as f64 + props.min_x as f64,
                        y as f64 + props.min_y as f64,
                        z as f64 + props.min_z as f64,
                        x as f64 + props.max_x as f64,
                        y as f64 + props.max_y as f64,
                        z as f64 + props.max_z as f64,
                    );
                    if let Some(hit) = bb.clip(&start, &end) {
                        let d = start.square_distance_to(&hit.hit_vec);
                        if block_hit.map(|(_, _, _, _, _, bd)| d < bd).unwrap_or(true) {
                            block_hit = Some((x, y, z, bid as i32, hit.hit_vec, d));
                        }
                    }
                }
            }
        }
        // Entity sweep (anything collidable: mobs, animals, players, boats,
        // items — arrows themselves are not; the shooter is immune while
        // the arrow is young).
        let (shooter, ticks_in_air) = match self.entities.get(id) {
            Some(Entity::Arrow(a)) => (a.shooter_id, a.ticks_in_air),
            _ => return,
        };
        let mut best = block_hit.map(|(_, _, _, _, _, d)| d).unwrap_or(f64::MAX);
        let mut entity_hit: Option<EntityId> = None;
        let mut cands: Vec<EntityId> = Vec::new();
        for oid in self.entities.alive_ids() {
            if oid == id {
                continue;
            }
            if matches!(self.entities.get(oid), Some(Entity::Arrow(_))) {
                continue;
            }
            if oid == shooter && ticks_in_air < 5 {
                continue;
            }
            if let Some(o) = self.entities.get(oid) {
                if sweep.intersects_with(&o.body().bounding_box) {
                    cands.push(oid);
                }
            }
        }
        cands.sort_unstable();
        for oid in cands {
            let expanded = match self.entities.get(oid) {
                Some(o) => o.body().bounding_box.expand(0.3, 0.3, 0.3),
                None => continue,
            };
            if let Some(hit) = expanded.clip(&start, &end) {
                let d = start.square_distance_to(&hit.hit_vec);
                if d < best {
                    best = d;
                    entity_hit = Some(oid);
                }
            }
        }
        if let Some(v) = entity_hit {
            let shooter_living = match self.entities.get(shooter) {
                Some(Entity::Mob(_)) | Some(Entity::Animal(_)) | Some(Entity::Player(_)) => {
                    Some(shooter)
                }
                _ => None,
            };
            match self.entities.get(v) {
                Some(Entity::Boat(_)) => {
                    self.damage_boat(v, 4);
                }
                Some(Entity::Mob(_)) | Some(Entity::Animal(_)) | Some(Entity::Player(_)) => {
                    self.attack_living(v, 4, shooter_living);
                }
                _ => {}
            }
            if let Some(Entity::Arrow(a)) = self.entities.get_mut(id) {
                a.body.dead = true;
            }
            return;
        }
        if let Some((hx, hy, hz, bid, hit_vec, _)) = block_hit {
            if let Some(Entity::Arrow(a)) = self.entities.get_mut(id) {
                a.tile = [hx, hy, hz];
                a.in_tile = bid;
                a.body.set_position(hit_vec.x_coord, hit_vec.y_coord, hit_vec.z_coord);
                a.in_ground = true;
                a.shake = 7;
            }
            return;
        }
        // Free flight with yaw smoothing and drag.
        let (mx, my, mz) = match self.entities.get(id) {
            Some(Entity::Arrow(a)) => (a.body.motion[0], a.body.motion[1], a.body.motion[2]),
            _ => return,
        };
        let horizontal = sqrt_float((mx * mx + mz * mz) as f32);
        let mut yaw = (mx.atan2(mz) * 180.0 / std::f64::consts::PI) as f32;
        let mut pitch = (my.atan2(horizontal as f64) * 180.0 / std::f64::consts::PI) as f32;
        let (mut prev_yaw, mut prev_pitch) = match self.entities.get(id) {
            Some(Entity::Arrow(a)) => (a.body.prev_yaw, a.body.prev_pitch),
            _ => return,
        };
        while pitch - prev_pitch < -180.0 {
            prev_pitch -= 360.0;
        }
        while pitch - prev_pitch >= 180.0 {
            prev_pitch -= 360.0;
        }
        while yaw - prev_yaw < -180.0 {
            prev_yaw -= 360.0;
        }
        while yaw - prev_yaw >= 180.0 {
            prev_yaw -= 360.0;
        }
        pitch = prev_pitch + (pitch - prev_pitch) * 0.2;
        yaw = prev_yaw + (yaw - prev_yaw) * 0.2;
        // Water drag (local probe; the env slice will own `in_water`).
        let probe = match self.entities.get(id) {
            Some(Entity::Arrow(a)) => a.body.bounding_box.expand(0.0, -0.4, 0.0),
            _ => return,
        };
        let mut in_water = false;
        for x in floor_double(probe.min_x)..=floor_double(probe.max_x) {
            for y in floor_double(probe.min_y)..=floor_double(probe.max_y) {
                for z in floor_double(probe.min_z)..=floor_double(probe.max_z) {
                    if self.material_at(x, y, z) == Material::WATER {
                        in_water = true;
                        break;
                    }
                }
                if in_water {
                    break;
                }
            }
            if in_water {
                break;
            }
        }
        let drag = if in_water { 0.8f32 } else { 0.99f32 };
        if let Some(Entity::Arrow(a)) = self.entities.get_mut(id) {
            a.body.prev_yaw = prev_yaw;
            a.body.prev_pitch = prev_pitch;
            a.body.yaw = yaw;
            a.body.pitch = pitch;
            a.body.motion[0] = mx * drag as f64;
            a.body.motion[1] = my * drag as f64 - 0.03;
            a.body.motion[2] = mz * drag as f64;
            let (px, py, pz) = (a.body.pos[0] + mx, a.body.pos[1] + my, a.body.pos[2] + mz);
            // NOTE: C++ integrates the pre-drag motion, then damps.
            a.body.set_position(px, py, pz);
        }
    }
}

// Native spawner bridge: the existing `spawn_pass` drivers stay
// untouched; these shims feed them the native world's RNG, chunk map,
// and table through a thread-local pointer (the established `*World`
// bridge pattern). Draws come from the world's own `JavaRandom`, so the
// native stream is deterministic per seed but independent of the C++
// mt19937 stream. No pending queue exists natively: inserts land
// directly in the table, so no live-pointer workaround is needed.
thread_local! {
    static SPAWN_WORLD: std::cell::Cell<*mut World> = std::cell::Cell::new(std::ptr::null_mut());
    static SPAWN_HOSTILE: std::cell::Cell<bool> = std::cell::Cell::new(true);
}

fn spawn_next_int(bound: i32) -> i32 {
    if bound <= 0 {
        return 0;
    }
    SPAWN_WORLD.with(|w| unsafe {
        // SAFETY: the spawn driver sets this from a live `&mut World` and
        // clears it right after; null (no driver) is checked below.
        let world = w.get();
        if world.is_null() {
            return 0;
        }
        (*world).rng.next_int_bound(bound)
    })
}

fn spawn_next_uniform_float(lo: f32, hi: f32) -> f32 {
    SPAWN_WORLD.with(|w| unsafe {
        // SAFETY: the spawn driver sets this from a live `&mut World` and
        // clears it right after; null (no driver) is checked below.
        let world = w.get();
        if world.is_null() {
            return lo;
        }
        lo + (hi - lo) * (*world).rng.next_float()
    })
}

fn spawn_chunk_exists(x: i32, z: i32) -> bool {
    SPAWN_WORLD.with(|w| unsafe {
        // SAFETY: the spawn driver sets this from a live `&mut World` and
        // clears it right after; null (no driver) is checked below.
        let world = w.get();
        if world.is_null() {
            return false;
        }
        (*world).has_chunk(x, z)
    })
}

fn spawn_is_solid(x: i32, y: i32, z: i32) -> bool {
    SPAWN_WORLD.with(|w| unsafe {
        // SAFETY: the spawn driver sets this from a live `&mut World` and
        // clears it right after; null (no driver) is checked below.
        let world = w.get();
        if world.is_null() {
            return false;
        }
        (*world).is_solid(x, y, z)
    })
}

fn spawn_is_air(x: i32, y: i32, z: i32) -> bool {
    SPAWN_WORLD.with(|w| unsafe {
        // SAFETY: the spawn driver sets this from a live `&mut World` and
        // clears it right after; null (no driver) is checked below.
        let world = w.get();
        if world.is_null() {
            return false;
        }
        is_air_material((*world).get_block_id(x, y, z))
    })
}

fn spawn_is_liquid(x: i32, y: i32, z: i32) -> bool {
    SPAWN_WORLD.with(|w| unsafe {
        // SAFETY: the spawn driver sets this from a live `&mut World` and
        // clears it right after; null (no driver) is checked below.
        let world = w.get();
        if world.is_null() {
            return false;
        }
        (*world).material_at(x, y, z).is_liquid()
    })
}

fn spawn_try_spawn(
    kind: u8,
    fx: f32,
    fy: f32,
    fz: f32,
    yaw: f32,
    out_max_in_chunk: &mut i32,
) -> i32 {
    SPAWN_WORLD.with(|w| unsafe {
        // SAFETY: the spawn driver sets this from a live `&mut World` and
        // clears it right after; null (no driver) is checked below.
        let world = w.get();
        if world.is_null() {
            return -1;
        }
        let world = &mut *world;
        let hostile = SPAWN_HOSTILE.with(|h| h.get());
        let id = world.entities.alloc_id();
        if hostile {
            let mkind = match kind {
                0 => MobKind::Spider,
                1 => MobKind::Zombie,
                2 => MobKind::Skeleton,
                _ => MobKind::Creeper,
            };
            let mut m = crate::entity_table::MobEnt::new(id, mkind);
            m.living.body.set_position(fx as f64, fy as f64, fz as f64);
            m.living.body.yaw = yaw;
            world.entities.insert(Entity::Mob(m));
            if !world.spawner_mob_ok(id) {
                world.entities.remove(id);
                return -1;
            }
        } else {
            let akind = match kind {
                0 => AnimalKind::Sheep,
                1 => AnimalKind::Pig,
                2 => AnimalKind::Chicken,
                _ => AnimalKind::Cow,
            };
            let mut a = crate::entity_table::AnimalEnt::new(id, akind);
            a.living.body.set_position(fx as f64, fy as f64, fz as f64);
            a.living.body.yaw = yaw;
            // The C++ chicken ctor rolls the egg clock at construction,
            // before the spawn check below (draw consumed even on reject).
            if akind == AnimalKind::Chicken {
                a.egg_timer = 6000 + world.rng.next_int_bound(6000);
            }
            world.entities.insert(Entity::Animal(a));
            if !world.spawner_animal_ok(id) {
                world.entities.remove(id);
                return -1;
            }
        }
        *out_max_in_chunk = 4;
        id
    })
}

fn spawn_jockey(fx: f32, fy: f32, fz: f32, yaw: f32, host_id: i32) -> bool {
    SPAWN_WORLD.with(|w| unsafe {
        // SAFETY: the spawn driver sets this from a live `&mut World` and
        // clears it right after; null (no driver) is checked below.
        let world = w.get();
        if world.is_null() {
            return false;
        }
        let world = &mut *world;
        if world.entities.get(host_id).is_none() {
            return false;
        }
        let id = world.entities.alloc_id();
        let mut m = crate::entity_table::MobEnt::new(id, MobKind::Skeleton);
        m.living.body.set_position(fx as f64, fy as f64, fz as f64);
        m.living.body.yaw = yaw;
        world.entities.insert(Entity::Mob(m));
        world.entities.mount(id, Some(host_id));
        true
    })
}

/// Nest-safe tick-bridge scope: runs `f` with the bridge pointing at
/// `world`, restoring the previous pointer after (unlike `TickGuard`,
/// which always clears).
pub(crate) fn with_tick_bridge<T>(world: *mut World, f: impl FnOnce() -> T) -> T {
    struct Restore {
        prev: *mut World,
    }
    impl Drop for Restore {
        fn drop(&mut self) {
            TICK_WORLD.with(|t| t.set(self.prev));
        }
    }
    let prev = TICK_WORLD.with(|t| {
        let p = t.get();
        t.set(world);
        p
    });
    let _restore = Restore { prev };
    f()
}

/// Shared tick table for cross-bridge use (canStay routing from item
/// verbs, drops from harvests).
pub(crate) fn tick_table_ref() -> &'static BlockTickWorld {
    &TICK_TABLE
}

fn spawner_table() -> crate::mob_spawning::SpawnerWorld {
    crate::mob_spawning::SpawnerWorld {
        next_int: Some(spawn_next_int),
        next_uniform_float: Some(spawn_next_uniform_float),
        chunk_exists: Some(spawn_chunk_exists),
        is_solid: Some(spawn_is_solid),
        is_air: Some(spawn_is_air),
        is_liquid: Some(spawn_is_liquid),
        try_spawn: Some(spawn_try_spawn),
        spawn_jockey: Some(spawn_jockey),
    }
}

impl World {
    /// Mob spawn fitness (mirrors `EntityMob::getCanSpawnHere`): dark
    /// enough (two unconditional RNG draws like C++), collision-free, and
    /// dry.
    fn spawner_mob_ok(&mut self, id: EntityId) -> bool {
        let (px, min_y, pz, bbox) = match self.entities.get(id) {
            Some(e) => (e.body().pos[0], e.body().bounding_box.min_y, e.body().pos[2], e.body().bounding_box.clone()),
            None => return false,
        };
        let (x, y, z) = (floor_double(px), floor_double(min_y), floor_double(pz));
        if self.saved_light_value(0, x, y, z) as i32 > self.rng.next_int_bound(32) {
            return false;
        }
        if self.block_light_value(x, y, z) as i32 > self.rng.next_int_bound(8) {
            return false;
        }
        self.colliding_boxes(&bbox).is_empty() && !self.touching_liquid(id)
    }

    /// Animal spawn fitness (mirrors `EntityAnimals::getCanSpawnHere`):
    /// grass below, bright, collision-free, and dry. No RNG draws.
    fn spawner_animal_ok(&mut self, id: EntityId) -> bool {
        let (px, min_y, pz, bbox) = match self.entities.get(id) {
            Some(e) => (e.body().pos[0], e.body().bounding_box.min_y, e.body().pos[2], e.body().bounding_box.clone()),
            None => return false,
        };
        let (x, y, z) = (floor_double(px), floor_double(min_y), floor_double(pz));
        if self.get_block_id(x, y - 1, z) != GRASS_BLOCK_ID {
            return false;
        }
        if self.block_light_value(x, y, z) <= 8 {
            return false;
        }
        self.colliding_boxes(&bbox).is_empty() && !self.touching_liquid(id)
    }

    /// Player anchor positions for the spawn passes (mirrors
    /// `gatherPlayerPositions`: every joined player, dead or not).
    fn spawn_anchors(&self) -> (Vec<f64>, Vec<f64>, Vec<f64>) {
        let mut rows: Vec<(EntityId, [f64; 3])> = Vec::new();
        for oid in self.entities.all_ids() {
            if let Some(Entity::Player(p)) = self.entities.get(oid) {
                rows.push((oid, p.living.body.pos));
            }
        }
        rows.sort_by_key(|(oid, _)| *oid);
        let mut xs = Vec::with_capacity(rows.len());
        let mut ys = Vec::with_capacity(rows.len());
        let mut zs = Vec::with_capacity(rows.len());
        for (_, pos) in rows {
            xs.push(pos[0]);
            ys.push(pos[1]);
            zs.push(pos[2]);
        }
        (xs, ys, zs)
    }

    /// Hostile spawn pass (mirrors `World::spawnHostileMobs`).
    pub fn spawn_hostile_mobs(&mut self) -> i32 {
        if !self.spawn_monsters {
            return 0;
        }
        let (px, py, pz) = self.spawn_anchors();
        let count = self.entities.count_mobs() as i32;
        let (sx, sy, sz) = (self.spawn[0], self.spawn[1], self.spawn[2]);
        SPAWN_WORLD.with(|w| w.set(self as *mut World));
        SPAWN_HOSTILE.with(|h| h.set(true));
        let table = spawner_table();
        let n = crate::mob_spawning::rust_world_spawn_hostile(
            &table, &px, &py, &pz, count, sx, sy, sz, WORLD_HEIGHT,
        );
        SPAWN_WORLD.with(|w| w.set(std::ptr::null_mut()));
        n
    }

    /// Passive spawn pass (mirrors `World::spawnPassiveMobs`).
    pub fn spawn_passive_mobs(&mut self) -> i32 {
        if !self.spawn_animals {
            return 0;
        }
        let (px, py, pz) = self.spawn_anchors();
        let count = self.entities.count_animals() as i32;
        let (sx, sy, sz) = (self.spawn[0], self.spawn[1], self.spawn[2]);
        SPAWN_WORLD.with(|w| w.set(self as *mut World));
        SPAWN_HOSTILE.with(|h| h.set(false));
        let table = spawner_table();
        let n = crate::mob_spawning::rust_world_spawn_passive(
            &table, &px, &py, &pz, count, sx, sy, sz, WORLD_HEIGHT,
        );
        SPAWN_WORLD.with(|w| w.set(std::ptr::null_mut()));
        n
    }

    /// Item pickup sweep (mirrors the in-loop pickup: ready items within
    /// the expanded player box merge via `player_add_item`; packets are
    /// the network slice's). Two-phase instead of interleaved, which is
    /// equivalent here: fresh drops always carry a pickup delay, and
    /// native players hold still between network ticks.
    fn pickup_items(&mut self) {
        let mut items: Vec<EntityId> = Vec::new();
        for oid in self.entities.alive_ids() {
            if let Some(Entity::Item(e)) = self.entities.get(oid) {
                if e.pickup_delay <= 0 {
                    items.push(oid);
                }
            }
        }
        items.sort_unstable();
        let mut players: Vec<EntityId> = Vec::new();
        for oid in self.entities.alive_ids() {
            if self.target_alive(oid) {
                players.push(oid);
            }
        }
        players.sort_unstable();
        for iid in items {
            if self.entities.get(iid).map(|e| e.body().dead).unwrap_or(true) {
                continue;
            }
            for pid in &players {
                if self.entities.get(iid).map(|e| e.body().dead).unwrap_or(true) {
                    break;
                }
                let hit = match (self.entities.get(iid), self.entities.get(*pid)) {
                    (Some(Entity::Item(it)), Some(Entity::Player(p))) => {
                        let ex = p.living.body.width as f64 / 2.0 + 1.0 + 0.125;
                        let min_y = p.living.body.pos[1] - 0.25;
                        let max_y = p.living.body.pos[1] + p.living.body.height as f64;
                        (p.living.body.pos[0] - it.body.pos[0]).abs() < ex
                            && it.body.pos[1] < max_y
                            && it.body.pos[1] + 0.25 > min_y
                            && (p.living.body.pos[2] - it.body.pos[2]).abs() < ex
                    }
                    _ => false,
                };
                if !hit {
                    continue;
                }
                let (item_id, count, damage) = match self.entities.get(iid) {
                    Some(Entity::Item(e)) => (e.item_id, e.count, e.damage),
                    _ => continue,
                };
                let rem = self.player_add_item(
                    *pid,
                    crate::inventory::FfiItemStack {
                        stack_size: count,
                        animations_to_go: 0,
                        item_id,
                        item_damage: damage,
                    },
                );
                if rem < count {
                    if rem <= 0 {
                        if let Some(e) = self.entities.get_mut(iid) {
                            e.body_mut().dead = true;
                        }
                        self.item_pickups.push((iid, *pid));
                    } else if let Some(Entity::Item(e)) = self.entities.get_mut(iid) {
                        e.count = rem;
                    }
                }
                if self.entities.get(iid).map(|e| e.body().dead).unwrap_or(true) {
                    break;
                }
            }
        }
    }

    /// Server tick (mirrors `World::tick` minus chunk I/O, lighting,
    /// chest/sign-tile behavior, and packets): clock, spawners, furnace
    /// tiles, scheduled and random block ticks, entity dispatch on a
    /// snapshot (mid-tick spawns wait a tick like C++), item pickup,
    /// dead-row purge, and periodic unload.
    pub fn tick_world(&mut self) {
        self.time += 1;
        if self.spawn_monsters {
            self.spawn_hostile_mobs();
        }
        if self.spawn_animals {
            self.spawn_passive_mobs();
        }
        self.tick_furnaces();
        self.tick_primed_tnt();
        self.process_scheduled_ticks();
        self.random_block_ticks();
        let mut ids = self.entities.alive_ids();
        ids.sort_unstable();
        for id in ids {
            if self.entities.get(id).map(|e| e.body().dead).unwrap_or(true) {
                continue;
            }
            match self.entities.get(id) {
                Some(Entity::Item(_)) => self.tick_item(id),
                Some(Entity::Falling(_)) => self.tick_falling(id),
                Some(Entity::Boat(_)) => self.tick_boat(id),
                Some(Entity::Arrow(_)) => self.tick_arrow(id),
                Some(Entity::Mob(_)) => self.tick_mob(id),
                Some(Entity::Animal(_)) => self.tick_animal(id),
                Some(Entity::Player(_)) => self.tick_player(id),
                None => {}
            }
        }
        self.pickup_items();
        self.entities.purge_dead();
        self.unload_chunks();
    }

    /// Native furnace ticking (mirrors the `World::tick` tile-entity pass
    /// over `TileEntityFurnace::updateEntity`): every furnace tile looks
    /// up fuel from its own slot and runs the shared core, then a burn
    /// flip swaps the block 61 <-> 62 preserving metadata (mirrors
    /// `updateFurnaceBlockState`, whose no-notify set keeps the tile
    /// alive — here tiles live outside chunks, so any plain set is safe).
    /// Swapped cells accumulate in `furnace_updates` for the server tick
    /// to broadcast (the C++ `markBlockNeedsUpdate`).
    pub fn tick_furnaces(&mut self) {
        let cells: Vec<(i32, i32, i32)> = self.tiles.keys().copied().collect();
        for (x, y, z) in cells {
            let ticked = match self.tiles.get_mut(&(x, y, z)) {
                Some(TileData::Furnace(state)) => {
                    crate::tile_entity_furnace::furnace_tick_native(state)
                }
                _ => continue,
            };
            if !ticked.needs_block_update {
                continue;
            }
            let burning = matches!(
                self.tiles.get(&(x, y, z)),
                Some(TileData::Furnace(state)) if state.burn_time > 0
            );
            let meta = self.get_block_meta(x, y, z);
            let new_id = if burning { 62 } else { 61 };
            if self.set_block_id(x, y, z, new_id) {
                self.set_block_meta(x, y, z, meta);
                self.furnace_updates.push([x, y, z]);
            }
        }
    }
}

// Native block-tick bridge: the `block_*` drivers stay untouched; these
// shims feed them the native world's blocks, light, schedule, RNG, and
// spawns through a thread-local pointer (the established `*World`
// pattern). Set-variants apply id/meta plus the C++ added/neighbor
// routing; client packets (markBlockNeedsUpdate) are no-ops. Container
// removal scatter is skipped until the tile table slice (no tile state
// exists natively yet). The native RNG stream is independent of the C++
// mt19937 stream (same ranges, deterministic per seed).
thread_local! {
    static TICK_WORLD: std::cell::Cell<*mut World> = std::cell::Cell::new(std::ptr::null_mut());
}

/// RAII guard for the tick bridge pointer (restores null on scope exit).
struct TickGuard;
impl TickGuard {
    fn enter(world: *mut World) -> Self {
        TICK_WORLD.with(|t| t.set(world));
        TickGuard
    }
}
impl Drop for TickGuard {
    fn drop(&mut self) {
        TICK_WORLD.with(|t| t.set(std::ptr::null_mut()));
    }
}

fn with_tick_world<T>(f: impl FnOnce(&mut World) -> T, dflt: T) -> T {
    // SAFETY: `TickGuard`/`with_tick_bridge` point at a live `&mut World`
    // and restore/clear afterwards; null (no driver) returns the default.
    TICK_WORLD.with(|t| unsafe {
        let p = t.get();
        if p.is_null() {
            dflt
        } else {
            f(&mut *p)
        }
    })
}

fn tick_next_int(bound: i32) -> i32 {
    if bound <= 0 {
        return 0;
    }
    with_tick_world(|w| w.rng.next_int_bound(bound), 0)
}

fn tick_next_float01() -> f32 {
    with_tick_world(|w| w.rng.next_float(), 0.0)
}

fn tick_next_u64() -> u64 {
    with_tick_world(|w| w.rng.next_long() as u64, 0)
}

fn tick_get_id(x: i32, y: i32, z: i32) -> u8 {
    with_tick_world(|w| w.get_block_id(x, y, z), 0)
}

fn tick_get_id_nc(x: i32, y: i32, z: i32) -> u8 {
    // Same map: the native world never force-loads chunks.
    with_tick_world(|w| w.get_block_id(x, y, z), 0)
}

fn tick_get_meta(x: i32, y: i32, z: i32) -> u8 {
    with_tick_world(|w| w.get_block_meta(x, y, z), 0)
}

fn tick_set(x: i32, y: i32, z: i32, id: u8) {
    with_tick_world(|w| w.set_block_id(x, y, z, id), false);
}

fn tick_set_meta(x: i32, y: i32, z: i32, meta: u8) {
    with_tick_world(|w| w.set_block_meta(x, y, z, meta), false);
}

fn tick_set_notify(x: i32, y: i32, z: i32, id: u8) {
    with_tick_world(|w| w.apply_set_notify(x, y, z, id), false);
}

fn tick_set_update(x: i32, y: i32, z: i32, id: u8) {
    // setBlockAndUpdate: plain set plus the (client) mark.
    with_tick_world(|w| w.set_block_id(x, y, z, id), false);
}

fn tick_set_meta_notify(x: i32, y: i32, z: i32, id: u8, meta: u8) {
    with_tick_world(|w| w.apply_set_meta_notify(x, y, z, id, meta), false);
}

fn tick_set_and_meta(x: i32, y: i32, z: i32, id: u8, meta: u8) {
    with_tick_world(
        |w| {
            w.set_block_id(x, y, z, id);
            w.set_block_meta(x, y, z, meta);
        },
        (),
    );
}

fn tick_light(x: i32, y: i32, z: i32) -> i32 {
    with_tick_world(|w| w.block_light_value(x, y, z) as i32, 0)
}

fn tick_sky(x: i32, y: i32, z: i32) -> bool {
    with_tick_world(|w| w.can_see_sky(x, y, z), false)
}

fn tick_attach_world(x: i32, y: i32, z: i32) -> bool {
    with_tick_world(|w| w.block_allows_attachment(x, y, z), false)
}

fn tick_attach_torch(x: i32, y: i32, z: i32) -> bool {
    // Solid material plus collidable (everything but fluids).
    with_tick_world(
        |w| {
            if w.get_block_id(x, y, z) == 0 {
                return false;
            }
            let m = w.material_at(x, y, z);
            m.is_solid() && !m.is_liquid()
        },
        false,
    )
}

fn tick_solid(x: i32, y: i32, z: i32) -> bool {
    // Material-solid like blockTickIsSolid (NOT the id list).
    with_tick_world(
        |w| w.get_block_id(x, y, z) != 0 && w.material_at(x, y, z).is_solid(),
        false,
    )
}

fn tick_solid_nc(x: i32, y: i32, z: i32) -> bool {
    with_tick_world(
        |w| w.get_block_id(x, y, z) != 0 && w.material_at(x, y, z).is_solid(),
        false,
    )
}

fn tick_water_lava(x: i32, y: i32, z: i32) -> bool {
    // Literal mirror (air and fire count as water-or-lava in C++!).
    with_tick_world(
        |w| {
            let bid = w.get_block_id(x, y, z);
            if bid == 0 || bid == FIRE_BLOCK_ID {
                return true;
            }
            matches!(w.material_at(x, y, z), Material::WATER | Material::LAVA)
        },
        false,
    )
}

fn tick_water(x: i32, y: i32, z: i32) -> bool {
    with_tick_world(|w| w.material_at(x, y, z) == Material::WATER, false)
}

fn tick_registered(id: u8) -> bool {
    // Mirrors initBlocks: every non-air material gets a Block instance.
    with_tick_world(|_| id != 0 && !is_air_material(id), false)
}

fn tick_collidable(x: i32, y: i32, z: i32) -> bool {
    with_tick_world(
        |w| {
            let bid = w.get_block_id(x, y, z);
            bid != 0
                && alpha_block_properties_get(bid as u32).block_type != BlockType::Fluid as u8
                && has_collision_box(alpha_block_properties_get(bid as u32).block_type)
                && has_collision_id(bid)
        },
        false,
    )
}

fn tick_schedule(x: i32, y: i32, z: i32, block_id: u8, delay: i32) {
    with_tick_world(|w| w.schedule_block_update(x, y, z, block_id, delay), ());
}

fn tick_mark(_x: i32, _y: i32, _z: i32) {
    // markBlockNeedsUpdate only addresses clients.
}

fn tick_notify(x: i32, y: i32, z: i32, _block_id: u8) {
    with_tick_world(|w| w.notify_neighbors_of(x, y, z), ());
}

fn tick_spawn_drop(
    item_id: i32,
    count: i32,
    damage: i32,
    fx: f64,
    fy: f64,
    fz: f64,
    spread: f64,
    up: f64,
) {
    with_tick_world(
        |w| {
            let eid = w.spawn_item_entity(item_id, count, damage, fx, fy, fz);
            let (dx, dz) = (w.rng.next_double(), w.rng.next_double());
            if let Some(Entity::Item(e)) = w.entities.get_mut(eid) {
                e.body.motion[0] = -spread + 2.0 * spread * dx;
                e.body.motion[1] = up;
                e.body.motion[2] = -spread + 2.0 * spread * dz;
            }
        },
        (),
    );
}

fn tick_spawn_falling(block_id: u8, fx: f64, fy: f64, fz: f64) {
    with_tick_world(
        |w| {
            let id = w.entities.alloc_id();
            let mut b = Body::new(id, 0.98, 0.98, 0.49);
            b.set_position(fx, fy, fz);
            w.entities.insert(Entity::Falling(crate::entity_table::FallingEnt {
                body: b,
                block_id: block_id as i32,
                fall_time: 0,
            }));
        },
        (),
    );
}

fn tick_drop_occupant(x: i32, y: i32, z: i32) {
    // dropBlockAsItem with chance 1.0. The C++ needs-server guard is a
    // client check; the native world is always the server side.
    with_tick_world(|w| w.drop_block_as_item(x, y, z), ());
}

fn tick_detonate(x: i32, y: i32, z: i32) {
    // Java BlockFire.tryToCatchBlockOnFire for id 46: the TNT block is
    // replaced by fire/air above, then onBlockDestroyedByPlayer primes it.
    with_tick_world(|w| w.ignite_tnt(x, y, z, 80), ());
}

// Tree-generation accessor over the same bridge pointer.
fn tree_get_id(x: i32, y: i32, z: i32) -> u8 {
    with_tick_world(|w| w.get_block_id(x, y, z), 0)
}

fn tree_set_id(x: i32, y: i32, z: i32, id: u8) {
    // C++ tree gen uses the plain (non-notify) setBlock.
    with_tick_world(|w| w.set_block_id(x, y, z, id), false);
}

fn tree_get_meta(x: i32, y: i32, z: i32) -> u8 {
    with_tick_world(|w| w.get_block_meta(x, y, z), 0)
}

fn tree_set_meta(x: i32, y: i32, z: i32, meta: u8) {
    with_tick_world(|w| w.set_block_meta(x, y, z, meta), false);
}

fn tree_attach(x: i32, y: i32, z: i32) -> bool {
    // Block::allowsAttachmentArr: registered plus the allowsAttachment flag.
    with_tick_world(
        |w| {
            let bid = w.get_block_id(x, y, z);
            bid != 0
                && !is_air_material(bid)
                && alpha_block_properties_get(bid as u32).allows_attachment
        },
        false,
    )
}

fn tree_solid(x: i32, y: i32, z: i32) -> bool {
    with_tick_world(|w| w.is_solid(x, y, z), false)
}

fn tree_height(x: i32, z: i32) -> i32 {
    with_tick_world(|w| w.get_height_value(x, z), 0)
}

static TICK_TABLE: BlockTickWorld = BlockTickWorld {
    next_int: Some(tick_next_int),
    next_float01: Some(tick_next_float01),
    next_u64: Some(tick_next_u64),
    get_block_id: Some(tick_get_id),
    get_block_id_nc: Some(tick_get_id_nc),
    get_block_meta: Some(tick_get_meta),
    set_block: Some(tick_set),
    set_block_meta: Some(tick_set_meta),
    set_block_notify: Some(tick_set_notify),
    set_block_update: Some(tick_set_update),
    set_block_meta_notify: Some(tick_set_meta_notify),
    set_block_and_meta: Some(tick_set_and_meta),
    get_block_light: Some(tick_light),
    can_see_sky: Some(tick_sky),
    attach_world: Some(tick_attach_world),
    attach_torch: Some(tick_attach_torch),
    is_solid: Some(tick_solid),
    is_solid_nc: Some(tick_solid_nc),
    is_water_or_lava: Some(tick_water_lava),
    is_water: Some(tick_water),
    block_registered: Some(tick_registered),
    collidable_box: Some(tick_collidable),
    schedule_update: Some(tick_schedule),
    mark_update: Some(tick_mark),
    notify_neighbors: Some(tick_notify),
    spawn_drop: Some(tick_spawn_drop),
    spawn_falling: Some(tick_spawn_falling),
    drop_occupant: Some(tick_drop_occupant),
};

fn fire_table() -> FireWorld<'static> {
    FireWorld { base: &TICK_TABLE, detonate_tnt: Some(tick_detonate) }
}

fn tree_accessor() -> crate::decorators::WorldAccessor {
    crate::decorators::WorldAccessor {
        get_block_id: tree_get_id,
        set_block_id: tree_set_id,
        get_block_meta: tree_get_meta,
        set_block_meta: tree_set_meta,
        allows_attachment: tree_attach,
        is_block_solid: tree_solid,
        get_height_value: tree_height,
    }
}

/// Alpha wheat/seeds item ids for the crops drivers.
const WHEAT_ITEM_ID: i32 = 296;
const SEEDS_ITEM_ID: i32 = 295;
/// Sign drop item id (BlockSign::idDropped).
const SIGN_ITEM_ID: i32 = 323;

impl World {
    /// (drop_id, drop_count, drop_damage) mirroring the Java idDropped /
    /// quantityDropped call sites. No damageDropped overrides exist, so
    /// damage is always 0.
    pub(crate) fn native_drop_ids(bid: u8) -> (i32, i32, i32) {
        let p = alpha_block_properties_get(bid as u32);
        (if p.id_dropped != 0 { p.id_dropped } else { bid as i32 }, p.quantity_dropped, 0)
    }

    /// Placement write (mirrors `setBlockWithNotify` minus removal scatter
    /// and client packets): set the id, run the added-router, notify
    /// Placement write (mirrors `setBlockWithNotify`): run the removal hook
    /// for the old id (container scatter + tile clear, Java
    /// `onBlockRemoval`), then set, run the added-router, notify neighbors.
    pub(crate) fn apply_set_notify(&mut self, x: i32, y: i32, z: i32, id: u8) -> bool {
        let old = self.get_block_id(x, y, z);
        if old != 0 && old != id {
            self.block_removed(x, y, z, old);
        }
        if !self.set_block_id(x, y, z, id) {
            return false;
        }
        self.block_added(x, y, z, id);
        self.notify_neighbors_of(x, y, z);
        true
    }

    /// Id+meta placement write (mirrors `setBlockAndMetadataWithNotify`
    /// the same way).
    pub(crate) fn apply_set_meta_notify(&mut self, x: i32, y: i32, z: i32, id: u8, meta: u8) -> bool {
        if y < 0 || y >= WORLD_HEIGHT {
            return false;
        }
        let old = self.get_block_id(x, y, z);
        if old != 0 && old != id {
            self.block_removed(x, y, z, old);
        }
        self.set_block_id(x, y, z, id);
        self.set_block_meta(x, y, z, meta);
        self.block_added(x, y, z, id);
        self.notify_neighbors_of(x, y, z);
        true
    }

    /// Removal hook (mirrors `Block.onBlockRemoval` for containers): scatter
    /// chest/furnace contents, drop the tile row. Safe to call when no tile
    /// exists (scatter is a no-op then).
    fn block_removed(&mut self, x: i32, y: i32, z: i32, old: u8) {
        if matches!(old, 54 | 61 | 62 | 63 | 68) {
            self.scatter_container_tile(x, y, z);
            self.tiles.remove(&(x, y, z));
        } else {
            self.tiles.remove(&(x, y, z));
        }
    }

    /// Neighbor fan-out (mirrors `notifyBlocksOfNeighborChange`).
    fn notify_neighbors_of(&mut self, x: i32, y: i32, z: i32) {
        const OFF: [[i32; 3]; 6] =
            [[-1, 0, 0], [1, 0, 0], [0, -1, 0], [0, 1, 0], [0, 0, -1], [0, 0, 1]];
        for o in OFF {
            self.neighbor_changed(x + o[0], y + o[1], z + o[2]);
        }
    }

    /// Placement router (mirrors the `onBlockAdded` overrides, including
    /// container tile creation).
    fn block_added(&mut self, x: i32, y: i32, z: i32, bid: u8) {
        match bid {
            54 => {
                self.tiles.entry((x, y, z)).or_insert_with(|| {
                    TileData::Chest(crate::tile_entity_chest::chest_create())
                });
            }
            61 | 62 => {
                self.tiles.entry((x, y, z)).or_insert_with(|| {
                    TileData::Furnace(crate::tile_entity_furnace::furnace_create())
                });
            }
            63 | 68 => {
                self.tiles.entry((x, y, z)).or_insert_with(|| {
                    TileData::Sign(crate::tile_entity_sign::sign_create())
                });
            }
            _ => {}
        }
        let _guard = TickGuard::enter(self as *mut World);
        match bid {
            12 | 13 => block_sand_added(&TICK_TABLE, bid, x, y, z),
            8 | 9 | 10 | 11 => {
                let rate = with_tick_world(
                    |w| {
                        if w.material_at(x, y, z) == Material::LAVA {
                            30
                        } else {
                            5
                        }
                    },
                    5,
                );
                block_fluid_added(&TICK_TABLE, bid, rate, x, y, z);
                self.fluid_lava_contact(x, y, z, bid);
            }
            81 => block_cactus_added(&TICK_TABLE, bid, x, y, z),
            83 => block_reed_added(&TICK_TABLE, bid, x, y, z),
            50 => block_torch_added(&TICK_TABLE, bid, x, y, z),
            18 => block_leaves_added(&TICK_TABLE, bid, x, y, z),
            6 => block_sapling_added(&TICK_TABLE, bid, x, y, z),
            59 => block_crops_added(&TICK_TABLE, bid, x, y, z),
            60 => block_soil_added(&TICK_TABLE, bid, x, y, z),
            51 => block_fire_added(&fire_table(), bid, 10, x, y, z),
            _ => {}
        }
    }

    /// Neighbor router (mirrors the `onNeighborBlockChange` overrides,
    /// plus the inline sign-break check).
    fn neighbor_changed(&mut self, x: i32, y: i32, z: i32) {
        let bid = self.get_block_id(x, y, z);
        if bid == 0 || !Self::native_registered(bid) {
            return;
        }
        let _meta = self.get_block_meta(x, y, z);
        let _guard = TickGuard::enter(self as *mut World);
        match bid {
            12 | 13 => block_sand_neighbor(&TICK_TABLE, bid, x, y, z),
            8 | 9 | 10 | 11 => {
                let rate = with_tick_world(
                    |w| {
                        if w.material_at(x, y, z) == Material::LAVA {
                            30
                        } else {
                            5
                        }
                    },
                    5,
                );
                block_fluid_neighbor(&TICK_TABLE, bid, rate, x, y, z);
                self.fluid_lava_contact(x, y, z, bid);
            }
            37 | 38 => {
                let (d, q, g) = Self::native_drop_ids(bid);
                block_flower_neighbor(&TICK_TABLE, d, q, g, x, y, z);
            }
            39 | 40 => {
                let (d, q, g) = Self::native_drop_ids(bid);
                block_mushroom_neighbor(&TICK_TABLE, d, q, g, x, y, z);
            }
            50 => {
                let (d, q, g) = Self::native_drop_ids(bid);
                block_torch_neighbor(&TICK_TABLE, d, q, g, x, y, z);
            }
            78 => self.snow_neighbor(x, y, z),
            81 => {
                let (d, q, g) = Self::native_drop_ids(bid);
                block_cactus_neighbor(&TICK_TABLE, bid, d, q, g, x, y, z);
            }
            83 => {
                let (d, q, g) = Self::native_drop_ids(bid);
                block_reed_neighbor(&TICK_TABLE, bid, d, q, g, x, y, z);
            }
            18 => {
                let mut guard = with_tick_world(|w| w.leaves_guard, 0);
                block_leaves_neighbor(&TICK_TABLE, bid, bid, &mut guard, x, y, z);
                with_tick_world(|w| w.leaves_guard = guard, ());
            }
            6 => {
                let (d, q, g) = Self::native_drop_ids(bid);
                block_sapling_neighbor(&TICK_TABLE, bid, d, q, g, x, y, z);
            }
            59 => block_crops_neighbor(&TICK_TABLE, bid, bid, WHEAT_ITEM_ID, SEEDS_ITEM_ID, x, y, z),
            60 => block_soil_neighbor(&TICK_TABLE, bid, x, y, z),
            51 => block_fire_neighbor(&fire_table(), x, y, z),
            63 | 68 => {}
            _ => {}
        }
        if bid == 63 || bid == 68 {
            self.sign_neighbor(x, y, z, bid);
        }
    }

    /// Snow-layer support (mirrors `BlockSnow.func_275_g`): needs a solid
    /// attachable block below, else drops and vanishes.
    fn snow_neighbor(&mut self, x: i32, y: i32, z: i32) {
        let below = self.get_block_id(x, y - 1, z);
        let ok = below != 0
            && alpha_block_properties_get(below as u32).allows_attachment
            && material_of(alpha_block_properties_get(below as u32).material).is_solid();
        if !ok {
            self.drop_block_for(78, 0, x, y, z);
            self.apply_set_notify(x, y, z, 0);
        }
    }

    /// Registered-block check (mirrors `blocksList[id] != nullptr` via the
    /// initBlocks rule: every non-air material gets an instance).
    pub(crate) fn native_registered(bid: u8) -> bool {
        bid != 0 && !is_air_material(bid)
    }

    /// Block-as-item drop at chance 1.0 (mirrors `dropBlockAsItem` for an
    /// already-captured block id — the cell may be air by now, so the
    /// pre-removal metadata rides along for meta-sensitive drops).
    pub(crate) fn drop_block_for(&mut self, bid: u8, meta: u8, x: i32, y: i32, z: i32) {
        if bid == 0 {
            return;
        }
        let (drop, qty) = self.rolled_drop_ids(bid, meta);
        if drop <= 0 {
            return;
        }
        let _guard = TickGuard::enter(self as *mut World);
        block_base_drop(&TICK_TABLE, drop, qty, 0, x, y, z, 1.0);
    }

    /// idDropped/quantityDropped rolls that need world RNG or metadata
    /// (mirrors the Java overrides, which draw from the block-break
    /// Random): doors drop the item only from the lower half (wood 324,
    /// iron 330), gravel flints 1/10, redstone dust comes 4-5, leaves
    /// drop a sapling 1/20. Everything else is the props table.
    fn rolled_drop_ids(&mut self, bid: u8, meta: u8) -> (i32, i32) {
        match bid {
            64 | 71 => {
                if meta & 8 != 0 {
                    (0, 0)
                } else if bid == 71 {
                    (330, 1)
                } else {
                    (324, 1)
                }
            }
            13 => {
                if self.rng.next_int_bound(10) == 0 {
                    (318, 1)
                } else {
                    (13, 1)
                }
            }
            73 | 74 => (331, 4 + self.rng.next_int_bound(2)),
            18 => {
                if self.rng.next_int_bound(20) == 0 {
                    (6, 1)
                } else {
                    (0, 0)
                }
            }
            _ => {
                let (d, q, _) = Self::native_drop_ids(bid);
                (d, q)
            }
        }
    }

    /// Block-as-item drop for the live occupant (fluid wash path).
    pub(crate) fn drop_block_as_item(&mut self, x: i32, y: i32, z: i32) {
        let bid = self.get_block_id(x, y, z);
        let meta = self.get_block_meta(x, y, z);
        self.drop_block_for(bid, meta, x, y, z);
    }

    /// Container tile scatter on break (mirrors the furnace/chest
    /// `onBlockRemoval` halves, including the tile-row removal).
    /// Tile-less cells are a no-op.
    pub(crate) fn scatter_container_tile(&mut self, x: i32, y: i32, z: i32) {
        use crate::block_container::{block_chest_scatter_stack, block_furnace_scatter_stack};
        let tile = match self.tiles.get(&(x, y, z)) {
            Some(t) => *t,
            None => return,
        };
        // ScatterWorld shims draw from the world RNG and spawn directly
        // (mirroring the C++ global-RNG scatter table).
        fn sc_next_int(bound: i32) -> i32 {
            if bound <= 0 {
                return 0;
            }
            with_tick_world(|w| w.rng.next_int_bound(bound), 0)
        }
        fn sc_next_f32() -> f32 {
            with_tick_world(|w| w.rng.next_float(), 0.0)
        }
        fn sc_next_f64() -> f64 {
            with_tick_world(|w| w.rng.next_double(), 0.0)
        }
        fn sc_spawn(
            item_id: i32,
            count: i32,
            damage: i32,
            fx: f64,
            fy: f64,
            fz: f64,
            mx: f64,
            my: f64,
            mz: f64,
        ) {
            with_tick_world(
                |w| {
                    let eid = w.spawn_item_entity(item_id, count, damage, fx, fy, fz);
                    if let Some(Entity::Item(e)) = w.entities.get_mut(eid) {
                        e.body.motion = [mx, my, mz];
                    }
                },
                (),
            );
        }
        let table = crate::block_container::ScatterWorld {
            next_int: Some(sc_next_int),
            next_f32_01: Some(sc_next_f32),
            next_f64_01: Some(sc_next_f64),
            spawn_item: Some(sc_spawn),
        };
        let _guard = TickGuard::enter(self as *mut World);
        match tile {
            TileData::Furnace(s) => {
                for slot in s.slots {
                    if slot.stack_size > 0 {
                        block_furnace_scatter_stack(
                            &table,
                            slot.item_id,
                            slot.stack_size,
                            slot.item_damage,
                            x,
                            y,
                            z,
                        );
                    }
                }
            }
            TileData::Chest(s) => {
                for slot in s.slots {
                    if slot.stack_size > 0 {
                        block_chest_scatter_stack(
                            &table,
                            slot.item_id,
                            slot.stack_size,
                            slot.item_damage,
                            x,
                            y,
                            z,
                        );
                    }
                }
            }
            TileData::Sign(_) => {}
        }
        self.tiles.remove(&(x, y, z));
    }

    /// Sign support check (mirrors `BlockSign::onNeighborBlockChange`):
    /// wall signs need material-solid behind per facing, posts need solid
    /// below; otherwise drop 323 through the standard base-drop path and
    /// clear. Requires the tick bridge pointer (callers hold the guard).
    fn sign_neighbor(&mut self, x: i32, y: i32, z: i32, bid: u8) {
        let supported = if bid == 68 {
            match self.get_block_meta(x, y, z) {
                2 => self.material_at(x, y, z + 1).is_solid(),
                3 => self.material_at(x, y, z - 1).is_solid(),
                4 => self.material_at(x + 1, y, z).is_solid(),
                5 => self.material_at(x - 1, y, z).is_solid(),
                _ => false,
            }
        } else {
            self.material_at(x, y - 1, z).is_solid()
        };
        if !supported {
            block_base_drop(&TICK_TABLE, SIGN_ITEM_ID, 1, 0, x, y, z, 1.0);
            self.set_block_id(x, y, z, 0);
        }
    }

    /// Scheduled/random-tick router (mirrors the `updateTick` overrides).
    fn update_block_tick(&mut self, x: i32, y: i32, z: i32) {
        let bid = self.get_block_id(x, y, z);
        if bid == 0 {
            return;
        }
        let _guard = TickGuard::enter(self as *mut World);
        match bid {
            12 | 13 => block_sand_tick(&TICK_TABLE, bid, x, y, z),
            8 | 9 | 10 | 11 => {
                let lava = with_tick_world(|w| w.material_at(x, y, z) == Material::LAVA, false);
                block_fluid_tick(&TICK_TABLE, bid, lava, x, y, z);
            }
            37 | 38 => {
                let (d, q, g) = Self::native_drop_ids(bid);
                block_flower_tick(&TICK_TABLE, d, q, g, x, y, z);
            }
            81 => {
                let (d, q, g) = Self::native_drop_ids(bid);
                block_cactus_tick(&TICK_TABLE, bid, d, q, g, x, y, z);
            }
            83 => {
                let (d, q, g) = Self::native_drop_ids(bid);
                block_reed_tick(&TICK_TABLE, bid, d, q, g, x, y, z);
            }
            18 => {
                let mut guard = with_tick_world(|w| w.leaves_guard, 0);
                // Decayed leaves drop a sapling 1/20 (BlockLeaves);
                // the tick itself always clears the cell.
                let (did, dqty) = if self.rng.next_int_bound(20) == 0 { (6, 1) } else { (0, 0) };
                block_leaves_tick(&TICK_TABLE, bid, bid, did, dqty, 0, &mut guard, x, y, z);
                with_tick_world(|w| w.leaves_guard = guard, ());
            }
            6 => {
                let (d, q, g) = Self::native_drop_ids(bid);
                let action = block_sapling_tick(&TICK_TABLE, bid, d, q, g, x, y, z);
                drop(_guard);
                if action.kind == 1 {
                    self.grow_sapling(x, y, z, bid, action.seed);
                }
                return;
            }
            59 => block_crops_tick(&TICK_TABLE, bid, bid, WHEAT_ITEM_ID, SEEDS_ITEM_ID, x, y, z),
            60 => block_soil_tick(&TICK_TABLE, bid, x, y, z),
            51 => block_fire_tick(&fire_table(), bid, 10, x, y, z),
            50 => {
                // Torch re-seats meta 0 (Java BlockTorch.updateTick).
                if self.get_block_meta(x, y, z) == 0 {
                    drop(_guard);
                    let _g2 = TickGuard::enter(self as *mut World);
                    block_torch_added(&TICK_TABLE, bid, x, y, z);
                    return;
                }
            }
            2 => self.grass_tick(x, y, z),
            78 => {
                // Snow melts under strong block light (Java BlockSnow).
                if self.saved_light_value(1, x, y, z) > 11 {
                    self.drop_block_for(78, 0, x, y, z);
                    self.apply_set_notify(x, y, z, 0);
                }
            }
            79 => {
                // Ice melts to flowing water (Java BlockIce: light > 11-3).
                if self.saved_light_value(1, x, y, z) > 8 {
                    self.apply_set_notify(x, y, z, 9);
                }
            }
            80 => {
                if self.saved_light_value(1, x, y, z) > 11 {
                    self.drop_block_for(80, 0, x, y, z);
                    self.apply_set_notify(x, y, z, 0);
                }
            }
            74 => {
                // Glowing redstone cools back to idle (Java BlockRedstoneOre).
                self.apply_set_notify(x, y, z, 73);
            }
            _ => {}
        }
    }

    /// Grass spread/decay (mirrors `BlockGrass.updateTick`): dark + opaque
    /// cover turns to dirt (1/4 roll), bright spreads to nearby dirt.
    fn grass_tick(&mut self, x: i32, y: i32, z: i32) {
        let above_light = self.block_light_value(x, y + 1, z);
        let above_mat = self.material_at(x, y + 1, z);
        if above_light < 4 && above_mat.can_block_grass() {
            if self.rng.next_int_bound(4) != 0 {
                return;
            }
            self.apply_set_notify(x, y, z, 3);
        } else if above_light >= 9 {
            let (nx, ny, nz) = (
                x + self.rng.next_int_bound(3) - 1,
                y + self.rng.next_int_bound(5) - 3,
                z + self.rng.next_int_bound(3) - 1,
            );
            if self.get_block_id(nx, ny, nz) == 3
                && self.block_light_value(nx, ny + 1, nz) >= 4
                && !self.material_at(nx, ny + 1, nz).can_block_grass()
            {
                self.apply_set_notify(nx, ny, nz, 2);
            }
        }
    }

    /// Sapling growth (mirrors the C++ `GrowTree` tail): clear, roll the
    /// 1/10 big tree, generate through the tree accessor, restore the
    /// sapling on failure.
    fn grow_sapling(&mut self, x: i32, y: i32, z: i32, bid: u8, seed: u64) {
        self.apply_set_notify(x, y, z, 0);
        let big = self.rng.next_int_bound(10) == 0;
        let _guard = TickGuard::enter(self as *mut World);
        let acc = tree_accessor();
            let ok = if big {
                crate::generate_big_tree(&acc, seed as i64, x, y, z)
            } else {
                crate::generate_tree(&acc, seed as i64, x, y, z)
            };
        drop(_guard);
        if !ok {
            self.apply_set_notify(x, y, z, bid);
        }
    }

    /// Queue a block update (mirrors `scheduleBlockUpdate`): entries dedup
    /// by (x, y, z, id) — a second schedule for the same cell+id is dropped
    /// (Java `scheduledTickSet`), and cells without loaded surroundings
    /// (radius 8) are skipped. Time-Keyed map keeps fire order.
    pub fn schedule_block_update(&mut self, x: i32, y: i32, z: i32, block_id: u8, delay: i32) {
        if block_id == 0 {
            return;
        }
        if !self.chunks_exist_radius(x, z, 8) {
            return;
        }
        if self.scheduled.values().zip(self.scheduled.keys()).any(|(v, k)| {
            *v == block_id && k.1 == x && k.2 == y && k.3 == z
        }) {
            return;
        }
        self.scheduled.entry((self.time + delay as i64, x, y, z)).or_insert(block_id);
    }

    /// Loaded-area check for a block radius (mirrors `checkChunksExist`).
    fn chunks_exist_radius(&self, x: i32, z: i32, r: i32) -> bool {
        let (x0, z0) = ((x - r).div_euclid(16), (z - r).div_euclid(16));
        let (x1, z1) = ((x + r).div_euclid(16), (z + r).div_euclid(16));
        for cx in x0..=x1 {
            for cz in z0..=z1 {
                if !self.has_chunk(cx, cz) {
                    return false;
                }
            }
        }
        true
    }

    /// Due scheduled updates, oldest first, capped at 1000 per tick like
    /// C++ (stale entries die on the id check; missing chunks skip).
    fn process_scheduled_ticks(&mut self) {
        let mut ran = 0;
        while ran < 1000 {
            let next = self.scheduled.iter().next().map(|(k, v)| (*k, *v));
            let ((t, x, y, z), bid) = match next {
                Some(n) => n,
                None => break,
            };
            if t > self.time {
                break;
            }
            self.scheduled.remove(&(t, x, y, z));
            ran += 1;
            // Java processes only ticks whose radius-8 surroundings are
            // loaded (World.scheduleBlockUpdate/process path).
            if !self.chunks_exist_radius(x, z, 8) {
                continue;
            }
            if bid == 0 || self.get_block_id(x, y, z) != bid {
                continue;
            }
            self.update_block_tick(x, y, z);
        }
    }

    /// Random block ticks (mirrors the 80-cells-per-chunk pass over the
    /// radius-9 loaded chunks around every joined player (Java World:1345);
    /// chunk order is sorted for determinism where C++ iterates an
    /// unordered set).
    fn random_block_ticks(&mut self) {
        let mut players: Vec<(f64, f64, f64)> = Vec::new();
        for oid in self.entities.all_ids() {
            if let Some(Entity::Player(p)) = self.entities.get(oid) {
                let b = &p.living.body;
                players.push((b.pos[0], b.pos[1], b.pos[2]));
            }
        }
        if players.is_empty() {
            return;
        }
        let mut keys: Vec<(i32, i32)> = Vec::new();
        for (px, _, pz) in &players {
            let (cx, cz) = ((px / 16.0).floor() as i32, (pz / 16.0).floor() as i32);
            for dx in -9..=9 {
                for dz in -9..=9 {
                    keys.push((cx + dx, cz + dz));
                }
            }
        }
        keys.sort_unstable();
        keys.dedup();
        for (cx, cz) in keys {
            if !self.has_chunk(cx, cz) {
                continue;
            }
            for _ in 0..80 {
                let bx = (cx << 4) + self.rng.next_int_bound(16);
                let by = self.rng.next_int_bound(128);
                let bz = (cz << 4) + self.rng.next_int_bound(16);
                let id = self.get_block_id(bx, by, bz);
                if id == 0 {
                    continue;
                }
                if alpha_block_properties_get(id as u32).tick_on_load {
                    self.update_block_tick(bx, by, bz);
                }
            }
        }
    }

    /// Periodic unload (mirrors the `worldTime % 100` pass): chunks outside
    /// the unload radius of every joined player go, except the protected
    /// spawn area (|cx|,|cz| <= 3). Live rows spill into the chunk lists
    /// for reload; with no players everything stays (no-server case).
    fn unload_chunks(&mut self) {
        if self.time % 100 != 0 {
            return;
        }
        let mut anchors: Vec<(i32, i32)> = Vec::new();
        for oid in self.entities.all_ids() {
            if let Some(Entity::Player(p)) = self.entities.get(oid) {
                anchors.push((
                    (p.living.body.pos[0].floor() as i32) >> 4,
                    (p.living.body.pos[2].floor() as i32) >> 4,
                ));
            }
        }
        if anchors.is_empty() {
            return;
        }
        let r = self.unload_radius;
        let drop: Vec<(i32, i32)> = self
            .chunks
            .keys()
            .filter(|(cx, cz)| {
                if cx.abs() <= 3 && cz.abs() <= 3 {
                    return false;
                }
                !anchors.iter().any(|(px, pz)| (cx - px).abs() <= r && (cz - pz).abs() <= r)
            })
            .copied()
            .collect();
        for (cx, cz) in drop {
            self.spill_chunk(cx, cz);
            if let Some(chunk) = self.chunks.remove(&(cx, cz)) {
                self.unloaded.insert((cx, cz), chunk);
            }
        }
    }

    /// Recall every staged chunk (the server calls this before a world
    /// save: native unloads stage to memory without hitting the disk,
    /// while C++ unloads save through, so staged edits must be pulled
    /// back before the flush or they die in memory).
    pub(crate) fn recall_all_staged(&mut self) {
        let keys: Vec<(i32, i32)> = self.unloaded.keys().copied().collect();
        for (cx, cz) in keys {
            self.recall_chunk(cx, cz);
        }
    }

    /// Recall an unloaded chunk and thaw its spill back into rows
    /// (mirrors the load path; disk fills `unloaded` in the persistence
    /// slice). Returns false when nothing was staged.
    pub fn recall_chunk(&mut self, cx: i32, cz: i32) -> bool {
        if self.chunks.contains_key(&(cx, cz)) {
            return true;
        }
        if let Some(chunk) = self.unloaded.remove(&(cx, cz)) {
            self.chunks.insert((cx, cz), chunk);
            self.restore_chunk_entities(cx, cz);
            return true;
        }
        false
    }

    /// Freeze a chunk's live rows into its pending lists (mirrors the
    /// unload spill; boats included).
    fn spill_chunk(&mut self, cx: i32, cz: i32) {
        let mut gone: Vec<EntityId> = Vec::new();
        for oid in self.entities.alive_ids() {
            let e = match self.entities.get(oid) {
                Some(e) => e,
                None => continue,
            };
            if (e.body().pos[0].floor() as i32) >> 4 != cx
                || (e.body().pos[2].floor() as i32) >> 4 != cz
            {
                continue;
            }
            let chunk = match self.chunks.get_mut(&(cx, cz)) {
                Some(c) => c,
                None => continue,
            };
            match e {
                Entity::Item(it) => chunk.pending_items.push(crate::chunk::PendingItem {
                    item_id: it.item_id,
                    count: it.count,
                    damage: it.damage,
                    age: it.age,
                    pickup_delay: it.pickup_delay,
                    pos: it.body.pos,
                }),
                Entity::Animal(a) => chunk.pending_animals.push(pending_creature(
                    animal_string_id(a.kind),
                    &a.living,
                    a.saddled,
                    a.sheared,
                    a.egg_timer,
                )),
                Entity::Mob(m) => chunk.pending_monsters.push(pending_creature(
                    mob_string_id(m.kind),
                    &m.living,
                    false,
                    false,
                    0,
                )),
                Entity::Boat(b) => chunk.pending_boats.push(crate::chunk::PendingBoat {
                    pos: b.body.pos,
                    motion: b.body.motion,
                    yaw: b.body.yaw,
                    pitch: b.body.pitch,
                    time_since_hit: b.time_since_hit,
                    damage_taken: b.damage_taken,
                    forward_dir: b.forward_dir,
                }),
                _ => continue,
            }
            gone.push(oid);
        }
        gone.sort_unstable();
        for oid in gone {
            self.entities.remove(oid);
        }
    }

    /// Thaw a chunk's pending lists back into rows (used on load and by
    /// tests; unknown string ids are skipped like the C++ restore).
    pub fn restore_chunk_entities(&mut self, cx: i32, cz: i32) {
        let chunk = match self.chunks.get_mut(&(cx, cz)) {
            Some(c) => c,
            None => return,
        };
        let items = std::mem::take(&mut chunk.pending_items);
        let animals = std::mem::take(&mut chunk.pending_animals);
        let monsters = std::mem::take(&mut chunk.pending_monsters);
        let boats = std::mem::take(&mut chunk.pending_boats);
        for it in items {
            let id = self.entities.alloc_id();
            let mut b = Body::new(id, 0.25, 0.25, 0.125);
            b.set_position(it.pos[0], it.pos[1], it.pos[2]);
            self.entities.insert(Entity::Item(crate::entity_table::ItemEnt {
                body: b,
                item_id: it.item_id,
                count: it.count,
                damage: it.damage,
                age: it.age,
                pickup_delay: it.pickup_delay,
            }));
        }
        for an in animals {
            if let Some(kind) = animal_kind_of(&an.string_id) {
                let id = self.entities.alloc_id();
                let mut a = crate::entity_table::AnimalEnt::new(id, kind);
                a.living.body.set_position(an.pos[0], an.pos[1], an.pos[2]);
                a.living.body.motion = an.motion;
                a.living.body.yaw = an.yaw;
                a.living.body.pitch = an.pitch;
                a.living.health = an.health;
                a.living.max_health = an.max_health;
                a.saddled = an.saddled;
                a.sheared = an.sheared;
                a.egg_timer = an.egg_timer;
                self.entities.insert(Entity::Animal(a));
            }
        }
        for mo in monsters {
            if let Some(kind) = mob_kind_of(&mo.string_id) {
                let id = self.entities.alloc_id();
                let mut m = crate::entity_table::MobEnt::new(id, kind);
                m.living.body.set_position(mo.pos[0], mo.pos[1], mo.pos[2]);
                m.living.body.motion = mo.motion;
                m.living.body.yaw = mo.yaw;
                m.living.body.pitch = mo.pitch;
                m.living.health = mo.health;
                m.living.max_health = mo.max_health;
                self.entities.insert(Entity::Mob(m));
            }
        }
        for bt in boats {
            let id = self.entities.alloc_id();
            let mut b = Body::new(id, 1.5, 0.6, 0.3);
            b.set_position(bt.pos[0], bt.pos[1], bt.pos[2]);
            b.motion = bt.motion;
            b.yaw = bt.yaw;
            b.pitch = bt.pitch;
            self.entities.insert(Entity::Boat(crate::entity_table::BoatEnt {
                body: b,
                time_since_hit: bt.time_since_hit,
                damage_taken: bt.damage_taken,
                forward_dir: bt.forward_dir,
            }));
        }
    }
}

/// Block-entity data by cell (mirrors the C++ per-chunk `TileEntity`
/// objects, stored flat until the tile tick slice needs behavior).
#[derive(Clone, Copy, Debug)]
pub enum TileData {
    Furnace(FfiFurnaceState),
    Chest(FfiChestState),
    Sign(FfiSignState),
}

/// String id for spill/restore (mirrors `getEntityStringId`).
pub(crate) fn mob_string_id(kind: MobKind) -> String {
    match kind {
        MobKind::Spider => "Spider",
        MobKind::Zombie => "Zombie",
        MobKind::Skeleton => "Skeleton",
        MobKind::Creeper => "Creeper",
    }
    .to_string()
}

/// String id for spill/restore (mirrors `getEntityStringId`).
pub(crate) fn animal_string_id(kind: AnimalKind) -> String {
    match kind {
        AnimalKind::Sheep => "Sheep",
        AnimalKind::Pig => "Pig",
        AnimalKind::Chicken => "Chicken",
        AnimalKind::Cow => "Cow",
    }
    .to_string()
}

fn mob_kind_of(id: &str) -> Option<MobKind> {
    match id {
        "Spider" => Some(MobKind::Spider),
        "Zombie" => Some(MobKind::Zombie),
        "Skeleton" => Some(MobKind::Skeleton),
        "Creeper" => Some(MobKind::Creeper),
        _ => None,
    }
}

fn animal_kind_of(id: &str) -> Option<AnimalKind> {
    match id {
        "Sheep" => Some(AnimalKind::Sheep),
        "Pig" => Some(AnimalKind::Pig),
        "Chicken" => Some(AnimalKind::Chicken),
        "Cow" => Some(AnimalKind::Cow),
        _ => None,
    }
}

fn pending_creature(
    string_id: String,
    living: &LivingBody,
    saddled: bool,
    sheared: bool,
    egg_timer: i32,
) -> crate::chunk::PendingCreature {
    crate::chunk::PendingCreature {
        string_id,
        pos: living.body.pos,
        motion: living.body.motion,
        yaw: living.body.yaw,
        pitch: living.body.pitch,
        health: living.health,
        max_health: living.max_health,
        saddled,
        sheared,
        egg_timer,
    }
}

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
        {
            let _guard = TickGuard::enter(self as *mut World);
            // Decoration sets bypass skylight regen (mirrors C++
            // isPopulating); the write-back below regenerates instead.
            self.populating = true;
            let gen = self.generator();
            rust_chunk_provider_populate_batch(
                gen,
                &batch,
                tree_accessor(),
                cx,
                cz,
                center_biome.biome_type as i32,
                &center_temps,
            );
            self.populating = false;
        }
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
        for (lx, ly, lz, slot, item, count) in crate::decorators::misc::drain_dungeon_loot() {
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
