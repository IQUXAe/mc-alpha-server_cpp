//! Digging state machine ported from C++ `ItemInWorldManager`.
//!
//! This module owns the progressive-digging state (`cur_damage`,
//! `initial_cooldown`, current target block) that used to live as six
//! separate fields on the C++ class (`ItemInWorldManager.h:12-20`).
//! The C++ side keeps only world access (`getBlockId`, `harvestBlock`)
//! and delegates the state transitions here, one FFI call per packet.
//!
//! Design notes for FFI reduction:
//! - Hardness is computed inside Rust via `alpha_mining_check_hardness`,
//!   so C++ no longer needs a separate `Block::blocksList` lookup +
//!   `checkHardness` round-trip per tick. One call carries everything.
//! - No allocation, no strings, no `unwrap()`: with `panic = "abort"`
//!   any panic would kill the whole server, so all paths are total.
//! - State is a plain `#[repr(C)]` value stored inline in C++
//!   (no opaque pointer, no extra free function needed).

use crate::player_mining::alpha_mining_check_hardness;

/// Digging state, stored inline on the C++ side.
///
/// Mirrors `ItemInWorldManager::{curblockDamage, blockDamage,
/// initialDamage, partiallyDestroyedBlockX/Y/Z}` plus a `has_target`
/// flag (C++ used `(0,0,0)` as implicit "no target", which collides
/// with a real block at the origin).
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct FfiDigState {
    pub cur_damage: f32,
    pub block_damage: f32,
    pub initial_cooldown: i32,
    pub target_x: i32,
    pub target_y: i32,
    pub target_z: i32,
    pub has_target: bool,
}

/// Everything the digging simulation needs for one decision.
/// `block_id == 0` means air (nothing to dig).
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct FfiDigInput {
    pub block_id: i32,
    pub held_item_id: i32,
    pub in_water: bool,
    pub on_ground: bool,
}

/// Fresh digging state (no target, no damage).
#[no_mangle]
pub extern "C" fn alpha_dig_state_new() -> FfiDigState {
    FfiDigState {
        cur_damage: 0.0,
        block_damage: 0.0,
        initial_cooldown: 0,
        target_x: 0,
        target_y: 0,
        target_z: 0,
        has_target: false,
    }
}

/// Reset digging progress (mirrors `ItemInWorldManager::cancelRemoving`).
///
/// # Safety
/// `state` must be non-null, aligned for `FfiDigState`, and writable
/// for the duration of the call.
#[no_mangle]
pub unsafe extern "C" fn alpha_dig_cancel(state: *mut FfiDigState) {
    if state.is_null() {
        return;
    }
    let s = unsafe { &mut *state };
    s.cur_damage = 0.0;
    s.initial_cooldown = 0;
}

/// Start-of-dig predicate (mirrors `ItemInWorldManager::onBlockClicked`).
///
/// Returns `true` when the block breaks instantly (`hardness >= 1.0`,
/// e.g. torches/flowers) and the caller must harvest immediately.
/// Does not mutate `state`: progressive digging is driven by
/// `alpha_dig_on_tick`.
#[no_mangle]
pub extern "C" fn alpha_dig_on_click(input: FfiDigInput) -> bool {
    if input.block_id <= 0 {
        return false;
    }
    let hardness = alpha_mining_check_hardness(
        input.block_id,
        input.held_item_id,
        input.in_water,
        input.on_ground,
    );
    hardness >= 1.0
}

/// Progressive-digging tick (mirrors `ItemInWorldManager::blockRemoving`).
///
/// Returns `true` when accumulated damage reaches `1.0` and the caller
/// must call `harvestBlock(x, y, z)` now (which also resets the
/// accumulator and installs the 5-tick `initial_cooldown`).
///
/// Exact C++ semantics preserved:
/// - `initial_cooldown > 0` ticks down first, no progress.
/// - A different target resets accumulators and latches the new target.
/// - Air (`block_id == 0`) on the current target is a no-op (matches
///   the early `return` in C++; progress is kept, not reset).
///
/// # Safety
/// `state` must be non-null, aligned for `FfiDigState`, and writable
/// for the duration of the call.
#[no_mangle]
pub unsafe extern "C" fn alpha_dig_on_tick(
    state: *mut FfiDigState,
    x: i32,
    y: i32,
    z: i32,
    input: FfiDigInput,
) -> bool {
    if state.is_null() {
        return false;
    }
    let s = unsafe { &mut *state };

    if s.initial_cooldown > 0 {
        s.initial_cooldown -= 1;
        return false;
    }

    let same_target = s.has_target && s.target_x == x && s.target_y == y && s.target_z == z;
    if !same_target {
        s.cur_damage = 0.0;
        s.block_damage = 0.0;
        s.target_x = x;
        s.target_y = y;
        s.target_z = z;
        s.has_target = true;
        return false;
    }

    if input.block_id <= 0 {
        return false;
    }

    let hardness_tick = alpha_mining_check_hardness(
        input.block_id,
        input.held_item_id,
        input.in_water,
        input.on_ground,
    );
    // Unbreakable (bedrock, hardness < 0 -> 0.0 tick): never accumulates.
    if hardness_tick <= 0.0 {
        return false;
    }
    s.cur_damage += hardness_tick;
    s.block_damage += 1.0;
    if s.cur_damage >= 1.0 {
        s.cur_damage = 0.0;
        s.block_damage = 0.0;
        s.initial_cooldown = 5;
        return true;
    }
    false
}

#[cfg(test)]
mod tests {
    use super::*;

    fn input(block_id: i32, held: i32) -> FfiDigInput {
        FfiDigInput {
            block_id,
            held_item_id: held,
            in_water: false,
            on_ground: true,
        }
    }

    #[test]
    fn test_click_instant_break_torch() {
        // Torch (50): hardness 0.0 -> check returns 1.0 -> instant.
        assert!(alpha_dig_on_click(input(50, 0)));
    }

    #[test]
    fn test_click_stone_not_instant() {
        assert!(!alpha_dig_on_click(input(1, 278)));
    }

    #[test]
    fn test_click_air_never_breaks() {
        assert!(!alpha_dig_on_click(input(0, 278)));
    }

    #[test]
    fn test_tick_stone_diamond_pick_takes_six_ticks() {
        // Stone (1) + diamond pick (278): 0.1777/tick -> 6 ticks.
        // First tick latches the target, then 6 progress ticks.
        let mut s = alpha_dig_state_new();
        let inp = input(1, 278);
        assert!(!unsafe { alpha_dig_on_tick(&mut s, 10, 64, 10, inp) }); // latch
        for _ in 0..5 {
            assert!(!unsafe { alpha_dig_on_tick(&mut s, 10, 64, 10, inp) });
        }
        assert!(unsafe { alpha_dig_on_tick(&mut s, 10, 64, 10, inp) }); // broke
        assert_eq!(s.initial_cooldown, 5);
        assert_eq!(s.cur_damage, 0.0);
    }

    #[test]
    fn test_tick_cooldown_blocks_progress() {
        let mut s = alpha_dig_state_new();
        let inp = input(50, 0); // instant hardness, but cooldown gates tick path
        s.initial_cooldown = 2;
        s.has_target = true;
        s.target_x = 1;
        s.target_y = 2;
        s.target_z = 3;
        assert!(!unsafe { alpha_dig_on_tick(&mut s, 1, 2, 3, inp) });
        assert_eq!(s.initial_cooldown, 1);
        assert!(!unsafe { alpha_dig_on_tick(&mut s, 1, 2, 3, inp) });
        assert_eq!(s.initial_cooldown, 0);
    }

    #[test]
    fn test_tick_target_switch_resets() {
        let mut s = alpha_dig_state_new();
        let inp = input(1, 278);
        assert!(!unsafe { alpha_dig_on_tick(&mut s, 0, 64, 0, inp) });
        assert!(!unsafe { alpha_dig_on_tick(&mut s, 0, 64, 0, inp) });
        assert!(s.cur_damage > 0.0);
        // Different block: reset, no progress, new target latched.
        assert!(!unsafe { alpha_dig_on_tick(&mut s, 5, 64, 5, inp) });
        assert_eq!(s.cur_damage, 0.0);
        assert_eq!((s.target_x, s.target_y, s.target_z), (5, 64, 5));
    }

    #[test]
    fn test_tick_bedrock_never_breaks() {
        let mut s = alpha_dig_state_new();
        let inp = input(7, 278);
        assert!(!unsafe { alpha_dig_on_tick(&mut s, 0, 1, 0, inp) }); // latch
        for _ in 0..50 {
            assert!(!unsafe { alpha_dig_on_tick(&mut s, 0, 1, 0, inp) });
        }
    }

    #[test]
    fn test_cancel_resets() {
        let mut s = alpha_dig_state_new();
        s.cur_damage = 0.5;
        s.initial_cooldown = 3;
        unsafe { alpha_dig_cancel(&mut s) };
        assert_eq!(s.cur_damage, 0.0);
        assert_eq!(s.initial_cooldown, 0);
    }

    #[test]
    fn test_null_state_is_safe() {
        let inp = input(1, 278);
        assert!(!unsafe { alpha_dig_on_tick(core::ptr::null_mut(), 0, 0, 0, inp) });
        unsafe { alpha_dig_cancel(core::ptr::null_mut()) }; // must not crash
    }
}
