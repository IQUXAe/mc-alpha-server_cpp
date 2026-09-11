//! Pure entity-tracking math ported from C++ `EntityTracker`.
//!
//! `EntityTracker.cpp` duplicated the same fixed-point / angle /
//! delta-packet formulas in 4 places (constructor, `makeSpawnPacket`,
//! `sendSpawnTo`, `sendUpdates`). This module is the single source of
//! truth. All functions are pure (no pointers, no allocation, no
//! `unwrap()`) so they can be unit-tested in Rust and called from C++
//! one at a time while ownership (`entries_`, `trackingPlayers`) stays
//! in C++ for now.
//!
//! Long-term FFI-reduction direction: once all call sites use these,
//! the whole per-tick `sendUpdates` loop can move behind a single
//! batch FFI call (snapshots in, packet list out) instead of dozens
//! of fine-grained calls.

/// Network fixed-point position: `(int)(pos * 32.0)`.
///
/// Matches the C++ C-style cast (truncation toward zero) via `as`,
/// which also truncates toward zero for float->int.
#[no_mangle]
pub extern "C" fn alpha_tracker_encode_pos(pos: f64) -> i32 {
    (pos * 32.0) as i32
}

/// Network angle byte: `(int8_t)((int)floor(deg * 256 / 360) & 0xFF)`.
///
/// The masking makes negative angles wrap exactly like the C++ code
/// (e.g. -1 degree -> 0xFF -> -1 as i8).
#[no_mangle]
pub extern "C" fn alpha_tracker_encode_rot(degrees: f32) -> i8 {
    let v = (degrees * 256.0 / 360.0).floor() as i32 & 0xFF;
    v as u8 as i8
}

/// Which movement packet to broadcast (mirrors `sendUpdates:199-216`).
///
/// Returns: 0 = keep-alive `entity`, 1 = `relEntityMove`,
/// 2 = `entityLook`, 3 = `relEntityMoveLook`, 4 = `entityTeleport`
/// (delta outside `[-128, 128)` no longer fits in an i8).
#[no_mangle]
pub extern "C" fn alpha_tracker_move_kind(
    dx: i32,
    dy: i32,
    dz: i32,
    moved: bool,
    turned: bool,
) -> u8 {
    if dx >= -128 && dx < 128 && dy >= -128 && dy < 128 && dz >= -128 && dz < 128 {
        if moved && turned {
            3
        } else if moved {
            1
        } else if turned {
            2
        } else {
            0
        }
    } else {
        4
    }
}

/// Velocity-dirty check (mirrors `sendUpdates:183-196`).
///
/// Java sends `Packet28` when squared velocity delta exceeds
/// `0.02^2`, plus an explicit stop packet when motion hits exactly
/// zero after being non-zero.
#[no_mangle]
pub extern "C" fn alpha_tracker_velocity_changed(
    motion_x: f64,
    motion_y: f64,
    motion_z: f64,
    last_x: f64,
    last_y: f64,
    last_z: f64,
    send_velocity: bool,
) -> bool {
    if !send_velocity {
        return false;
    }
    let dvx = motion_x - last_x;
    let dvy = motion_y - last_y;
    let dvz = motion_z - last_z;
    let dv_sq = dvx * dvx + dvy * dvy + dvz * dvz;
    if dv_sq > 0.02 * 0.02 {
        return true;
    }
    motion_x == 0.0 && motion_y == 0.0 && motion_z == 0.0
        && (last_x != 0.0 || last_y != 0.0 || last_z != 0.0)
}

/// Range check (mirrors `updateTracking:148-151`).
///
/// Note: vanilla only compares X/Z against `trackingRange`
/// (Y is ignored), and compares the *observer* position against the
/// *last sent* fixed-point position, not the live one. Preserved here.
#[no_mangle]
pub extern "C" fn alpha_tracker_in_range(
    player_x: f64,
    player_z: f64,
    last_fixed_x: i32,
    last_fixed_z: i32,
    tracking_range: i32,
) -> bool {
    let dx = player_x - last_fixed_x as f64 / 32.0;
    let dz = player_z - last_fixed_z as f64 / 32.0;
    let r = tracking_range as f64;
    dx >= -r && dx <= r && dz >= -r && dz <= r
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_encode_pos_truncates_like_cpp_cast() {
        assert_eq!(alpha_tracker_encode_pos(10.5), 336);
        assert_eq!(alpha_tracker_encode_pos(0.0), 0);
        // C-style cast truncates toward zero, it does not floor:
        assert_eq!(alpha_tracker_encode_pos(-10.5), -336);
    }

    #[test]
    fn test_encode_rot_wraps_like_cpp() {
        assert_eq!(alpha_tracker_encode_rot(0.0), 0);
        // 90 deg -> 64
        assert_eq!(alpha_tracker_encode_rot(90.0), 64);
        // 360 deg -> 256 & 0xFF = 0
        assert_eq!(alpha_tracker_encode_rot(360.0), 0);
        // -1 deg -> floor(-0.71) = -1 -> & 0xFF = 255 -> -1 as i8
        assert_eq!(alpha_tracker_encode_rot(-1.0), -1);
    }

    #[test]
    fn test_move_kind_selection() {
        assert_eq!(alpha_tracker_move_kind(5, 0, 0, true, true), 3);
        assert_eq!(alpha_tracker_move_kind(5, 0, 0, true, false), 1);
        assert_eq!(alpha_tracker_move_kind(0, 0, 0, false, true), 2);
        assert_eq!(alpha_tracker_move_kind(0, 0, 0, false, false), 0);
        // Boundary: 128 no longer fits in i8 -> teleport.
        assert_eq!(alpha_tracker_move_kind(128, 0, 0, true, false), 4);
        assert_eq!(alpha_tracker_move_kind(-129, 0, 0, true, false), 4);
        assert_eq!(alpha_tracker_move_kind(127, 127, 127, true, false), 1);
    }

    #[test]
    fn test_velocity_changed() {
        assert!(!alpha_tracker_velocity_changed(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, false));
        assert!(!alpha_tracker_velocity_changed(1.0, 0.0, 0.0, 1.0, 0.0, 0.0, true));
        assert!(alpha_tracker_velocity_changed(1.0, 0.0, 0.0, 0.0, 0.0, 0.0, true));
        // Small jitter below threshold: no packet.
        assert!(!alpha_tracker_velocity_changed(0.01, 0.0, 0.0, 0.0, 0.0, 0.0, true));
        // Stop packet: motion hit zero after being non-zero.
        assert!(alpha_tracker_velocity_changed(0.0, 0.0, 0.0, 0.5, 0.0, 0.0, true));
    }

    #[test]
    fn test_in_range_xz_only() {
        // last sent (0,0), range 64: inside.
        assert!(alpha_tracker_in_range(10.0, 10.0, 0, 0, 64));
        // Outside X.
        assert!(!alpha_tracker_in_range(100.0, 0.0, 0, 0, 64));
        // Boundary inclusive.
        assert!(alpha_tracker_in_range(64.0, 0.0, 0, 0, 64));
        assert!(!alpha_tracker_in_range(64.1, 0.0, 0, 0, 64));
    }
}
