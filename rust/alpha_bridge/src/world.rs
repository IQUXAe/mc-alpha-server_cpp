//! Native world state: the Rust-owned replacement for the C++ `World`
//! block storage, clock, and spawn point (mirrors Java `World` storage
//! semantics). Chunks reuse the `chunk` module, block facts come from the
//! `block` table, and materials from `material`.
//!
//! v1 scope: chunk map, block access, material/light/height queries,
//! collision-box gathering for physics, plus the entity table and tracker
//! for tick integration. Generation, persistence, lighting updates, and
//! networking arrive in later slices.

use std::collections::HashMap;

use crate::aabb::AxisAlignedBB;
use crate::block::{BlockMaterial, BlockType, alpha_block_properties_get};
use crate::chunk::Chunk;
use crate::entity_ai::{
    alpha_ai_animal_path_weight, alpha_ai_mob_path_weight, chase_speed, face_run, steer_run,
    wander_pick,
};
use crate::entity_living::{HeadingIo, MoveFeedback, alpha_living_fall_damage, living_heading_run};
use crate::entity_physics::{PushOut, alpha_entity_push};
use crate::entity_table::{
    AnimalKind, Body, Entity, EntityId, EntityTable, MobKind, mob_attack_reach,
    mob_burns_in_daylight,
};
use crate::material::Material;
use crate::math_helper::{floor_double, sqrt_float};
use crate::pathfinder::find_path_native;
use crate::random::JavaRandom;
use crate::tracker::Tracker;

pub const WORLD_HEIGHT: i32 = 128;

/// Block types with no collision box (mirrors the C++ `nullopt`
/// `getCollisionBoundingBoxFromPool` overrides: fluids, plants, torches,
/// saplings, crops, fire).
fn has_collision_box(block_type: u8) -> bool {
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
#[derive(Debug)]
pub struct World {
    pub seed: i64,
    pub time: i64,
    pub spawn: [i32; 3],
    chunks: HashMap<(i32, i32), Chunk>,
    pub entities: EntityTable,
    pub tracker: Tracker,
    rng: JavaRandom,
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
    /// NoChunkLoad setters swallowing silently).
    pub fn set_block_id(&mut self, x: i32, y: i32, z: i32, id: u8) -> bool {
        if y < 0 || y >= WORLD_HEIGHT {
            return false;
        }
        let (cx, cz, lx, lz) = Self::chunk_of(x, z);
        self.chunks.get_mut(&(cx, cz)).map(|c| c.set_block_id(lx, y, lz, id)).unwrap_or(false)
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
                    if !has_collision_box(props.block_type) {
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
        let (orig, no_clip, step, was_ground, suppress, fall) = match self.entities.get(id) {
            Some(e) => {
                let b = e.body();
                (
                    b.bounding_box.clone(),
                    b.no_clip,
                    b.step_height,
                    b.on_ground,
                    b.suppress_fall_state,
                    b.fall_distance,
                )
            }
            None => return None,
        };
        let (old_x, old_y, old_z) = (dx, dy, dz);
        let (mut mx, mut my, mut mz) = (dx, dy, dz);
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
            if !suppress {
                let mut ev = -1.0f32;
                let nd = unsafe {
                    crate::entity_physics::alpha_entity_fall_step(falling.0, falling.1, fall, &mut ev)
                };
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
        let side = unsafe {
            crate::entity_misc::alpha_item_push_side(
                !self.is_solid(ix - 1, iy, iz),
                !self.is_solid(ix + 1, iy, iz),
                !self.is_solid(ix, iy - 1, iz),
                !self.is_solid(ix, iy + 1, iz),
                !self.is_solid(ix, iy, iz - 1),
                !self.is_solid(ix, iy, iz + 1),
                lx,
                ly,
                lz,
            )
        };
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
            unsafe {
                crate::entity_misc::alpha_item_damp(e.body.on_ground, &mut m);
            }
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
        unsafe {
            crate::entity_misc::alpha_boat_steer(dx, dz, yaw, &mut new_yaw);
        }
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
            let ok = unsafe { crate::entity_physics::alpha_entity_push(ax, az, bx, bz, true, true, &mut push) };
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
    use crate::entity_table::{LivingBody, PlayerEnt};

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
        let mut l = LivingBody::new(id, 0.6, 1.8, 0.0);
        l.body.set_position(x, y, z);
        w.entities.insert(crate::entity_table::Entity::Player(PlayerEnt {
            living: l,
            username: name.to_string(),
            score: 0,
        }));
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
            path: Vec::new(),
            path_index: 0,
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

    use crate::entity_table::{AnimalEnt, AnimalKind, MobEnt, MobKind};

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
        // Fell ~6 blocks: a zombie would be hurt, the chicken is unharmed.
        assert_eq!(
            match w.entities.get(chicken).unwrap() {
                crate::entity_table::Entity::Animal(a) => a.living.health,
                _ => unreachable!(),
            },
            20
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
}

impl World {
    fn living_eye_height(e: &Entity) -> f64 {
        match e {
            Entity::Player(_) => 1.62,
            _ => e.body().height as f64 * 0.85,
        }
    }

    /// Damage pipeline on a native living row (mirrors
    /// `EntityLiving::attackEntityFrom` + `onDeath` with mob/animal drops).
    /// `attacker` supplies knockback direction; `None` skips it.
    pub fn attack_living(&mut self, id: EntityId, amount: i32, attacker: Option<EntityId>) {
        use crate::entity_living::{AttackInput, living_attack_run};
        let input = match self.entities.get(id) {
            Some(Entity::Mob(_)) | Some(Entity::Animal(_)) => {
                let (l, px, pz) = match self.entities.get(id) {
                    Some(Entity::Mob(m)) => (&m.living, m.living.body.pos[0], m.living.body.pos[2]),
                    Some(Entity::Animal(a)) => (&a.living, a.living.body.pos[0], a.living.body.pos[2]),
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
            _ => return,
        }
        if died {
            self.kill_living(id);
        }
    }

    /// Death: dismount both sides, spawn kind drops, mark dead (mirrors
    /// `EntityLiving::onDeath` + mob/animal `onDeath`).
    pub fn kill_living(&mut self, id: EntityId) {
        let (px, py, pz) = match self.entities.get(id) {
            Some(Entity::Mob(m)) => (m.living.body.pos[0], m.living.body.pos[1], m.living.body.pos[2]),
            Some(Entity::Animal(a)) => (a.living.body.pos[0], a.living.body.pos[1], a.living.body.pos[2]),
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
        // Kind drops (counts mirror the C++ getDropCount formulas).
        let (drop_id, drop_count) = self.living_drops(id);
        for _ in 0..drop_count {
            self.spawn_item_entity(drop_id, 1, 0, px, py, pz);
        }
        if let Some(e) = self.entities.get_mut(id) {
            e.body_mut().dead = true;
        }
    }

    fn living_drops(&mut self, id: EntityId) -> (i32, i32) {
        // Counts mirror the C++ getDropCount formulas exactly (zombie and
        // spider roll 0..2, the rest 1..3). Draws come from the world RNG.
        match self.entities.get(id) {
            Some(Entity::Mob(m)) => match m.kind {
                crate::entity_table::MobKind::Spider => (287, self.rng.next_int_bound(3)),
                crate::entity_table::MobKind::Zombie => (288, self.rng.next_int_bound(3)),
                crate::entity_table::MobKind::Skeleton => (262, 1 + self.rng.next_int_bound(3)),
                crate::entity_table::MobKind::Creeper => (289, self.rng.next_int_bound(3)),
            },
            Some(Entity::Animal(a)) => match a.kind {
                crate::entity_table::AnimalKind::Sheep if a.sheared => (0, 0),
                crate::entity_table::AnimalKind::Sheep => (35, 1 + self.rng.next_int_bound(3)),
                crate::entity_table::AnimalKind::Pig => (319, 1 + self.rng.next_int_bound(3)),
                crate::entity_table::AnimalKind::Chicken => (288, 1 + self.rng.next_int_bound(3)),
                crate::entity_table::AnimalKind::Cow => (334, 1 + self.rng.next_int_bound(3)),
            },
            _ => (0, 0),
        }
    }

    /// Per-tick living maintenance (mirrors `EntityLiving::tick`).
    pub fn tick_living(&mut self, id: EntityId) {
        self.entities.tick_base(id);
        let (alive, opaque, water, air, hurt, attack, resist) = match self.entities.get(id) {
            Some(Entity::Mob(_)) | Some(Entity::Animal(_)) => {
                let l = match self.entities.get(id) {
                    Some(Entity::Mob(m)) => &m.living,
                    Some(Entity::Animal(a)) => &a.living,
                    _ => unreachable!(),
                };
                let eye = l.body.pos[1] + l.body.height as f64 * 0.85;
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
            _ => return,
        }
        if t.suffocate {
            self.attack_living(id, 1, None);
        }
        if t.drown {
            self.attack_living(id, 2, None);
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
    on_ground: bool,
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
        Some(near)
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
            WeightRule::Mob => alpha_ai_mob_path_weight(),
            WeightRule::Animal => alpha_ai_animal_path_weight(
                World::block_id_in(chunks, x, y - 1, z) == GRASS_BLOCK_ID,
                World::block_light_in(chunks, x, y, z) as i32,
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
            on_ground: body.on_ground,
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
        snap: &mut CreatureSnap,
        in_liquid: bool,
        strafe: &mut f32,
        forward: &mut f32,
        jumping: &mut bool,
    ) -> Option<EntityId> {
        // Phase 1: drop dead/missing targets like the base clear branch.
        if let Some(t) = target {
            if !self.target_alive(t) {
                target = None;
                self.store_mob_target(id, None);
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
        let world = self as *mut World;
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
                    fb.pos_y = e.body().pos[1];
                }
            }
            true
        };
        living_heading_run(strafe, forward, jumping, on_ground, yaw, &mut io, liquid, &mut ladder, &mut mover);
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
            let ok = unsafe { alpha_entity_push(ox, oz, sx, sz, true, self_pushable, &mut push) };
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
            self.creature_phases(id, target, WeightRule::Mob, &mut snap, in_liquid, &mut strafe, &mut forward, &mut jumping);
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
        self.creature_phases(id, None, WeightRule::Animal, &mut snap, in_liquid, &mut strafe, &mut forward, &mut jumping);
        self.store_creature_nav(id, &snap, jumping);
        (strafe, forward)
    }

    /// Mob tick (mirrors `EntityMob::tick`): living maintenance, cooldown
    /// and burn schedule, daylight ignition, AI, heading move with fall
    /// damage, and neighbor shoves. Like C++, the AI and move still run
    /// when burn damage kills mid-tick.
    pub fn tick_mob(&mut self, id: EntityId) {
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
