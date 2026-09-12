//! Tree-generation bridge: `tree_*` adapters feeding the `decorators`
//! `WorldAccessor` from the live [`World`].
//! Split out of `world.rs`; behavior unchanged.
//!
//! SAFETY: same pattern as before — the cell holds a raw `*mut World` set
//! from a live `&mut World` (`TickGuard` in chunk populate and sapling
//! growth) and cleared on drop; the `tree_*` helpers deref it only while
//! set and return a default when null. Single-threaded tick loop: never
//! held across awaits, never shared between threads.
//!
//! This is the last thread-local in the world module tree; threading
//! `&mut World` through the decorator plumbing (the `WorldAccessor`
//! table) is tracked work, not done here.

use crate::block::alpha_block_properties_get;
use crate::world::{World, is_air_material};

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
    // SAFETY: `TickGuard` points at a live `&mut World` and clears
    // afterwards; null (no driver) returns the default.
    TICK_WORLD.with(|t| unsafe {
        let p = t.get();
        if p.is_null() {
            dflt
        } else {
            f(&mut *p)
        }
    })
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
