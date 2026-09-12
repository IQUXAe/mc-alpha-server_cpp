//! Thread-local bridges from the stateless `block_*` drivers back into [`World`].
//! Split out of `world.rs`; behavior unchanged.
//!
//! SAFETY: each bridge cell holds a raw `*mut World` set from a live
//! `&mut World` by the driver (`TickGuard`, `with_tick_bridge`,
//! `SPAWN_WORLD` setters) and cleared/restored right after. The `*_world`
//! helpers deref it only while set and return a default when null (no
//! driver on the stack). Single-threaded tick loop: never held across
//! awaits, never shared between threads. Callers must not re-enter the
//! same bridge with a *different* world (nesting the same world is fine
//! via `with_tick_bridge`'s restore).
//!
//! This file is the whole `unsafe` surface of the world module tree;
//! shrinking it (e.g. by threading `&mut World` through the drivers) is
//! tracked work, not done here.

use crate::block::{BlockType, alpha_block_properties_get};
use crate::block_fire::FireWorld;
use crate::block_ticks::BlockTickWorld;
use crate::entity_table::{Body, Entity};
use crate::material::Material;
use crate::world::{World, has_collision_box, has_collision_id, is_air_material};

/// Fire block id placed by explosions and lava contact.
const FIRE_BLOCK_ID: u8 = 51;

/// Nest-safe tick-bridge scope (also used by `session` item verbs): runs `f` with the bridge pointing at
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
pub(crate) struct TickGuard;
impl TickGuard {
    pub(crate) fn enter(world: *mut World) -> Self {
        TICK_WORLD.with(|t| t.set(world));
        TickGuard
    }
}
impl Drop for TickGuard {
    fn drop(&mut self) {
        TICK_WORLD.with(|t| t.set(std::ptr::null_mut()));
    }
}

pub(crate) fn with_tick_world<T>(f: impl FnOnce(&mut World) -> T, dflt: T) -> T {
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

pub(crate) static TICK_TABLE: BlockTickWorld = BlockTickWorld {
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

pub(crate) fn fire_table() -> FireWorld<'static> {
    FireWorld { base: &TICK_TABLE, detonate_tnt: Some(tick_detonate) }
}

pub(crate) fn tree_accessor() -> crate::decorators::WorldAccessor {
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
