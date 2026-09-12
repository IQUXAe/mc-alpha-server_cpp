//! Thread-local `USE_CTX` bridge from the item-use drivers back into
//! `PlaySession`/`World`. Split out of `session.rs`; behavior unchanged.
//!
//! SAFETY: same pattern as `world/shims` — the cell holds raw pointers set
//! from a live `&mut SessionCtx` by `UseGuard` and cleared on drop; the
//! `use_*` helpers deref them only while set and return a default when
//! null. Never held across awaits, never shared between threads.

use std::collections::HashSet;
use crate::entity_table::{Entity, EntityId};
use crate::item_verbs::ItemUseWorld;
use crate::session::play::PlaySession;
use crate::session_packets::tile_packet;
use crate::world::World;

// ---- use/chat bridge (session-scoped driver shims) ----

#[derive(Clone, Copy)]
struct UseCtx {
    world: *mut World,
    player: EntityId,
    session: *mut PlaySession,
    ops: *const HashSet<String>,
}

thread_local! {
    static USE_CTX: std::cell::Cell<UseCtx> = std::cell::Cell::new(UseCtx {
        world: std::ptr::null_mut(),
        player: -1,
        session: std::ptr::null_mut(),
        ops: std::ptr::null(),
    });
}

fn with_use_ctx<T>(f: impl FnOnce(&mut World, EntityId, &mut PlaySession, &HashSet<String>) -> T, dflt: T) -> T {
    // SAFETY: `UseGuard` reborrows `&mut World` / `&mut PlaySession` through
    // raw pointers for the guard's lifetime. Sound because the original
    // `&mut SessionCtx` (which owns the `&mut World`) is never touched
    // while the guard lives: the guarded region only uses `self`
    // (PlaySession), the local `s` stack, and these shims. No two live
    // `&mut` to the same allocation overlap in use.
    USE_CTX.with(|c| unsafe {
        let ctx = c.get();
        if ctx.world.is_null() || ctx.session.is_null() || ctx.ops.is_null() {
            return dflt;
        }
        f(&mut *ctx.world, ctx.player, &mut *ctx.session, &*ctx.ops)
    })
}

pub(crate) struct UseGuard;
impl UseGuard {
    pub(crate) fn enter(world: *mut World, player: EntityId, session: *mut PlaySession, ops: *const HashSet<String>) -> Self {
        USE_CTX.with(|c| {
            c.set(UseCtx { world, player, session, ops });
        });
        UseGuard
    }
}
impl Drop for UseGuard {
    fn drop(&mut self) {
        USE_CTX.with(|c| {
            c.set(UseCtx {
                world: std::ptr::null_mut(),
                player: -1,
                session: std::ptr::null_mut(),
                ops: std::ptr::null(),
            });
        });
    }
}

fn use_next_int(bound: i32) -> i32 {
    if bound <= 0 {
        return 0;
    }
    with_use_ctx(|w, _, _, _| w.rng_next_int(bound), 0)
}

fn use_next_f64() -> f64 {
    with_use_ctx(|w, _, _, _| w.rng_next_f64(), 0.0)
}

fn use_get_id(x: i32, y: i32, z: i32) -> u8 {
    with_use_ctx(|w, _, _, _| w.get_block_id(x, y, z), 0)
}

fn use_set_notify(x: i32, y: i32, z: i32, id: u8) -> bool {
    with_use_ctx(|w, _, _, _| w.apply_set_notify(x, y, z, id), false)
}

fn use_set_meta_notify(x: i32, y: i32, z: i32, id: u8, meta: u8) -> bool {
    with_use_ctx(|w, _, _, _| w.apply_set_meta_notify(x, y, z, id, meta), false)
}

fn use_set_quiet(x: i32, y: i32, z: i32, id: u8) -> bool {
    // setBlockWithNotifyNoClientUpdate == notify path natively (mark is a no-op).
    with_use_ctx(|w, _, _, _| w.apply_set_notify(x, y, z, id), false)
}

fn use_set_meta(x: i32, y: i32, z: i32, meta: u8) {
    with_use_ctx(|w, _, _, _| w.set_block_meta(x, y, z, meta), false);
}

fn use_does_attach(x: i32, y: i32, z: i32) -> bool {
    with_use_ctx(|w, _, _, _| w.block_allows_attachment(x, y, z), false)
}

fn use_mat_burning(x: i32, y: i32, z: i32) -> bool {
    with_use_ctx(
        |w, _, _, _| {
            crate::world::material_of(crate::block::alpha_block_properties_get(
                w.get_block_id(x, y, z) as u32,
            ).material)
            .get_burning()
        },
        false,
    )
}

fn use_mat_solid(x: i32, y: i32, z: i32) -> bool {
    with_use_ctx(|w, _, _, _| w.material_at(x, y, z).is_solid(), false)
}

fn use_collidable(x: i32, y: i32, z: i32) -> bool {
    with_use_ctx(
        |w, _, _, _| {
            let bid = w.get_block_id(x, y, z);
            let props = crate::block::alpha_block_properties_get(bid as u32);
            bid != 0
                && props.block_type != crate::block::BlockType::Fluid as u8
                && crate::world::has_collision_box(props.block_type)
        },
        false,
    )
}

fn use_can_stay(id: u8, x: i32, y: i32, z: i32) -> bool {
    // canBlockStay drivers under the tick table (base rule is true).
    with_use_ctx(
        |w, _, _, _| {
            crate::world::shims::with_tick_bridge(w as *mut World, || {
                let t = crate::world::shims::tick_table_ref();
                match id {
                    37 | 38 => crate::block_ticks::block_flower_can_stay(t, x, y, z),
                    39 | 40 => crate::block_ticks::block_mushroom_can_stay(t, x, y, z),
                    50 => crate::block_ticks::block_torch_can_stay(t, x, y, z),
                    81 => crate::block_ticks::block_cactus_can_stay(t, x, y, z),
                    83 => crate::block_ticks::block_reed_can_stay(t, x, y, z),
                    6 => crate::block_ticks::block_sapling_can_stay(t, x, y, z),
                    59 => crate::block_ticks::block_crops_can_stay(t, id, x, y, z),
                    _ => true,
                }
            })
        },
        false,
    )
}

fn use_placement_clear(id: u8, x: i32, y: i32, z: i32) -> bool {
    // mirrors isPlacementVolumeClear: no live boat/living intersecting.
    with_use_ctx(
        |w, _, _, _| {
            let props = crate::block::alpha_block_properties_get(id as u32);
            if !crate::world::has_collision_box(props.block_type) {
                return true;
            }
            let mask = crate::aabb::AxisAlignedBB::get_bounding_box(
                x as f64 + props.min_x as f64,
                y as f64 + props.min_y as f64,
                z as f64 + props.min_z as f64,
                x as f64 + props.max_x as f64,
                y as f64 + props.max_y as f64,
                z as f64 + props.max_z as f64,
            );
            for oid in w.entities.alive_ids() {
                let blocks = match w.entities.get(oid) {
                    Some(Entity::Boat(_)) | Some(Entity::Mob(_)) | Some(Entity::Animal(_))
                    | Some(Entity::Player(_)) => true,
                    _ => false,
                };
                if blocks {
                    if let Some(e) = w.entities.get(oid) {
                        if mask.intersects_with(&e.body().bounding_box) {
                            return false;
                        }
                    }
                }
            }
            true
        },
        false,
    )
}

fn use_block_placed(id: u8, x: i32, y: i32, z: i32, side: i32) {
    // Torch facing like onBlockPlaced (tiles already exist via added).
    if id != 50 {
        return;
    }
    with_use_ctx(
        |w, _, _, _| {
            crate::world::shims::with_tick_bridge(w as *mut World, || {
                let meta = crate::block_ticks::block_torch_attach_meta(
                    crate::world::shims::tick_table_ref(),
                    side,
                    x,
                    y,
                    z,
                );
                w.set_block_id(x, y, z, id);
                w.set_block_meta(x, y, z, meta);
            })
        },
        (),
    );
}

fn use_have_block(id: u8) -> bool {
    with_use_ctx(|_, _, _, _| crate::world::World::native_registered(id), false)
}

fn use_spawn_item(
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
    with_use_ctx(
        |w, _, _, _| {
            let eid = w.spawn_item_entity(item_id, count, damage, fx, fy, fz);
            if let Some(Entity::Item(e)) = w.entities.get_mut(eid) {
                e.body.motion = [mx, my, mz];
            }
        },
        (),
    );
}

fn use_send_te(x: i32, y: i32, z: i32) {
    with_use_ctx(
        |w, _, sess, _| {
            if let Some(tile) = w.tiles.get(&(x, y, z)) {
                sess.outbox.push(tile_packet(x, y, z, tile));
            }
        },
        (),
    );
}

fn use_ray_trace(
    sx: f64,
    sy: f64,
    sz: f64,
    ex: f64,
    ey: f64,
    ez: f64,
    out_x: &mut i32,
    out_y: &mut i32,
    out_z: &mut i32,
) -> bool {
    with_use_ctx(
        |w, _, _, _| match w.ray_trace_hit_liquids([sx, sy, sz], [ex, ey, ez]) {
            Some([x, y, z]) => {
                *out_x = x;
                *out_y = y;
                *out_z = z;
                true
            }
            None => false,
        },
        false,
    )
}

pub(crate) static USE_TABLE: ItemUseWorld = ItemUseWorld {
    next_int: Some(use_next_int),
    next_f64_01: Some(use_next_f64),
    get_block_id: Some(use_get_id),
    set_block_notify: Some(use_set_notify),
    set_block_meta_notify: Some(use_set_meta_notify),
    set_block_quiet: Some(use_set_quiet),
    set_block_meta: Some(use_set_meta),
    does_attach: Some(use_does_attach),
    material_burning: Some(use_mat_burning),
    material_solid: Some(use_mat_solid),
    collidable_box: Some(use_collidable),
    block_can_stay: Some(use_can_stay),
    placement_clear: Some(use_placement_clear),
    block_placed: Some(use_block_placed),
    have_block: Some(use_have_block),
    spawn_item: Some(use_spawn_item),
    send_te_packet: Some(use_send_te),
    ray_trace: Some(use_ray_trace),
};
