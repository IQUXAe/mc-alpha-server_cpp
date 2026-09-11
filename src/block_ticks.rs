//! Block behavior ported from C++ `Block.cpp` (which mirrors Java `Block*`).
//!
//! Every per-block event (`onBlockAdded`, `onNeighborBlockChange`,
//! `updateTick`, `canBlockStay`, drops) lives here. C++ keeps the `Block`
//! objects, the global registry, and scheduling plumbing; Rust owns the
//! decisions. World access goes through the `BlockTickWorld` callback table
//! (block storage, light, RNG draws on `World::rand`, entity spawning), so
//! draw sequences and check order are preserved.
//!
//! Parameter notes (kept faithful to C++):
//! - block ids the C++ side already knows (`leaves_id`, `crop_id`, item
//!   ids, `is_lava`) are passed in instead of re-reading the registry.
//! - the leaves recursion guard (`BlockLeaves::field_663_c`) stays a C++
//!   member; Rust takes it as `*mut i32`, exactly where C++ read/wrote it.
//! - sapling growth returns an action: tree generation itself runs in C++
//!   through the existing `WorldAccessor` path.

/// World access for block ticks. All function pointers must be non-null;
/// a null table (or null entry) makes queries return safe defaults and
/// updates become no-ops.
#[repr(C)]
pub struct BlockTickWorld {
    pub next_int: Option<fn(bound: i32) -> i32>,
    pub next_float01: Option<fn() -> f32>,
    pub next_u64: Option<fn() -> u64>,
    pub get_block_id: Option<fn(x: i32, y: i32, z: i32) -> u8>,
    pub get_block_id_nc: Option<fn(x: i32, y: i32, z: i32) -> u8>,
    pub get_block_meta: Option<fn(x: i32, y: i32, z: i32) -> u8>,
    pub set_block: Option<fn(x: i32, y: i32, z: i32, id: u8)>,
    pub set_block_meta: Option<fn(x: i32, y: i32, z: i32, meta: u8)>,
    pub set_block_notify: Option<fn(x: i32, y: i32, z: i32, id: u8)>,
    pub set_block_update: Option<fn(x: i32, y: i32, z: i32, id: u8)>,
    pub set_block_meta_notify: Option<fn(x: i32, y: i32, z: i32, id: u8, meta: u8)>,
    pub set_block_and_meta: Option<fn(x: i32, y: i32, z: i32, id: u8, meta: u8)>,
    pub get_block_light: Option<fn(x: i32, y: i32, z: i32) -> i32>,
    pub can_see_sky: Option<fn(x: i32, y: i32, z: i32) -> bool>,
    /// World::doesBlockAllowAttachment (solid && blocksMovement). Used by fluids.
    pub attach_world: Option<fn(x: i32, y: i32, z: i32) -> bool>,
    /// BlockTorch attachment (solid && collidable). Used by torches.
    pub attach_torch: Option<fn(x: i32, y: i32, z: i32) -> bool>,
    pub is_solid: Option<fn(x: i32, y: i32, z: i32) -> bool>,
    /// NoChunkLoad solid check (leaves ground fix must not force loads).
    pub is_solid_nc: Option<fn(x: i32, y: i32, z: i32) -> bool>,
    pub is_water_or_lava: Option<fn(x: i32, y: i32, z: i32) -> bool>,
    pub is_water: Option<fn(x: i32, y: i32, z: i32) -> bool>,
    pub block_registered: Option<fn(id: u8) -> bool>,
    pub collidable_box: Option<fn(x: i32, y: i32, z: i32) -> bool>,
    pub schedule_update: Option<fn(x: i32, y: i32, z: i32, block_id: u8, delay: i32)>,
    pub mark_update: Option<fn(x: i32, y: i32, z: i32)>,
    pub notify_neighbors: Option<fn(x: i32, y: i32, z: i32, block_id: u8)>,
    pub spawn_drop:
        Option<fn(item_id: i32, count: i32, damage: i32, fx: f64, fy: f64, fz: f64, spread: f64, up: f64)>,
    pub spawn_falling: Option<fn(block_id: u8, fx: f64, fy: f64, fz: f64)>,
    /// Drop the current occupant of a cell via C++ virtual dispatch
    /// (mirrors the drop half of `flowIntoBlock`).
    pub drop_occupant: Option<fn(x: i32, y: i32, z: i32)>,
}

pub const PLANT_GROWTH_STAGE_MAX: u8 = 15;
pub const LEAVES_DECAY_GUARD_MAX: i32 = 100;

/// Sapling tick outcome. `GrowTree` carries the `World::rand()` draw; C++
/// runs the existing `WorldAccessor` tree generation for it.
#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SaplingAction {
    None = 0,
    GrowTree = 1,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct TickAction {
    pub kind: u8,
    pub seed: u64,
}

// ---- internal query/update shims (null-safe) ----

fn q_id(w: &BlockTickWorld, x: i32, y: i32, z: i32) -> u8 {
    w.get_block_id.map(|f| f(x, y, z)).unwrap_or(0)
}
/// Neighbor scans that must not force chunk loads (mirrors NoChunkLoad calls).
fn q_id_nc(w: &BlockTickWorld, x: i32, y: i32, z: i32) -> u8 {
    w.get_block_id_nc.map(|f| f(x, y, z)).unwrap_or(0)
}
fn q_meta(w: &BlockTickWorld, x: i32, y: i32, z: i32) -> u8 {
    w.get_block_meta.map(|f| f(x, y, z)).unwrap_or(0)
}
fn q_light(w: &BlockTickWorld, x: i32, y: i32, z: i32) -> i32 {
    w.get_block_light.map(|f| f(x, y, z)).unwrap_or(0)
}
fn q_sky(w: &BlockTickWorld, x: i32, y: i32, z: i32) -> bool {
    w.can_see_sky.map(|f| f(x, y, z)).unwrap_or(false)
}
fn q_attach_world(w: &BlockTickWorld, x: i32, y: i32, z: i32) -> bool {
    w.attach_world.map(|f| f(x, y, z)).unwrap_or(false)
}
fn q_attach_torch(w: &BlockTickWorld, x: i32, y: i32, z: i32) -> bool {
    w.attach_torch.map(|f| f(x, y, z)).unwrap_or(false)
}
fn q_solid(w: &BlockTickWorld, x: i32, y: i32, z: i32) -> bool {
    w.is_solid.map(|f| f(x, y, z)).unwrap_or(false)
}
fn q_solid_nc(w: &BlockTickWorld, x: i32, y: i32, z: i32) -> bool {
    w.is_solid_nc.map(|f| f(x, y, z)).unwrap_or(false)
}
fn q_water_lava(w: &BlockTickWorld, x: i32, y: i32, z: i32) -> bool {
    w.is_water_or_lava.map(|f| f(x, y, z)).unwrap_or(false)
}
fn q_water(w: &BlockTickWorld, x: i32, y: i32, z: i32) -> bool {
    w.is_water.map(|f| f(x, y, z)).unwrap_or(false)
}
fn q_registered(w: &BlockTickWorld, id: u8) -> bool {
    w.block_registered.map(|f| f(id)).unwrap_or(false)
}
fn q_collidable(w: &BlockTickWorld, x: i32, y: i32, z: i32) -> bool {
    w.collidable_box.map(|f| f(x, y, z)).unwrap_or(false)
}
fn u_set_meta(w: &BlockTickWorld, x: i32, y: i32, z: i32, meta: u8) {
    if let Some(f) = w.set_block_meta {
        f(x, y, z, meta);
    }
}
fn u_set_notify(w: &BlockTickWorld, x: i32, y: i32, z: i32, id: u8) {
    if let Some(f) = w.set_block_notify {
        f(x, y, z, id);
    }
}
fn u_set_update(w: &BlockTickWorld, x: i32, y: i32, z: i32, id: u8) {
    if let Some(f) = w.set_block_update {
        f(x, y, z, id);
    }
}
fn u_set_meta_notify(w: &BlockTickWorld, x: i32, y: i32, z: i32, id: u8, meta: u8) {
    if let Some(f) = w.set_block_meta_notify {
        f(x, y, z, id, meta);
    }
}
fn u_set_and_meta(w: &BlockTickWorld, x: i32, y: i32, z: i32, id: u8, meta: u8) {
    if let Some(f) = w.set_block_and_meta {
        f(x, y, z, id, meta);
    }
}
fn u_schedule(w: &BlockTickWorld, x: i32, y: i32, z: i32, id: u8, delay: i32) {
    if let Some(f) = w.schedule_update {
        f(x, y, z, id, delay);
    }
}
fn u_mark(w: &BlockTickWorld, x: i32, y: i32, z: i32) {
    if let Some(f) = w.mark_update {
        f(x, y, z);
    }
}
fn u_notify(w: &BlockTickWorld, x: i32, y: i32, z: i32, id: u8) {
    if let Some(f) = w.notify_neighbors {
        f(x, y, z, id);
    }
}
fn u_drop(w: &BlockTickWorld, item: i32, count: i32, damage: i32, fx: f64, fy: f64, fz: f64, spread: f64, up: f64) {
    if count <= 0 {
        return;
    }
    if let Some(f) = w.spawn_drop {
        f(item, count, damage, fx, fy, fz, spread, up);
    }
}
fn rng_int(w: &BlockTickWorld, bound: i32) -> i32 {
    w.next_int.map(|f| f(bound)).unwrap_or(0)
}
fn rng_f01(w: &BlockTickWorld) -> f32 {
    w.next_float01.map(|f| f()).unwrap_or(0.0)
}
fn rng_u64(w: &BlockTickWorld) -> u64 {
    w.next_u64.map(|f| f()).unwrap_or(0)
}
fn chance_one_in(w: &BlockTickWorld, one_in: i32) -> bool {
    if one_in <= 1 {
        return true;
    }
    rng_int(w, one_in) == 0
}

/// Base drop (`Block::dropBlockAsItemWithChance`): id/count/damage already
/// resolved by C++ virtuals. Returns whether anything was spawned.
fn base_drop(
    w: &BlockTickWorld,
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

fn sand_schedule(w: &BlockTickWorld, block_id: u8, x: i32, y: i32, z: i32) {
    u_schedule(w, x, y, z, block_id, 3);
}

pub fn block_sand_added(world: &BlockTickWorld, block_id: u8, x: i32, y: i32, z: i32) {
    let w = world;
    sand_schedule(w, block_id, x, y, z);
}

pub fn block_sand_neighbor(world: &BlockTickWorld, block_id: u8, x: i32, y: i32, z: i32) {
    let w = world;
    sand_schedule(w, block_id, x, y, z);
}

fn sand_can_fall_below(w: &BlockTickWorld, x: i32, y: i32, z: i32) -> bool {
    let id = q_id(w, x, y, z);
    if id == 0 || id == 51 {
        return true;
    }
    if !q_registered(w, id) {
        return true;
    }
    q_water_lava(w, x, y, z)
}

pub fn block_sand_tick(world: &BlockTickWorld, block_id: u8, x: i32, y: i32, z: i32) {
    let w = world;
    if y >= 0 && sand_can_fall_below(w, x, y - 1, z) {
        u_set_update(w, x, y, z, 0);
        if let Some(f) = w.spawn_falling {
            f(block_id, x as f64 + 0.5, y as f64 + 0.5, z as f64 + 0.5);
        }
    }
}

// ---- fluid ----

const FLOW_DIRS: [(i32, i32, i32); 6] = [(1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)];
const FLOW_PASSABLE: [u8; 10] = [37, 38, 39, 40, 50, 51, 55, 59, 83, 78];

fn fluid_can_flow_into(w: &BlockTickWorld, x: i32, y: i32, z: i32) -> bool {
    let id = q_id(w, x, y, z);
    if id == 0 {
        return true;
    }
    if id == 8 || id == 9 || id == 10 || id == 11 {
        return false;
    }
    FLOW_PASSABLE.contains(&id)
}

pub fn block_fluid_added(world: &BlockTickWorld, block_id: u8, tick_rate: i32, x: i32, y: i32, z: i32) {
    let w = world;
    u_schedule(w, x, y, z, block_id, tick_rate);
}

pub fn block_fluid_neighbor(world: &BlockTickWorld, block_id: u8, tick_rate: i32, x: i32, y: i32, z: i32) {
    let w = world;
    u_schedule(w, x, y, z, block_id, tick_rate);
}

pub fn block_fluid_tick(
    world: &BlockTickWorld,
    block_id: u8,
    is_lava: bool,
    x: i32,
    y: i32,
    z: i32,
) {
    let w = world;
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
                        if let Some(f) = w.drop_occupant {
                            f(nx, y, nz);
                        }
                    }
                    u_set_meta_notify(w, nx, y, nz, block_id, new_meta);
                }
            }
        }
    }
}

// ---- flower ----

fn flower_can_stay_here(w: &BlockTickWorld, x: i32, y: i32, z: i32) -> bool {
    let below = q_id(w, x, y - 1, z);
    (q_light(w, x, y, z) >= 8 || q_sky(w, x, y, z)) && (below == 2 || below == 3 || below == 60)
}

pub fn block_flower_can_stay(world: &BlockTickWorld, x: i32, y: i32, z: i32) -> bool {
    flower_can_stay_here(world, x, y, z)
}

pub fn block_flower_neighbor(
    world: &BlockTickWorld,
    drop_id: i32,
    drop_count: i32,
    drop_damage: i32,
    x: i32,
    y: i32,
    z: i32,
) {
    let w = world;
    if !flower_can_stay_here(w, x, y, z) {
        base_drop(w, drop_id, drop_count, drop_damage, x, y, z, 1.0);
        u_set_update(w, x, y, z, 0);
    }
}

pub fn block_flower_tick(
    world: &BlockTickWorld,
    drop_id: i32,
    drop_count: i32,
    drop_damage: i32,
    x: i32,
    y: i32,
    z: i32,
) {
    let w = world;
    if !flower_can_stay_here(w, x, y, z) {
        base_drop(w, drop_id, drop_count, drop_damage, x, y, z, 1.0);
        u_set_notify(w, x, y, z, 0);
    }
}

// ---- tall grass (drop only; stay logic inherited from flower) ----

pub fn block_tallgrass_drop(
    world: &BlockTickWorld,
    seeds_id: i32,
    chance: f32,
    x: i32,
    y: i32,
    z: i32,
) {
    if seeds_id <= 0 {
        return;
    }
    let w = world;
    if rng_f01(w) > chance {
        return;
    }
    if !chance_one_in(w, 8) {
        return;
    }
    u_drop(w, seeds_id, 1, 0, x as f64 + 0.5, y as f64 + 0.5, z as f64 + 0.5, 0.05, 0.15);
}

// ---- mushroom ----

fn mushroom_can_stay_here(w: &BlockTickWorld, x: i32, y: i32, z: i32) -> bool {
    let below = q_id(w, x, y - 1, z);
    below > 0 && q_registered(w, below)
}

pub fn block_mushroom_can_stay(world: &BlockTickWorld, x: i32, y: i32, z: i32) -> bool {
    mushroom_can_stay_here(world, x, y, z)
}

pub fn block_mushroom_neighbor(
    world: &BlockTickWorld,
    drop_id: i32,
    drop_count: i32,
    drop_damage: i32,
    x: i32,
    y: i32,
    z: i32,
) {
    let w = world;
    if !mushroom_can_stay_here(w, x, y, z) {
        base_drop(w, drop_id, drop_count, drop_damage, x, y, z, 1.0);
        u_set_update(w, x, y, z, 0);
    }
}

// ---- torch ----

fn torch_attached(w: &BlockTickWorld, x: i32, y: i32, z: i32) -> bool {
    q_attach_torch(w, x, y, z)
}

/// Java BlockTorch.onBlockPlaced metadata from the clicked face.
/// Takes the torch position; side: 1=floor(default 5),2,3,4,5 wall faces.

pub fn block_torch_attach_meta(world: &BlockTickWorld, side: i32, x: i32, y: i32, z: i32) -> u8 {
    let w = world;
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

pub fn block_torch_added(world: &BlockTickWorld, block_id: u8, x: i32, y: i32, z: i32) {
    let w = world;
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

fn torch_can_stay_here(w: &BlockTickWorld, x: i32, y: i32, z: i32) -> bool {
    torch_attached(w, x - 1, y, z)
        || torch_attached(w, x + 1, y, z)
        || torch_attached(w, x, y, z - 1)
        || torch_attached(w, x, y, z + 1)
        || torch_attached(w, x, y - 1, z)
}

pub fn block_torch_can_stay(world: &BlockTickWorld, x: i32, y: i32, z: i32) -> bool {
    torch_can_stay_here(world, x, y, z)
}

pub fn block_torch_neighbor(
    world: &BlockTickWorld,
    drop_id: i32,
    drop_count: i32,
    drop_damage: i32,
    x: i32,
    y: i32,
    z: i32,
) {
    let w = world;
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

fn cactus_can_stay_here(w: &BlockTickWorld, x: i32, y: i32, z: i32) -> bool {
    if q_solid(w, x - 1, y, z) || q_solid(w, x + 1, y, z) || q_solid(w, x, y, z - 1) || q_solid(w, x, y, z + 1) {
        return false;
    }
    let below = q_id(w, x, y - 1, z);
    below == 12 || below == 81
}

fn reed_can_stay_here(w: &BlockTickWorld, x: i32, y: i32, z: i32) -> bool {
    let below = q_id(w, x, y - 1, z);
    if below == 83 {
        return true;
    }
    if below != 2 && below != 3 && below != 12 {
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
fn stalk_tick(w: &BlockTickWorld, block_id: u8, can_stay: bool, x: i32, y: i32, z: i32) {
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

pub fn block_cactus_can_stay(world: &BlockTickWorld, x: i32, y: i32, z: i32) -> bool {
    cactus_can_stay_here(world, x, y, z)
}

pub fn block_reed_can_stay(world: &BlockTickWorld, x: i32, y: i32, z: i32) -> bool {
    reed_can_stay_here(world, x, y, z)
}

pub fn block_cactus_added(world: &BlockTickWorld, block_id: u8, x: i32, y: i32, z: i32) {
    u_schedule(world, x, y, z, block_id, 20);
}

pub fn block_reed_added(world: &BlockTickWorld, block_id: u8, x: i32, y: i32, z: i32) {
    u_schedule(world, x, y, z, block_id, 20);
}

pub fn block_cactus_neighbor(
    world: &BlockTickWorld,
    block_id: u8,
    drop_id: i32,
    drop_count: i32,
    drop_damage: i32,
    x: i32,
    y: i32,
    z: i32,
) {
    let w = world;
    if !cactus_can_stay_here(w, x, y, z) {
        base_drop(w, drop_id, drop_count, drop_damage, x, y, z, 1.0);
        u_set_notify(w, x, y, z, 0);
        return;
    }
    u_schedule(w, x, y, z, block_id, 20);
}

pub fn block_reed_neighbor(
    world: &BlockTickWorld,
    block_id: u8,
    drop_id: i32,
    drop_count: i32,
    drop_damage: i32,
    x: i32,
    y: i32,
    z: i32,
) {
    let w = world;
    if !reed_can_stay_here(w, x, y, z) {
        base_drop(w, drop_id, drop_count, drop_damage, x, y, z, 1.0);
        u_set_notify(w, x, y, z, 0);
        return;
    }
    u_schedule(w, x, y, z, block_id, 20);
}

pub fn block_cactus_tick(
    world: &BlockTickWorld,
    block_id: u8,
    drop_id: i32,
    drop_count: i32,
    drop_damage: i32,
    x: i32,
    y: i32,
    z: i32,
) {
    let w = world;
    if !cactus_can_stay_here(w, x, y, z) {
        base_drop(w, drop_id, drop_count, drop_damage, x, y, z, 1.0);
        u_set_notify(w, x, y, z, 0);
        return;
    }
    stalk_tick(w, block_id, true, x, y, z);
}

pub fn block_reed_tick(
    world: &BlockTickWorld,
    block_id: u8,
    drop_id: i32,
    drop_count: i32,
    drop_damage: i32,
    x: i32,
    y: i32,
    z: i32,
) {
    let w = world;
    if !reed_can_stay_here(w, x, y, z) {
        base_drop(w, drop_id, drop_count, drop_damage, x, y, z, 1.0);
        u_set_notify(w, x, y, z, 0);
        return;
    }
    stalk_tick(w, block_id, true, x, y, z);
}

// ---- leaves (counter lives in C++, passed by pointer) ----

fn leaf_propagate(w: &BlockTickWorld, leaves_id: u8, x: i32, y: i32, z: i32, current: i32) -> i32 {
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

fn update_leaf_distance(w: &BlockTickWorld, block_id: u8, leaves_id: u8, x: i32, y: i32, z: i32, guard: &mut i32) {
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
        u_mark(w, x, y, z);
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
        u_mark(w, x, y, z);
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
    w: &BlockTickWorld,
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

pub fn block_leaves_added(world: &BlockTickWorld, block_id: u8, x: i32, y: i32, z: i32) {
    u_schedule(world, x, y, z, block_id, 40);
}

pub fn block_leaves_neighbor(
    world: &BlockTickWorld,
    block_id: u8,
    leaves_id: u8,
    guard: &mut i32,
    x: i32,
    y: i32,
    z: i32,
) {
    let w = world;
    *guard = 0;
    update_leaf_distance(w, block_id, leaves_id, x, y, z, guard);
}

pub fn block_leaves_tick(
    world: &BlockTickWorld,
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
    let w = world;
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
    world: &BlockTickWorld,
    sapling_id: i32,
    chance: f32,
    x: i32,
    y: i32,
    z: i32,
) {
    if sapling_id <= 0 {
        return;
    }
    let w = world;
    if rng_f01(w) > chance {
        return;
    }
    if !chance_one_in(w, 20) {
        return;
    }
    u_drop(w, sapling_id, 1, 0, x as f64 + 0.5, y as f64 + 0.5, z as f64 + 0.5, 0.05, 0.15);
}

// ---- sapling ----

fn sapling_can_stay_here(w: &BlockTickWorld, x: i32, y: i32, z: i32) -> bool {
    let below = q_id(w, x, y - 1, z);
    (q_light(w, x, y, z) >= 8 || q_sky(w, x, y, z)) && (below == 2 || below == 3 || below == 60)
}

pub fn block_sapling_added(world: &BlockTickWorld, block_id: u8, x: i32, y: i32, z: i32) {
    u_schedule(world, x, y, z, block_id, 100);
}

pub fn block_sapling_can_stay(world: &BlockTickWorld, x: i32, y: i32, z: i32) -> bool {
    sapling_can_stay_here(world, x, y, z)
}

pub fn block_sapling_neighbor(
    world: &BlockTickWorld,
    block_id: u8,
    drop_id: i32,
    drop_count: i32,
    drop_damage: i32,
    x: i32,
    y: i32,
    z: i32,
) {
    let w = world;
    if !sapling_can_stay_here(w, x, y, z) {
        base_drop(w, drop_id, drop_count, drop_damage, x, y, z, 1.0);
        u_set_notify(w, x, y, z, 0);
        return;
    }
    u_schedule(w, x, y, z, block_id, 100);
}

/// Sapling tick. Returns GrowTree (with the rand draw) when C++ must run
/// tree generation; C++ restores the sapling itself if generation fails.
pub fn block_sapling_tick(
    world: &BlockTickWorld,
    block_id: u8,
    drop_id: i32,
    drop_count: i32,
    drop_damage: i32,
    x: i32,
    y: i32,
    z: i32,
) -> TickAction {
    let none = TickAction { kind: SaplingAction::None as u8, seed: 0 };
    let w = world;
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
        u_mark(w, x, y, z);
        u_schedule(w, x, y, z, block_id, 100);
        return none;
    }
    let seed = rng_u64(w);
    u_set_notify(w, x, y, z, 0);
    TickAction { kind: SaplingAction::GrowTree as u8, seed }
}

// ---- crops ----

fn crops_can_stay_here(w: &BlockTickWorld, crop_id: u8, x: i32, y: i32, z: i32) -> bool {
    let _ = crop_id;
    q_id(w, x, y - 1, z) == 60 && (q_light(w, x, y, z) >= 8 || q_sky(w, x, y, z))
}

fn crops_growth_rate(w: &BlockTickWorld, crop_id: u8, x: i32, y: i32, z: i32) -> f32 {
    let mut rate = 1.0f32;
    for dx in -1..=1 {
        for dz in -1..=1 {
            if q_id(w, x + dx, y - 1, z + dz) == 60 {
                let mut bonus = if dx == 0 && dz == 0 { 3.0 } else { 1.0 };
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

pub fn block_crops_added(world: &BlockTickWorld, block_id: u8, x: i32, y: i32, z: i32) {
    u_schedule(world, x, y, z, block_id, 20);
}

pub fn block_crops_can_stay(world: &BlockTickWorld, crop_id: u8, x: i32, y: i32, z: i32) -> bool {
    crops_can_stay_here(world, crop_id, x, y, z)
}

pub fn block_crops_neighbor(
    world: &BlockTickWorld,
    block_id: u8,
    crop_id: u8,
    wheat_id: i32,
    seeds_id: i32,
    x: i32,
    y: i32,
    z: i32,
) {
    let w = world;
    if !crops_can_stay_here(w, crop_id, x, y, z) {
        block_crops_drop(w, wheat_id, seeds_id, x, y, z, q_meta(w, x, y, z));
        u_set_notify(w, x, y, z, 0);
        return;
    }
    u_schedule(w, x, y, z, block_id, 20);
}

fn block_crops_drop(w: &BlockTickWorld, wheat_id: i32, seeds_id: i32, x: i32, y: i32, z: i32, metadata: u8) {
    if rng_f01(w) > 1.0 {
        return;
    }
    // Mature: wheat + 1..3 seeds; else one seed. Velocities match base drops.
    if metadata >= 7 {
        u_drop(w, wheat_id, 1, 0, x as f64 + 0.5, y as f64 + 0.5, z as f64 + 0.5, 0.05, 0.15);
        let extra = rng_int(w, 3);
        u_drop(w, seeds_id, 1 + extra, 0, x as f64 + 0.5, y as f64 + 0.5, z as f64 + 0.5, 0.05, 0.15);
    } else {
        u_drop(w, seeds_id, 1, 0, x as f64 + 0.5, y as f64 + 0.5, z as f64 + 0.5, 0.05, 0.15);
    }
}

pub fn block_crops_drop_ffi(
    world: &BlockTickWorld,
    wheat_id: i32,
    seeds_id: i32,
    x: i32,
    y: i32,
    z: i32,
    metadata: u8,
    chance: f32,
) {
    let w = world;
    if rng_f01(w) > chance {
        return;
    }
    // Reuse the mature/immature split with the caller's chance gate.
    if metadata >= 7 {
        u_drop(w, wheat_id, 1, 0, x as f64 + 0.5, y as f64 + 0.5, z as f64 + 0.5, 0.05, 0.15);
        let extra = rng_int(w, 3);
        u_drop(w, seeds_id, 1 + extra, 0, x as f64 + 0.5, y as f64 + 0.5, z as f64 + 0.5, 0.05, 0.15);
    } else {
        u_drop(w, seeds_id, 1, 0, x as f64 + 0.5, y as f64 + 0.5, z as f64 + 0.5, 0.05, 0.15);
    }
}

pub fn block_crops_tick(
    world: &BlockTickWorld,
    block_id: u8,
    crop_id: u8,
    wheat_id: i32,
    seeds_id: i32,
    x: i32,
    y: i32,
    z: i32,
) {
    let w = world;
    if !crops_can_stay_here(w, crop_id, x, y, z) {
        block_crops_drop(w, wheat_id, seeds_id, x, y, z, q_meta(w, x, y, z));
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
                u_mark(w, x, y, z);
            }
        }
    }
    u_schedule(w, x, y, z, block_id, 20);
}

// ---- soil ----

fn soil_has_water(w: &BlockTickWorld, x: i32, y: i32, z: i32) -> bool {
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

fn soil_set_moisture(w: &BlockTickWorld, x: i32, y: i32, z: i32, moisture: u8) {
    if q_meta(w, x, y, z) == moisture {
        return;
    }
    u_set_meta(w, x, y, z, moisture);
    u_mark(w, x, y, z);
}

pub fn block_soil_added(world: &BlockTickWorld, block_id: u8, x: i32, y: i32, z: i32) {
    u_schedule(world, x, y, z, block_id, 20);
}

pub fn block_soil_tick(world: &BlockTickWorld, block_id: u8, x: i32, y: i32, z: i32) {
    let w = world;
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

pub fn block_soil_walking(world: &BlockTickWorld, x: i32, y: i32, z: i32) {
    let w = world;
    if chance_one_in(w, 4) {
        u_set_notify(w, x, y, z, 3);
    }
}

pub fn block_soil_neighbor(world: &BlockTickWorld, block_id: u8, x: i32, y: i32, z: i32) {
    let w = world;
    if q_collidable(w, x, y + 1, z) {
        u_set_notify(w, x, y, z, 3);
        return;
    }
    u_schedule(w, x, y, z, block_id, 20);
}

// ---- base drop entry point (Block::dropBlockAsItemWithChance) ----

pub fn block_base_drop(
    world: &BlockTickWorld,
    item_id: i32,
    count: i32,
    damage: i32,
    x: i32,
    y: i32,
    z: i32,
    chance: f32,
) -> bool {
    base_drop(world, item_id, count, damage, x, y, z, chance)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashMap;
    use std::sync::{Mutex, MutexGuard};

    struct Fake {
        blocks: HashMap<(i32, i32, i32), (u8, u8)>,
        light: HashMap<(i32, i32, i32), i32>,
        sky: bool,
        attach: HashMap<(i32, i32, i32), bool>,
        solid: HashMap<(i32, i32, i32), bool>,
        water_lava: HashMap<(i32, i32, i32), bool>,
        water: HashMap<(i32, i32, i32), bool>,
        unregistered: Vec<u8>,
        log: Vec<String>,
        int_script: Vec<i32>,
        int_pos: usize,
        f01: f32,
        u64_val: u64,
    }

    impl Fake {
        fn fresh() -> Self {
            Self {
                blocks: HashMap::new(),
                light: HashMap::new(),
                sky: true,
                attach: HashMap::new(),
                solid: HashMap::new(),
                water_lava: HashMap::new(),
                water: HashMap::new(),
                unregistered: Vec::new(),
                log: Vec::new(),
                int_script: Vec::new(),
                int_pos: 0,
                f01: 0.0,
                u64_val: 12345,
            }
        }
        fn id(&self, x: i32, y: i32, z: i32) -> u8 {
            self.blocks.get(&(x, y, z)).map(|b| b.0).unwrap_or(0)
        }
    }

    static FAKE: Mutex<Option<Fake>> = Mutex::new(None);

    fn fake() -> MutexGuard<'static, Option<Fake>> {
        match FAKE.lock() {
            Ok(g) => g,
            Err(poisoned) => poisoned.into_inner(),
        }
    }

    fn reset() {
        *fake() = Some(Fake::fresh());
    }

    fn s_next_int(bound: i32) -> i32 {
        let mut g = fake();
        let f = g.as_mut().unwrap_or_else(|| unreachable!());
        if f.int_pos < f.int_script.len() {
            let v = f.int_script[f.int_pos];
            f.int_pos += 1;
            v % bound
        } else {
            0
        }
    }
    fn s_next_f01() -> f32 {
        fake().as_ref().map(|f| f.f01).unwrap_or(0.0)
    }
    fn s_next_u64() -> u64 {
        fake().as_ref().map(|f| f.u64_val).unwrap_or(0)
    }
    fn s_get_id(x: i32, y: i32, z: i32) -> u8 {
        fake().as_ref().map(|f| f.id(x, y, z)).unwrap_or(0)
    }
    fn s_get_id_nc(x: i32, y: i32, z: i32) -> u8 {
        s_get_id(x, y, z)
    }
    fn s_get_meta(x: i32, y: i32, z: i32) -> u8 {
        fake().as_ref().and_then(|f| f.blocks.get(&(x, y, z)).map(|b| b.1)).unwrap_or(0)
    }
    fn s_set(x: i32, y: i32, z: i32, id: u8) {
        if let Some(f) = fake().as_mut() {
            f.blocks.insert((x, y, z), (id, 0));
            f.log.push(format!("set {x} {y} {z} {id}"));
        }
    }
    fn s_set_meta(x: i32, y: i32, z: i32, meta: u8) {
        if let Some(f) = fake().as_mut() {
            let id = f.id(x, y, z);
            f.blocks.insert((x, y, z), (id, meta));
            f.log.push(format!("meta {x} {y} {z} {meta}"));
        }
    }
    fn s_set_notify(x: i32, y: i32, z: i32, id: u8) {
        if let Some(f) = fake().as_mut() {
            f.blocks.insert((x, y, z), (id, 0));
            f.log.push(format!("notify {x} {y} {z} {id}"));
        }
    }
    fn s_set_update(x: i32, y: i32, z: i32, id: u8) {
        if let Some(f) = fake().as_mut() {
            f.blocks.insert((x, y, z), (id, 0));
            f.log.push(format!("update {x} {y} {z} {id}"));
        }
    }
    fn s_set_meta_notify(x: i32, y: i32, z: i32, id: u8, meta: u8) {
        if let Some(f) = fake().as_mut() {
            f.blocks.insert((x, y, z), (id, meta));
            f.log.push(format!("metanotify {x} {y} {z} {id} {meta}"));
        }
    }
    fn s_set_and_meta(x: i32, y: i32, z: i32, id: u8, meta: u8) {
        if let Some(f) = fake().as_mut() {
            f.blocks.insert((x, y, z), (id, meta));
            f.log.push(format!("setmeta {x} {y} {z} {id} {meta}"));
        }
    }
    fn s_light(x: i32, y: i32, z: i32) -> i32 {
        fake().as_ref().and_then(|f| f.light.get(&(x, y, z)).copied()).unwrap_or(15)
    }
    fn s_sky(_x: i32, _y: i32, _z: i32) -> bool {
        fake().as_ref().map(|f| f.sky).unwrap_or(true)
    }
    fn s_attach(x: i32, y: i32, z: i32) -> bool {
        fake().as_ref().and_then(|f| f.attach.get(&(x, y, z)).copied()).unwrap_or(false)
    }
    fn s_solid(x: i32, y: i32, z: i32) -> bool {
        fake().as_ref().and_then(|f| f.solid.get(&(x, y, z)).copied()).unwrap_or(false)
    }
    fn s_water_lava(x: i32, y: i32, z: i32) -> bool {
        fake().as_ref().and_then(|f| f.water_lava.get(&(x, y, z)).copied()).unwrap_or(false)
    }
    fn s_water(x: i32, y: i32, z: i32) -> bool {
        fake().as_ref().and_then(|f| f.water.get(&(x, y, z)).copied()).unwrap_or(false)
    }
    fn s_registered(id: u8) -> bool {
        fake().as_ref().map(|f| !f.unregistered.contains(&id)).unwrap_or(true)
    }
    fn s_collidable(x: i32, y: i32, z: i32) -> bool {
        s_solid(x, y, z)
    }
    fn s_schedule(x: i32, y: i32, z: i32, id: u8, delay: i32) {
        if let Some(f) = fake().as_mut() {
            f.log.push(format!("sched {x} {y} {z} {id} {delay}"));
        }
    }
    fn s_mark(x: i32, y: i32, z: i32) {
        if let Some(f) = fake().as_mut() {
            f.log.push(format!("mark {x} {y} {z}"));
        }
    }
    fn s_notify(x: i32, y: i32, z: i32, id: u8) {
        if let Some(f) = fake().as_mut() {
            f.log.push(format!("neigh {x} {y} {z} {id}"));
        }
    }
    fn s_drop(item: i32, count: i32, damage: i32, fx: f64, fy: f64, fz: f64, _sp: f64, _up: f64) {
        if let Some(f) = fake().as_mut() {
            f.log.push(format!("drop {item} {count} {damage} {fx:.1} {fy:.1} {fz:.1}"));
        }
    }
    fn s_falling(id: u8, fx: f64, fy: f64, fz: f64) {
        if let Some(f) = fake().as_mut() {
            f.log.push(format!("fall {id} {fx:.1} {fy:.1} {fz:.1}"));
        }
    }
    fn s_drop_occ(x: i32, y: i32, z: i32) {
        if let Some(f) = fake().as_mut() {
            f.log.push(format!("occ {x} {y} {z}"));
        }
    }

    fn table() -> BlockTickWorld {
        BlockTickWorld {
            next_int: Some(s_next_int),
            next_float01: Some(s_next_f01),
            next_u64: Some(s_next_u64),
            get_block_id: Some(s_get_id),
            get_block_id_nc: Some(s_get_id_nc),
            get_block_meta: Some(s_get_meta),
            set_block: Some(s_set),
            set_block_meta: Some(s_set_meta),
            set_block_notify: Some(s_set_notify),
            set_block_update: Some(s_set_update),
            set_block_meta_notify: Some(s_set_meta_notify),
            set_block_and_meta: Some(s_set_and_meta),
            get_block_light: Some(s_light),
            can_see_sky: Some(s_sky),
            attach_world: Some(s_attach),
            attach_torch: Some(s_attach),
            is_solid: Some(s_solid),
            is_solid_nc: Some(s_solid),
            is_water_or_lava: Some(s_water_lava),
            is_water: Some(s_water),
            block_registered: Some(s_registered),
            collidable_box: Some(s_collidable),
            schedule_update: Some(s_schedule),
            mark_update: Some(s_mark),
            notify_neighbors: Some(s_notify),
            spawn_drop: Some(s_drop),
            spawn_falling: Some(s_falling),
            drop_occupant: Some(s_drop_occ),
        }
    }

    fn logs() -> Vec<String> {
        fake().as_ref().map(|f| f.log.clone()).unwrap_or_default()
    }

    // All scenarios run sequentially here: they share the stub counters.
    #[test]
    fn test_block_scenarios() {
        let t = table();
        let tp = &t;

        // 1. Sand falls into air: clears itself and spawns the entity.
        reset();
        let _ = fake().as_mut().map(|f| {
            f.blocks.insert((0, 5, 0), (12, 0));
        });
        block_sand_tick(tp, 12, 0, 5, 0);
        let l = logs();
        assert!(l.contains(&"update 0 5 0 0".to_string()), "{l:?}");
        assert!(l.contains(&"fall 12 0.5 5.5 0.5".to_string()), "{l:?}");

        // 2. Sand on solid ground: nothing happens.
        reset();
        let _ = fake().as_mut().map(|f| {
            f.blocks.insert((0, 5, 0), (12, 0));
            f.solid.insert((0, 4, 0), true);
            f.blocks.insert((0, 4, 0), (1, 0));
        });
        block_sand_tick(tp, 12, 0, 5, 0);
        assert!(logs().is_empty(), "{:?}", logs());

        // 3. Cactus grows at age 15 with headroom: new block above, age reset.
        reset();
        let _ = fake().as_mut().map(|f| {
            f.blocks.insert((0, 1, 0), (81, 15));
            f.blocks.insert((0, 0, 0), (12, 0));
        });
        block_cactus_tick(tp, 81, 81, 1, 0, 0, 1, 0);
        let l = logs();
        assert!(l.contains(&"notify 0 2 0 81".to_string()), "{l:?}");
        assert!(l.contains(&"meta 0 1 0 0".to_string()), "{l:?}");

        // 4. Cactus at max height 3: no growth, only reschedule.
        reset();
        let _ = fake().as_mut().map(|f| {
            f.blocks.insert((0, 3, 0), (81, 15));
            f.blocks.insert((0, 2, 0), (81, 0));
            f.blocks.insert((0, 1, 0), (81, 0));
            f.blocks.insert((0, 0, 0), (12, 0));
        });
        block_cactus_tick(tp, 81, 81, 1, 0, 0, 3, 0);
        let l = logs();
        assert!(!l.iter().any(|e| e.starts_with("notify 0 4")), "{l:?}");
        assert!(l.iter().any(|e| e.starts_with("sched 0 3 0 81")), "{l:?}");

        // 5. Flower on stone without light: uprooted on neighbor change.
        reset();
        let _ = fake().as_mut().map(|f| {
            f.blocks.insert((0, 5, 0), (37, 0));
            f.blocks.insert((0, 4, 0), (1, 0));
            f.light.insert((0, 5, 0), 0);
            f.sky = false;
        });
        block_flower_neighbor(tp, 37, 1, 0, 0, 5, 0);
        let l = logs();
        assert!(l.iter().any(|e| e.starts_with("drop 37 1 0")), "{l:?}");
        assert!(l.contains(&"update 0 5 0 0".to_string()), "{l:?}");

        // 6. Torch meta 1 with no western support: detaches.
        reset();
        let _ = fake().as_mut().map(|f| {
            f.blocks.insert((0, 5, 0), (50, 1));
        });
        block_torch_neighbor(tp, 50, 1, 0, 0, 5, 0);
        let l = logs();
        assert!(l.iter().any(|e| e.starts_with("drop 50 1 0")), "{l:?}");
        assert!(l.contains(&"notify 0 5 0 0".to_string()), "{l:?}");

        // 7. Torch placement picks wall metadata (side 4 = west face => meta 2).
        reset();
        let _ = fake().as_mut().map(|f| {
            f.attach.insert((1, 5, 0), true);
        });
        let meta = block_torch_attach_meta(tp, 4, 0, 5, 0);
        assert_eq!(meta, 2);

        // 8. Crops grow one stage when the roll succeeds.
        reset();
        let _ = fake().as_mut().map(|f| {
            f.blocks.insert((0, 5, 0), (59, 3));
            f.blocks.insert((0, 4, 0), (60, 0));
            f.int_script = vec![0];
        });
        block_crops_tick(tp, 59, 59, 296, 295, 0, 5, 0);
        let l = logs();
        assert!(l.contains(&"meta 0 5 0 4".to_string()), "{l:?}");

        // 9. Mature crops drop wheat plus seeds.
        reset();
        let _ = fake().as_mut().map(|f| {
            f.blocks.insert((0, 5, 0), (59, 7));
            f.int_script = vec![1];
        });
        block_crops_drop_ffi(tp, 296, 295, 0, 5, 0, 7, 1.0);
        let l = logs();
        assert!(l.iter().any(|e| e.starts_with("drop 296 1 0")), "{l:?}");
        assert!(l.iter().any(|e| e.starts_with("drop 295 2 0")), "{l:?}");

        // 10. Dry soil without crops reverts to dirt.
        reset();
        let _ = fake().as_mut().map(|f| {
            f.blocks.insert((0, 4, 0), (60, 0));
            f.int_script = vec![0];
        });
        block_soil_tick(tp, 60, 0, 4, 0);
        assert!(logs().contains(&"notify 0 4 0 3".to_string()), "{:?}", logs());

        // 11. Trampled soil reverts on a 1/4 roll.
        reset();
        let _ = fake().as_mut().map(|f| {
            f.int_script = vec![0];
        });
        block_soil_walking(tp, 0, 4, 0);
        assert!(logs().contains(&"notify 0 4 0 3".to_string()), "{:?}", logs());

        // 12. Lava source ignites supported air neighbors on a 1/4 roll.
        reset();
        let _ = fake().as_mut().map(|f| {
            f.blocks.insert((0, 5, 0), (11, 0));
            f.attach.insert((1, 4, 0), true);
            f.int_script = vec![0, 1, 1, 1, 1, 1];
        });
        block_fluid_tick(tp, 11, true, 0, 5, 0);
        assert!(logs().contains(&"notify 1 5 0 51".to_string()), "{:?}", logs());

        // 13. Sapling at growth stage with light grows: action + seed, cleared.
        reset();
        let _ = fake().as_mut().map(|f| {
            f.blocks.insert((0, 5, 0), (6, 15));
            f.blocks.insert((0, 4, 0), (2, 0));
            f.light.insert((0, 6, 0), 15);
            f.int_script = vec![0];
            f.u64_val = 777;
        });
        let act = block_sapling_tick(tp, 6, 6, 1, 0, 0, 5, 0);
        assert_eq!(act.kind, SaplingAction::GrowTree as u8);
        assert_eq!(act.seed, 777);
        assert!(logs().contains(&"notify 0 5 0 0".to_string()), "{:?}", logs());

        // 14. Decayed leaves (meta 1) drop and clear on tick.
        reset();
        let _ = fake().as_mut().map(|f| {
            f.blocks.insert((0, 5, 0), (18, 1));
        });
        let mut guard = 0;
        block_leaves_tick(tp, 18, 18, 6, 1, 0, &mut guard, 0, 5, 0);
        let l = logs();
        assert!(l.iter().any(|e| e.starts_with("drop 6 1 0")), "{l:?}");
        assert!(l.contains(&"notify 0 5 0 0".to_string()), "{l:?}");

        // 15. Reed on sand near water stays; mushroom over void does not.
        reset();
        let _ = fake().as_mut().map(|f| {
            f.blocks.insert((0, 5, 0), (83, 0));
            f.blocks.insert((0, 4, 0), (12, 0));
            f.blocks.insert((-1, 4, 0), (8, 0));
            f.blocks.insert((0, 6, 0), (39, 0));
        });
        assert!(block_reed_can_stay(tp, 0, 5, 0));
        assert!(!block_mushroom_can_stay(tp, 5, 6, 0));

        // 16. Missing table hooks are safe no-ops (all-None table).
        let bare = BlockTickWorld {
            next_int: None,
            next_float01: None,
            next_u64: None,
            get_block_id: None,
            get_block_id_nc: None,
            get_block_meta: None,
            set_block: None,
            set_block_meta: None,
            set_block_notify: None,
            set_block_update: None,
            set_block_meta_notify: None,
            set_block_and_meta: None,
            get_block_light: None,
            can_see_sky: None,
            attach_world: None,
            attach_torch: None,
            is_solid: None,
            is_solid_nc: None,
            is_water_or_lava: None,
            is_water: None,
            block_registered: None,
            collidable_box: None,
            schedule_update: None,
            mark_update: None,
            notify_neighbors: None,
            spawn_drop: None,
            spawn_falling: None,
            drop_occupant: None,
        };
        block_sand_tick(&bare, 12, 0, 5, 0);
        assert!(!block_flower_can_stay(&bare, 0, 5, 0));
        let act = block_sapling_tick(&bare, 6, 6, 1, 0, 0, 5, 0);
        assert_eq!(act.kind, SaplingAction::None as u8);
    }
}
