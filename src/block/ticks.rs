//! Block behavior (mirrors Java `Block*`).
//!
//! Every per-block event (`onBlockAdded`, `onNeighborBlockChange`,
//! `updateTick`, `canBlockStay`, drops) lives here. World access is a
//! direct `&mut World` borrow, so draw sequences and check order are
//! preserved exactly.
//!
//! Parameter notes:
//! - block ids the caller already knows (`leaves_id`, `crop_id`, item
//!   ids, `is_lava`) are passed in instead of re-reading the registry.
//! - the leaves recursion guard is a plain `&mut i32` counter.
//! - sapling growth returns an action; tree generation itself runs through
//!   the world accessor path (see `world::blocks`).

use crate::entity::table::{Body, Entity};
use crate::material::Material;
use crate::world::{World, has_collision_box, has_collision_id};

pub const PLANT_GROWTH_STAGE_MAX: u8 = 15;
pub const LEAVES_DECAY_GUARD_MAX: i32 = 100;

/// Sapling tick outcome. `GrowTree` carries the `World::rand()` draw; the
/// caller runs tree generation for it.
#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SaplingAction {
    None = 0,
    GrowTree = 1,
}

#[derive(Clone, Copy, Debug)]
pub struct TickAction {
    pub kind: u8,
    pub seed: u64,
}

// ---- internal query/update helpers ----

fn q_id(w: &mut World, x: i32, y: i32, z: i32) -> u8 {
    w.get_block_id(x, y, z)
}
/// Neighbor scans that must not force chunk loads (mirrors NoChunkLoad calls).
fn q_id_nc(w: &mut World, x: i32, y: i32, z: i32) -> u8 {
    // Same map: the native world never force-loads chunks.
    w.get_block_id(x, y, z)
}
fn q_meta(w: &mut World, x: i32, y: i32, z: i32) -> u8 {
    w.get_block_meta(x, y, z)
}
fn q_light(w: &mut World, x: i32, y: i32, z: i32) -> i32 {
    w.block_light_value(x, y, z) as i32
}
fn q_sky(w: &mut World, x: i32, y: i32, z: i32) -> bool {
    w.can_see_sky(x, y, z)
}
fn q_attach_world(w: &mut World, x: i32, y: i32, z: i32) -> bool {
    w.block_allows_attachment(x, y, z)
}
fn q_attach_torch(w: &mut World, x: i32, y: i32, z: i32) -> bool {
    // Solid material plus collidable (everything but fluids).
    if w.get_block_id(x, y, z) == 0 {
        return false;
    }
    let m = w.material_at(x, y, z);
    m.is_solid() && !m.is_liquid()
}
fn q_solid(w: &mut World, x: i32, y: i32, z: i32) -> bool {
    // Material-solid like blockTickIsSolid (NOT the id list).
    w.get_block_id(x, y, z) != 0 && w.material_at(x, y, z).is_solid()
}
fn q_solid_nc(w: &mut World, x: i32, y: i32, z: i32) -> bool {
    w.get_block_id(x, y, z) != 0 && w.material_at(x, y, z).is_solid()
}
fn q_water_lava(w: &mut World, x: i32, y: i32, z: i32) -> bool {
    // Literal mirror (air and fire count as water-or-lava in C++!).
    let bid = w.get_block_id(x, y, z);
    if bid == 0 || bid == 51 {
        return true;
    }
    matches!(w.material_at(x, y, z), Material::WATER | Material::LAVA)
}
fn q_water(w: &mut World, x: i32, y: i32, z: i32) -> bool {
    w.material_at(x, y, z) == Material::WATER
}
fn q_collidable(w: &mut World, x: i32, y: i32, z: i32) -> bool {
    let bid = w.get_block_id(x, y, z);
    bid != 0
        && crate::block::table::block_properties_get(bid as u32).block_type
            != crate::block::table::BlockType::Fluid as u8
        && has_collision_box(crate::block::table::block_properties_get(bid as u32).block_type)
        && has_collision_id(bid)
}
fn u_set_meta(w: &mut World, x: i32, y: i32, z: i32, meta: u8) {
    w.set_block_meta(x, y, z, meta);
}
fn u_set_notify(w: &mut World, x: i32, y: i32, z: i32, id: u8) {
    w.apply_set_notify(x, y, z, id);
}
fn u_set_meta_notify(w: &mut World, x: i32, y: i32, z: i32, id: u8, meta: u8) {
    w.apply_set_meta_notify(x, y, z, id, meta);
}
fn u_set_and_meta(w: &mut World, x: i32, y: i32, z: i32, id: u8, meta: u8) {
    w.set_block_id(x, y, z, id);
    w.set_block_meta(x, y, z, meta);
}
fn u_schedule(w: &mut World, x: i32, y: i32, z: i32, id: u8, delay: i32) {
    w.schedule_block_update(x, y, z, id, delay);
}
fn u_notify(w: &mut World, x: i32, y: i32, z: i32, _id: u8) {
    w.notify_neighbors_of(x, y, z);
}
fn u_drop(w: &mut World, item: i32, count: i32, damage: i32, fx: f64, fy: f64, fz: f64, spread: f64, up: f64) {
    if count <= 0 {
        return;
    }
    let eid = w.spawn_item_entity(item, count, damage, fx, fy, fz);
    let (dx, dz) = (w.rng_next_f64(), w.rng_next_f64());
    if let Some(Entity::Item(e)) = w.entities.get_mut(eid) {
        e.body.motion[0] = -spread + 2.0 * spread * dx;
        e.body.motion[1] = up;
        e.body.motion[2] = -spread + 2.0 * spread * dz;
    }
}
fn rng_int(w: &mut World, bound: i32) -> i32 {
    w.rng_next_int(bound)
}
fn rng_f01(w: &mut World) -> f32 {
    w.rng_next_f32()
}
fn rng_u64(w: &mut World) -> u64 {
    w.rng_next_u64()
}
fn chance_one_in(w: &mut World, one_in: i32) -> bool {
    if one_in <= 1 {
        return true;
    }
    rng_int(w, one_in) == 0
}

/// Base drop (`Block::dropBlockAsItemWithChance`): id/count/damage already
/// resolved by C++ virtuals. Returns whether anything was spawned.
fn base_drop(
    w: &mut World,
    item_id: i32,
    count: i32,
    damage: i32,
    x: i32,
    y: i32,
    z: i32,
    chance: f32,
) -> bool {
    if item_id <= 0 || count <= 0 {
        return false;
    }
    if rng_f01(w) > chance {
        return false;
    }
    for _ in 0..count {
        u_drop(w, item_id, 1, damage, x as f64 + 0.5, y as f64 + 0.7, z as f64 + 0.5, 0.1, 0.2);
    }
    true
}

// ---- sand ----

fn sand_schedule(w: &mut World, block_id: u8, x: i32, y: i32, z: i32) {
    u_schedule(w, x, y, z, block_id, 3);
}

pub fn block_sand_added(w: &mut World, block_id: u8, x: i32, y: i32, z: i32) {
    sand_schedule(w, block_id, x, y, z);
}

pub fn block_sand_neighbor(w: &mut World, block_id: u8, x: i32, y: i32, z: i32) {
    sand_schedule(w, block_id, x, y, z);
}

fn sand_can_fall_below(w: &mut World, x: i32, y: i32, z: i32) -> bool {
    let id = q_id(w, x, y, z);
    if id == 0 || id == 51 {
        return true;
    }
    // Mirrors initBlocks: every non-air material gets a Block instance.
    if !World::native_registered(id) {
        return true;
    }
    q_water_lava(w, x, y, z)
}

pub fn block_sand_tick(w: &mut World, block_id: u8, x: i32, y: i32, z: i32) {
    if y >= 0 && sand_can_fall_below(w, x, y - 1, z) {
        w.set_block_id(x, y, z, 0);
        let id = w.entities.alloc_id();
        let mut b = Body::new(id, 0.98, 0.98, 0.49);
        b.set_position(x as f64 + 0.5, y as f64 + 0.5, z as f64 + 0.5);
        w.entities.insert(Entity::Falling(crate::entity::table::FallingEnt {
            body: b,
            block_id: block_id as i32,
            fall_time: 0,
        }));
    }
}

// ---- fluid ----

const FLOW_DIRS: [(i32, i32, i32); 6] = [(1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)];
const FLOW_PASSABLE: [u8; 11] = [6, 37, 38, 39, 40, 50, 51, 55, 59, 83, 78];

fn fluid_can_flow_into(w: &mut World, x: i32, y: i32, z: i32) -> bool {
    let id = q_id(w, x, y, z);
    if id == 0 {
        return true;
    }
    if id == 8 || id == 9 || id == 10 || id == 11 {
        return false;
    }
    FLOW_PASSABLE.contains(&id)
}

pub fn block_fluid_added(w: &mut World, block_id: u8, tick_rate: i32, x: i32, y: i32, z: i32) {
    u_schedule(w, x, y, z, block_id, tick_rate);
}

pub fn block_fluid_neighbor(w: &mut World, block_id: u8, tick_rate: i32, x: i32, y: i32, z: i32) {
    u_schedule(w, x, y, z, block_id, tick_rate);
}

pub fn block_fluid_tick(
    w: &mut World,
    block_id: u8,
    is_lava: bool,
    x: i32,
    y: i32,
    z: i32,
) {
    // Lava creates fire on adjacent burnable blocks (Java BlockStationary).
    if is_lava && q_meta(w, x, y, z) == 0 {
        for (dx, dy, dz) in FLOW_DIRS {
            let (nx, ny, nz) = (x + dx, y + dy, z + dz);
            if q_id(w, nx, ny, nz) == 0 && rng_int(w, 4) == 0
                && (q_attach_world(w, nx, ny - 1, nz) || q_id(w, nx, ny - 1, nz) == 87)
            {
                u_set_notify(w, nx, ny, nz, 51);
            }
        }
    }

    let metadata = q_meta(w, x, y, z);
    if metadata >= 8 {
        return;
    }
    if fluid_can_flow_into(w, x, y - 1, z) {
        u_set_meta_notify(w, x, y - 1, z, block_id, 8);
    } else if metadata < 7 {
        let mut new_meta = metadata + 1;
        if is_lava {
            new_meta = metadata + 2;
        }
        if new_meta < 8 {
            for (dx, dz) in [(-1, 0), (1, 0), (0, -1), (0, 1)] {
                let (nx, nz) = (x + dx, z + dz);
                if fluid_can_flow_into(w, nx, y, nz) {
                    if q_id(w, nx, y, nz) != 0 {
                        w.drop_block_as_item(nx, y, nz);
                    }
                    u_set_meta_notify(w, nx, y, nz, block_id, new_meta);
                }
            }
        }
    }
    // No self-reschedule: spread continues through the new cells' own
    // added/neighbor ticks (apply_set_meta_notify → block_fluid_added).
    // Self-perpetuating every 5 ticks would pin oceans/lakes in the
    // scheduler forever (1000-tick cap stall).
}

// ---- flower ----

fn flower_can_stay_here(w: &mut World, x: i32, y: i32, z: i32) -> bool {
    let below = q_id(w, x, y - 1, z);
    (q_light(w, x, y, z) >= 8 || q_sky(w, x, y, z)) && (below == 2 || below == 3 || below == 60)
}

pub fn block_flower_can_stay(w: &mut World, x: i32, y: i32, z: i32) -> bool {
    flower_can_stay_here(w, x, y, z)
}

pub fn block_flower_neighbor(
    w: &mut World,
    drop_id: i32,
    drop_count: i32,
    drop_damage: i32,
    x: i32,
    y: i32,
    z: i32,
) {
    if !flower_can_stay_here(w, x, y, z) {
        base_drop(w, drop_id, drop_count, drop_damage, x, y, z, 1.0);
        w.set_block_id(x, y, z, 0);
    }
}

pub fn block_flower_tick(
    w: &mut World,
    drop_id: i32,
    drop_count: i32,
    drop_damage: i32,
    x: i32,
    y: i32,
    z: i32,
) {
    if !flower_can_stay_here(w, x, y, z) {
        base_drop(w, drop_id, drop_count, drop_damage, x, y, z, 1.0);
        u_set_notify(w, x, y, z, 0);
    }
}

// ---- tall grass (drop only; stay logic inherited from flower) ----

pub fn block_tallgrass_drop(
    w: &mut World,
    seeds_id: i32,
    chance: f32,
    x: i32,
    y: i32,
    z: i32,
) {
    if seeds_id <= 0 {
        return;
    }
    if rng_f01(w) > chance {
        return;
    }
    if !chance_one_in(w, 8) {
        return;
    }
    u_drop(w, seeds_id, 1, 0, x as f64 + 0.5, y as f64 + 0.5, z as f64 + 0.5, 0.05, 0.15);
}

// ---- mushroom ----

fn mushroom_can_stay_here(w: &mut World, x: i32, y: i32, z: i32) -> bool {
    // Mirrors `BlockMushroom.canBlockStay`: dim light plus an attachable
    // (opaque) block below (`field_540_p`).
    q_light(w, x, y, z) <= 13 && q_attach_world(w, x, y - 1, z)
}

pub fn block_mushroom_can_stay(w: &mut World, x: i32, y: i32, z: i32) -> bool {
    mushroom_can_stay_here(w, x, y, z)
}

pub fn block_mushroom_neighbor(
    w: &mut World,
    drop_id: i32,
    drop_count: i32,
    drop_damage: i32,
    x: i32,
    y: i32,
    z: i32,
) {
    if !mushroom_can_stay_here(w, x, y, z) {
        base_drop(w, drop_id, drop_count, drop_damage, x, y, z, 1.0);
        w.set_block_id(x, y, z, 0);
    }
}

// ---- torch ----

fn torch_attached(w: &mut World, x: i32, y: i32, z: i32) -> bool {
    q_attach_torch(w, x, y, z)
}

/// Java BlockTorch.onBlockPlaced metadata from the clicked face.
/// Takes the torch position; side: 1=floor(default 5),2,3,4,5 wall faces.
pub fn block_torch_attach_meta(w: &mut World, side: i32, x: i32, y: i32, z: i32) -> u8 {
    if side == 2 && torch_attached(w, x, y, z + 1) {
        4
    } else if side == 3 && torch_attached(w, x, y, z - 1) {
        3
    } else if side == 4 && torch_attached(w, x + 1, y, z) {
        2
    } else if side == 5 && torch_attached(w, x - 1, y, z) {
        1
    } else {
        5
    }
}

pub fn block_torch_added(w: &mut World, block_id: u8, x: i32, y: i32, z: i32) {
    if q_meta(w, x, y, z) != 0 {
        return;
    }
    let meta = if torch_attached(w, x - 1, y, z) {
        1
    } else if torch_attached(w, x + 1, y, z) {
        2
    } else if torch_attached(w, x, y, z - 1) {
        3
    } else if torch_attached(w, x, y, z + 1) {
        4
    } else if torch_attached(w, x, y - 1, z) {
        5
    } else {
        0
    };
    if meta != 0 {
        u_set_and_meta(w, x, y, z, block_id, meta);
    }
}

fn torch_can_stay_here(w: &mut World, x: i32, y: i32, z: i32) -> bool {
    torch_attached(w, x - 1, y, z)
        || torch_attached(w, x + 1, y, z)
        || torch_attached(w, x, y, z - 1)
        || torch_attached(w, x, y, z + 1)
        || torch_attached(w, x, y - 1, z)
}

pub fn block_torch_can_stay(w: &mut World, x: i32, y: i32, z: i32) -> bool {
    torch_can_stay_here(w, x, y, z)
}

pub fn block_torch_neighbor(
    w: &mut World,
    drop_id: i32,
    drop_count: i32,
    drop_damage: i32,
    x: i32,
    y: i32,
    z: i32,
) {
    let meta = q_meta(w, x, y, z);
    let detach = (meta == 1 && !torch_attached(w, x - 1, y, z))
        || (meta == 2 && !torch_attached(w, x + 1, y, z))
        || (meta == 3 && !torch_attached(w, x, y, z - 1))
        || (meta == 4 && !torch_attached(w, x, y, z + 1))
        || (meta == 5 && !torch_attached(w, x, y - 1, z));
    if detach {
        base_drop(w, drop_id, drop_count, drop_damage, x, y, z, 1.0);
        u_set_notify(w, x, y, z, 0);
    }
}

// ---- cactus / reed (shared growth shape, different stay rules) ----

fn cactus_can_stay_here(w: &mut World, x: i32, y: i32, z: i32) -> bool {
    if q_solid(w, x - 1, y, z) || q_solid(w, x + 1, y, z) || q_solid(w, x, y, z - 1) || q_solid(w, x, y, z + 1) {
        return false;
    }
    let below = q_id(w, x, y - 1, z);
    below == 12 || below == 81
}

fn reed_can_stay_here(w: &mut World, x: i32, y: i32, z: i32) -> bool {
    let below = q_id(w, x, y - 1, z);
    if below == 83 {
        return true;
    }
    // Mirrors `BlockReed.canPlaceBlockAt`: grass or dirt only (sand
    // never hosts reed), with water adjacent at soil level.
    if below != 2 && below != 3 {
        return false;
    }
    for (dx, dz) in [(-1, 0), (1, 0), (0, -1), (0, 1)] {
        let id = q_id(w, x + dx, y - 1, z + dz);
        if id == 8 || id == 9 {
            return true;
        }
    }
    false
}

/// Shared cactus/reed tick: stay check, headroom, grow to height 3 max.
fn stalk_tick(w: &mut World, block_id: u8, can_stay: bool, x: i32, y: i32, z: i32) {
    if !can_stay {
        return;
    }
    if q_id(w, x, y + 1, z) != 0 {
        u_schedule(w, x, y, z, block_id, 20);
        return;
    }
    let mut height = 1;
    while q_id(w, x, y - height, z) == block_id {
        height += 1;
    }
    if height < 3 {
        let age = q_meta(w, x, y, z);
        if age >= PLANT_GROWTH_STAGE_MAX {
            u_set_notify(w, x, y + 1, z, block_id);
            u_set_meta(w, x, y, z, 0);
        } else {
            u_set_meta(w, x, y, z, age.saturating_add(1));
        }
    }
    u_schedule(w, x, y, z, block_id, 20);
}

pub fn block_cactus_can_stay(w: &mut World, x: i32, y: i32, z: i32) -> bool {
    cactus_can_stay_here(w, x, y, z)
}

pub fn block_reed_can_stay(w: &mut World, x: i32, y: i32, z: i32) -> bool {
    reed_can_stay_here(w, x, y, z)
}

pub fn block_cactus_added(w: &mut World, block_id: u8, x: i32, y: i32, z: i32) {
    u_schedule(w, x, y, z, block_id, 20);
}

pub fn block_reed_added(w: &mut World, block_id: u8, x: i32, y: i32, z: i32) {
    u_schedule(w, x, y, z, block_id, 20);
}

pub fn block_cactus_neighbor(
    w: &mut World,
    block_id: u8,
    drop_id: i32,
    drop_count: i32,
    drop_damage: i32,
    x: i32,
    y: i32,
    z: i32,
) {
    if !cactus_can_stay_here(w, x, y, z) {
        base_drop(w, drop_id, drop_count, drop_damage, x, y, z, 1.0);
        u_set_notify(w, x, y, z, 0);
        return;
    }
    u_schedule(w, x, y, z, block_id, 20);
}

pub fn block_reed_neighbor(
    w: &mut World,
    block_id: u8,
    drop_id: i32,
    drop_count: i32,
    drop_damage: i32,
    x: i32,
    y: i32,
    z: i32,
) {
    if !reed_can_stay_here(w, x, y, z) {
        base_drop(w, drop_id, drop_count, drop_damage, x, y, z, 1.0);
        u_set_notify(w, x, y, z, 0);
        return;
    }
    u_schedule(w, x, y, z, block_id, 20);
}

pub fn block_cactus_tick(
    w: &mut World,
    block_id: u8,
    drop_id: i32,
    drop_count: i32,
    drop_damage: i32,
    x: i32,
    y: i32,
    z: i32,
) {
    if !cactus_can_stay_here(w, x, y, z) {
        base_drop(w, drop_id, drop_count, drop_damage, x, y, z, 1.0);
        u_set_notify(w, x, y, z, 0);
        return;
    }
    stalk_tick(w, block_id, true, x, y, z);
}

pub fn block_reed_tick(
    w: &mut World,
    block_id: u8,
    drop_id: i32,
    drop_count: i32,
    drop_damage: i32,
    x: i32,
    y: i32,
    z: i32,
) {
    if !reed_can_stay_here(w, x, y, z) {
        base_drop(w, drop_id, drop_count, drop_damage, x, y, z, 1.0);
        u_set_notify(w, x, y, z, 0);
        return;
    }
    stalk_tick(w, block_id, true, x, y, z);
}

// ---- leaves (recursion guard passed by mutable borrow) ----

fn leaf_propagate(w: &mut World, leaves_id: u8, x: i32, y: i32, z: i32, current: i32) -> i32 {
    let id = q_id_nc(w, x, y, z);
    if id == 17 {
        return 16;
    }
    if id == leaves_id {
        let meta = q_meta(w, x, y, z) as i32;
        if meta != 0 && meta > current {
            return meta;
        }
    }
    current
}

fn update_leaf_distance(w: &mut World, block_id: u8, leaves_id: u8, x: i32, y: i32, z: i32, guard: &mut i32) {
    *guard += 1;
    if *guard > LEAVES_DECAY_GUARD_MAX {
        return;
    }

    // Server fix: leaves on solid ground don't decay.
    let mut candidate = 0;
    if q_solid_nc(w, x, y - 1, z) {
        candidate = 16;
    }

    let metadata = q_meta(w, x, y, z) as i32;
    if metadata == 0 {
        u_set_meta(w, x, y, z, 1);
    }

    candidate = leaf_propagate(w, leaves_id, x - 1, y, z, candidate);
    candidate = leaf_propagate(w, leaves_id, x + 1, y, z, candidate);
    candidate = leaf_propagate(w, leaves_id, x, y - 1, z, candidate);
    candidate = leaf_propagate(w, leaves_id, x, y + 1, z, candidate);
    candidate = leaf_propagate(w, leaves_id, x, y, z - 1, candidate);
    candidate = leaf_propagate(w, leaves_id, x, y, z + 1, candidate);

    let mut new_meta = candidate - 1;
    if new_meta < 10 {
        new_meta = 1;
    }

    if new_meta != metadata {
        u_set_meta(w, x, y, z, new_meta as u8);
        // Server fix: force neighbors to recalculate, otherwise the
        // flood-fill chain reaction stops here.
        u_notify(w, x, y, z, block_id);
        update_neighbor_leaf(w, block_id, leaves_id, x - 1, y, z, metadata, guard);
        update_neighbor_leaf(w, block_id, leaves_id, x + 1, y, z, metadata, guard);
        update_neighbor_leaf(w, block_id, leaves_id, x, y - 1, z, metadata, guard);
        update_neighbor_leaf(w, block_id, leaves_id, x, y + 1, z, metadata, guard);
        update_neighbor_leaf(w, block_id, leaves_id, x, y, z - 1, metadata, guard);
        update_neighbor_leaf(w, block_id, leaves_id, x, y, z + 1, metadata, guard);
    }
}

fn update_neighbor_leaf(
    w: &mut World,
    block_id: u8,
    leaves_id: u8,
    x: i32,
    y: i32,
    z: i32,
    previous: i32,
    guard: &mut i32,
) {
    if q_id_nc(w, x, y, z) != leaves_id {
        return;
    }
    let meta = q_meta(w, x, y, z) as i32;
    if meta != 0 && meta == previous - 1 {
        update_leaf_distance(w, block_id, leaves_id, x, y, z, guard);
    }
}

pub fn block_leaves_added(w: &mut World, block_id: u8, x: i32, y: i32, z: i32) {
    u_schedule(w, x, y, z, block_id, 40);
}

pub fn block_leaves_neighbor(
    w: &mut World,
    block_id: u8,
    leaves_id: u8,
    guard: &mut i32,
    x: i32,
    y: i32,
    z: i32,
) {
    *guard = 0;
    update_leaf_distance(w, block_id, leaves_id, x, y, z, guard);
}

pub fn block_leaves_tick(
    w: &mut World,
    block_id: u8,
    leaves_id: u8,
    drop_id: i32,
    drop_count: i32,
    drop_damage: i32,
    guard: &mut i32,
    x: i32,
    y: i32,
    z: i32,
) {
    let metadata = q_meta(w, x, y, z) as i32;
    if metadata == 0 {
        *guard = 0;
        update_leaf_distance(w, block_id, leaves_id, x, y, z, guard);
    } else if metadata == 1 {
        base_drop(w, drop_id, drop_count, drop_damage, x, y, z, 1.0);
        u_set_notify(w, x, y, z, 0);
    } else if chance_one_in(w, 10) {
        update_leaf_distance(w, block_id, leaves_id, x, y, z, guard);
    }
}

pub fn block_leaves_drop(
    w: &mut World,
    sapling_id: i32,
    chance: f32,
    x: i32,
    y: i32,
    z: i32,
) {
    if sapling_id <= 0 {
        return;
    }
    if rng_f01(w) > chance {
        return;
    }
    if !chance_one_in(w, 20) {
        return;
    }
    u_drop(w, sapling_id, 1, 0, x as f64 + 0.5, y as f64 + 0.5, z as f64 + 0.5, 0.05, 0.15);
}

// ---- sapling ----

fn sapling_can_stay_here(w: &mut World, x: i32, y: i32, z: i32) -> bool {
    let below = q_id(w, x, y - 1, z);
    (q_light(w, x, y, z) >= 8 || q_sky(w, x, y, z)) && (below == 2 || below == 3 || below == 60)
}

pub fn block_sapling_added(w: &mut World, block_id: u8, x: i32, y: i32, z: i32) {
    u_schedule(w, x, y, z, block_id, 100);
}

pub fn block_sapling_can_stay(w: &mut World, x: i32, y: i32, z: i32) -> bool {
    sapling_can_stay_here(w, x, y, z)
}

pub fn block_sapling_neighbor(
    w: &mut World,
    block_id: u8,
    drop_id: i32,
    drop_count: i32,
    drop_damage: i32,
    x: i32,
    y: i32,
    z: i32,
) {
    if !sapling_can_stay_here(w, x, y, z) {
        base_drop(w, drop_id, drop_count, drop_damage, x, y, z, 1.0);
        u_set_notify(w, x, y, z, 0);
        return;
    }
    u_schedule(w, x, y, z, block_id, 100);
}

/// Sapling tick. Returns GrowTree (with the rand draw) when the caller
/// should run tree generation; it restores the sapling if generation fails.
pub fn block_sapling_tick(
    w: &mut World,
    block_id: u8,
    drop_id: i32,
    drop_count: i32,
    drop_damage: i32,
    x: i32,
    y: i32,
    z: i32,
) -> TickAction {
    let none = TickAction { kind: SaplingAction::None as u8, seed: 0 };
    if !sapling_can_stay_here(w, x, y, z) {
        base_drop(w, drop_id, drop_count, drop_damage, x, y, z, 1.0);
        u_set_notify(w, x, y, z, 0);
        return none;
    }
    if q_light(w, x, y + 1, z) < 9 || !chance_one_in(w, 5) {
        u_schedule(w, x, y, z, block_id, 100);
        return none;
    }
    let metadata = q_meta(w, x, y, z);
    if metadata < 15 {
        u_set_meta(w, x, y, z, metadata.saturating_add(1));
        u_schedule(w, x, y, z, block_id, 100);
        return none;
    }
    let seed = rng_u64(w);
    u_set_notify(w, x, y, z, 0);
    TickAction { kind: SaplingAction::GrowTree as u8, seed }
}

// ---- crops ----

fn crops_can_stay_here(w: &mut World, _crop_id: u8, x: i32, y: i32, z: i32) -> bool {
    q_id(w, x, y - 1, z) == 60 && (q_light(w, x, y, z) >= 8 || q_sky(w, x, y, z))
}

fn crops_growth_rate(w: &mut World, crop_id: u8, x: i32, y: i32, z: i32) -> f32 {
    let mut rate = 1.0f32;
    for dx in -1..=1 {
        for dz in -1..=1 {
            if q_id(w, x + dx, y - 1, z + dz) == 60 {
                // Java: 1.0 dry, 3.0 hydrated (soil meta > 0).
                let mut bonus = if q_meta(w, x + dx, y - 1, z + dz) > 0 { 3.0 } else { 1.0 };
                if dx != 0 || dz != 0 {
                    bonus /= 4.0;
                }
                rate += bonus;
            }
        }
    }
    let row = q_id(w, x - 1, y, z) == crop_id || q_id(w, x + 1, y, z) == crop_id;
    let col = q_id(w, x, y, z - 1) == crop_id || q_id(w, x, y, z + 1) == crop_id;
    let diag = q_id(w, x - 1, y, z - 1) == crop_id
        || q_id(w, x + 1, y, z - 1) == crop_id
        || q_id(w, x + 1, y, z + 1) == crop_id
        || q_id(w, x - 1, y, z + 1) == crop_id;
    if diag || (row && col) {
        rate /= 2.0;
    }
    rate
}

pub fn block_crops_added(w: &mut World, block_id: u8, x: i32, y: i32, z: i32) {
    u_schedule(w, x, y, z, block_id, 20);
}

pub fn block_crops_can_stay(w: &mut World, crop_id: u8, x: i32, y: i32, z: i32) -> bool {
    crops_can_stay_here(w, crop_id, x, y, z)
}

pub fn block_crops_neighbor(
    w: &mut World,
    block_id: u8,
    crop_id: u8,
    wheat_id: i32,
    seeds_id: i32,
    x: i32,
    y: i32,
    z: i32,
) {
    if !crops_can_stay_here(w, crop_id, x, y, z) {
        let meta = q_meta(w, x, y, z);
        block_crops_drop(w, wheat_id, seeds_id, x, y, z, meta);
        u_set_notify(w, x, y, z, 0);
        return;
    }
    u_schedule(w, x, y, z, block_id, 20);
}

fn block_crops_drop(w: &mut World, wheat_id: i32, seeds_id: i32, x: i32, y: i32, z: i32, metadata: u8) {
    // Natural break (neighbor/tick → dropBlockAsItem): wheat only when
    // mature. Seeds come only from player harvest
    // (onBlockDestroyedByPlayer), handled in World::rolled_drop_ids.
    // The old code dropped seeds here too, doubling them on tramples.
    let _ = seeds_id;
    if metadata >= 7 {
        u_drop(w, wheat_id, 1, 0, x as f64 + 0.5, y as f64 + 0.5, z as f64 + 0.5, 0.05, 0.15);
    }
}

pub fn block_crops_drop_harvest(
    w: &mut World,
    wheat_id: i32,
    seeds_id: i32,
    x: i32,
    y: i32,
    z: i32,
    metadata: u8,
    chance: f32,
) {
    if rng_f01(w) > chance {
        return;
    }
    // Player-harvest path (onBlockDestroyedByPlayer): wheat + 3 seed rolls.
    if metadata >= 7 {
        u_drop(w, wheat_id, 1, 0, x as f64 + 0.5, y as f64 + 0.5, z as f64 + 0.5, 0.05, 0.15);
    }
    for _ in 0..3 {
        if rng_int(w, 15) <= metadata as i32 {
            u_drop(w, seeds_id, 1, 0, x as f64 + 0.5, y as f64 + 0.5, z as f64 + 0.5, 0.05, 0.15);
        }
    }
}

pub fn block_crops_tick(
    w: &mut World,
    block_id: u8,
    crop_id: u8,
    wheat_id: i32,
    seeds_id: i32,
    x: i32,
    y: i32,
    z: i32,
) {
    if !crops_can_stay_here(w, crop_id, x, y, z) {
        let meta = q_meta(w, x, y, z);
        block_crops_drop(w, wheat_id, seeds_id, x, y, z, meta);
        u_set_notify(w, x, y, z, 0);
        return;
    }
    if q_light(w, x, y + 1, z) >= 9 {
        let metadata = q_meta(w, x, y, z);
        if metadata < 7 {
            let rate = crops_growth_rate(w, crop_id, x, y, z);
            let chance = 2i32.max((100.0f32 / rate) as i32);
            if rng_int(w, chance) == 0 {
                u_set_meta(w, x, y, z, metadata.saturating_add(1));
            }
        }
    }
    u_schedule(w, x, y, z, block_id, 20);
}

// ---- soil ----

fn soil_has_water(w: &mut World, x: i32, y: i32, z: i32) -> bool {
    for cx in x - 4..=x + 4 {
        for cy in y..=y + 1 {
            for cz in z - 4..=z + 4 {
                if q_water(w, cx, cy, cz) {
                    return true;
                }
            }
        }
    }
    false
}

fn soil_set_moisture(w: &mut World, x: i32, y: i32, z: i32, moisture: u8) {
    if q_meta(w, x, y, z) == moisture {
        return;
    }
    u_set_meta(w, x, y, z, moisture);
}

pub fn block_soil_added(w: &mut World, block_id: u8, x: i32, y: i32, z: i32) {
    u_schedule(w, x, y, z, block_id, 20);
}

pub fn block_soil_tick(w: &mut World, block_id: u8, x: i32, y: i32, z: i32) {
    if chance_one_in(w, 5) {
        if soil_has_water(w, x, y, z) {
            soil_set_moisture(w, x, y, z, 7);
        } else {
            let moisture = q_meta(w, x, y, z);
            if moisture > 0 {
                soil_set_moisture(w, x, y, z, moisture - 1);
            } else if q_id(w, x, y + 1, z) != 59 {
                u_set_notify(w, x, y, z, 3);
                return;
            }
        }
    }
    u_schedule(w, x, y, z, block_id, 20);
}

pub fn block_soil_walking(w: &mut World, x: i32, y: i32, z: i32) {
    if chance_one_in(w, 4) {
        u_set_notify(w, x, y, z, 3);
    }
}

pub fn block_soil_neighbor(w: &mut World, block_id: u8, x: i32, y: i32, z: i32) {
    if q_collidable(w, x, y + 1, z) {
        u_set_notify(w, x, y, z, 3);
        return;
    }
    u_schedule(w, x, y, z, block_id, 20);
}

// ---- base drop entry point (Block::dropBlockAsItemWithChance) ----

pub fn block_base_drop(
    w: &mut World,
    item_id: i32,
    count: i32,
    damage: i32,
    x: i32,
    y: i32,
    z: i32,
    chance: f32,
) -> bool {
    base_drop(w, item_id, count, damage, x, y, z, chance)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::chunk::Chunk;
    use crate::entity::table::Entity;
    use crate::world::World;

    /// Fresh world. Seeds below were picked so the scenario's FIRST RNG
    /// draw hits (staging and routing draw nothing on these paths).
    fn harness(seed: i64) -> World {
        World::new(seed)
    }

    /// One chunk plus scripted cells (id + meta), with height and skylight
    /// maps, so light queries read real values. No neighbor routing: the
    /// drivers under test drive the real routing themselves.
    fn stage(w: &mut World, cells: &[(i32, i32, i32, u8, u8)]) {
        // 3x3 chunk neighborhood: scheduling requires loaded surroundings
        // (radius 8), so the center chunk never ticks alone.
        for cx in -1..=1 {
            for cz in -1..=1 {
                w.insert_chunk(Chunk::new(cx, cz));
            }
        }
        let c = w.chunk_ref_mut(0, 0).unwrap();
        for &(x, y, z, id, meta) in cells {
            c.set_block_id(x, y, z, id);
            c.set_block_metadata(x, y, z, meta);
        }
        c.generate_height_map();
        c.generate_skylight_map();
    }

    /// (pos, count) of every loose item with this id.
    fn items_at(w: &World, id: i32) -> Vec<([f64; 3], i32)> {
        let mut out = Vec::new();
        for oid in w.entities.all_ids() {
            if let Some(Entity::Item(e)) = w.entities.get(oid) {
                if e.item_id == id {
                    out.push((e.body.pos, e.count));
                }
            }
        }
        out
    }

    /// (block id, pos) of every falling block.
    fn fallings(w: &World) -> Vec<(i32, [f64; 3])> {
        let mut out = Vec::new();
        for oid in w.entities.all_ids() {
            if let Some(Entity::Falling(e)) = w.entities.get(oid) {
                out.push((e.block_id, e.body.pos));
            }
        }
        out
    }

    #[test]
    fn test_block_scenarios() {
        // 1. Sand falls into air: clears itself and spawns the entity.
        let mut w = harness(7);
        stage(&mut w, &[(0, 5, 0, 12, 0)]);
        block_sand_tick(&mut w, 12, 0, 5, 0);
        assert_eq!(w.get_block_id(0, 5, 0), 0);
        let f = fallings(&w);
        assert_eq!(f.len(), 1, "{f:?}");
        assert_eq!(f[0].0, 12);
        assert!((f[0].1[0] - 0.5).abs() < 1e-9 && (f[0].1[2] - 0.5).abs() < 1e-9);

        // 2. Sand on solid ground: nothing happens.
        let mut w = harness(7);
        stage(&mut w, &[(0, 5, 0, 12, 0), (0, 4, 0, 1, 0)]);
        block_sand_tick(&mut w, 12, 0, 5, 0);
        assert_eq!(w.get_block_id(0, 5, 0), 12);
        assert!(w.entities.all_ids().is_empty());

        // 3. Cactus grows at age 15 with headroom: new block above, age reset.
        let mut w = harness(7);
        stage(&mut w, &[(0, 1, 0, 81, 15), (0, 0, 0, 12, 0)]);
        block_cactus_tick(&mut w, 81, 81, 1, 0, 0, 1, 0);
        assert_eq!(w.get_block_id(0, 2, 0), 81);
        assert_eq!(w.get_block_meta(0, 1, 0), 0);

        // 4. Cactus at max height 3: no growth, only reschedule.
        let mut w = harness(7);
        stage(&mut w, &[(0, 3, 0, 81, 15), (0, 2, 0, 81, 0), (0, 1, 0, 81, 0), (0, 0, 0, 12, 0)]);
        block_cactus_tick(&mut w, 81, 81, 1, 0, 0, 3, 0);
        assert_eq!(w.get_block_id(0, 4, 0), 0);
        assert!(w.scheduled.contains_key(&(20, 0, 3, 0, 81)));

        // 5. Flower under a roof without light: uprooted on neighbor change.
        let mut w = harness(7);
        stage(&mut w, &[(0, 5, 0, 37, 0), (0, 4, 0, 1, 0), (0, 6, 0, 1, 0)]);
        block_flower_neighbor(&mut w, 37, 1, 0, 0, 5, 0);
        assert_eq!(w.get_block_id(0, 5, 0), 0);
        let drops = items_at(&w, 37);
        assert_eq!(drops.len(), 1, "{drops:?}");
        assert_eq!(drops[0].1, 1);

        // 6. Torch meta 1 with no western support: detaches.
        let mut w = harness(7);
        stage(&mut w, &[(0, 5, 0, 50, 1)]);
        block_torch_neighbor(&mut w, 50, 1, 0, 0, 5, 0);
        assert_eq!(w.get_block_id(0, 5, 0), 0);
        let drops = items_at(&w, 50);
        assert_eq!(drops.len(), 1, "{drops:?}");

        // 7. Torch placement picks wall metadata (side 4 = west face => meta 2).
        let mut w = harness(7);
        stage(&mut w, &[(1, 5, 0, 1, 0)]);
        assert_eq!(block_torch_attach_meta(&mut w, 4, 0, 5, 0), 2);

        // 8. Crops grow one stage when the roll succeeds (seed opens with
        // next_int(50) == 0 on dry soil: growth rate 2.0, chance 50).
        let mut w = harness(18);
        stage(&mut w, &[(0, 5, 0, 59, 3), (0, 4, 0, 60, 0)]);
        block_crops_tick(&mut w, 59, 59, 296, 295, 0, 5, 0);
        assert_eq!(w.get_block_meta(0, 5, 0), 4);

        // 9. Mature crops drop one wheat plus seed rolls (Java: 3x
        // rand(15) <= 7 seeds on destroy; the wheat drop's motion draws
        // sit between the rolls, so the seed accounts for them).
        let mut w = harness(1);
        block_crops_drop_harvest(&mut w, 296, 295, 0, 5, 0, 7, 1.0);
        let wheat = items_at(&w, 296);
        assert_eq!(wheat.len(), 1, "{wheat:?}");
        assert_eq!(wheat[0].1, 1);
        let seeds: i32 = items_at(&w, 295).iter().map(|(_, c)| c).sum();
        assert_eq!(seeds, 3);

        // 10. Dry soil without crops reverts to dirt (seed opens with
        // next_int(5) == 0).
        let mut w = harness(0);
        stage(&mut w, &[(0, 4, 0, 60, 0)]);
        block_soil_tick(&mut w, 60, 0, 4, 0);
        assert_eq!(w.get_block_id(0, 4, 0), 3);

        // 11. Trampled soil reverts on a 1/4 roll (seed opens with
        // next_int(4) == 0).
        let mut w = harness(4096);
        stage(&mut w, &[(0, 4, 0, 60, 0)]);
        block_soil_walking(&mut w, 0, 4, 0);
        assert_eq!(w.get_block_id(0, 4, 0), 3);

        // 12. Lava source ignites supported air neighbors on a 1/4 roll
        // (seed opens with next_int(4) == 0; other faces lack support).
        let mut w = harness(102400);
        stage(&mut w, &[(0, 5, 0, 11, 0), (1, 4, 0, 1, 0)]);
        block_fluid_tick(&mut w, 11, true, 0, 5, 0);
        assert_eq!(w.get_block_id(1, 5, 0), 51);

        // 13. Sapling at growth stage with light grows: action + seed,
        // cleared (seed opens with next_int(5) == 0, then u64 draws).
        let mut w = harness(0);
        stage(&mut w, &[(0, 5, 0, 6, 15), (0, 4, 0, 3, 0)]);
        let act = block_sapling_tick(&mut w, 6, 6, 1, 0, 0, 5, 0);
        assert_eq!(act.kind, SaplingAction::GrowTree as u8);
        assert_eq!(act.seed, 15337379307980049274);
        assert_eq!(w.get_block_id(0, 5, 0), 0);

        // 14. Decayed leaves (meta 1) drop and clear on tick.
        let mut w = harness(7);
        stage(&mut w, &[(0, 5, 0, 18, 1)]);
        let mut guard = 0;
        block_leaves_tick(&mut w, 18, 18, 6, 1, 0, &mut guard, 0, 5, 0);
        assert_eq!(w.get_block_id(0, 5, 0), 0);
        let drops = items_at(&w, 6);
        assert_eq!(drops.len(), 1, "{drops:?}");

        // 15. Reed needs grass/dirt near water (sand never hosts reed,
        // like Java `BlockReed.canPlaceBlockAt`); mushroom over void
        // does not stay.
        let mut w = harness(7);
        stage(&mut w, &[(0, 5, 0, 83, 0), (0, 4, 0, 12, 0), (1, 4, 0, 8, 0), (0, 6, 0, 39, 0)]);
        assert!(!block_reed_can_stay(&mut w, 0, 5, 0));
        w.set_block_id(0, 4, 0, 3);
        assert!(block_reed_can_stay(&mut w, 0, 5, 0));
        assert!(!block_mushroom_can_stay(&mut w, 0, 6, 0));
    }
}
