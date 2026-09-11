//! Entity physics kernel ported from C++ `Entity::moveEntity`,
//! `Entity::updateFallState`, and `Entity::applyEntityCollision`
//! (which mirror Java `Entity`).
//!
//! The world-dependent half stays in C++: gathering colliding boxes
//! (`World::getCollidingBoundingBoxes`), the `onFall` damage event
//! (virtual), and velocity application. Rust owns the pure resolution
//! math, so collision order (Y, then X, then Z, with box offsets between
//! passes) has a single tested source of truth.
//!
//! AABB ops reuse the `aabb` module (itself a 1:1 port of `AxisAlignedBB`).

use crate::aabb::AxisAlignedBB;
use crate::math_helper::{abs_max, sqrt_double};

/// Plain box for FFI (mirrors `AxisAlignedBB` field order).
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct FfiAabb {
    pub min_x: f64,
    pub min_y: f64,
    pub min_z: f64,
    pub max_x: f64,
    pub max_y: f64,
    pub max_z: f64,
}

impl From<AxisAlignedBB> for FfiAabb {
    fn from(b: AxisAlignedBB) -> Self {
        Self {
            min_x: b.min_x,
            min_y: b.min_y,
            min_z: b.min_z,
            max_x: b.max_x,
            max_y: b.max_y,
            max_z: b.max_z,
        }
    }
}

impl From<FfiAabb> for AxisAlignedBB {
    fn from(b: FfiAabb) -> Self {
        Self::get_bounding_box(b.min_x, b.min_y, b.min_z, b.max_x, b.max_y, b.max_z)
    }
}

/// Resolved movement: the box after collision plus the accepted delta.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct ResolvedMove {
    pub box_: FfiAabb,
    pub dx: f64,
    pub dy: f64,
    pub dz: f64,
}

/// Y-then-X-then-Z collision resolution over a pre-gathered box list.
/// Mirrors the `resolveMovement` lambda in `Entity::moveEntity` exactly:
/// each pass clamps its axis against every box, then offsets the working
/// box before the next pass. Returns false (leaving `out` untouched) on
/// null pointers.
#[no_mangle]
pub unsafe extern "C" fn alpha_entity_resolve_move(
    box_: *const FfiAabb,
    dx: f64,
    dy: f64,
    dz: f64,
    boxes: *const FfiAabb,
    num_boxes: usize,
    out: *mut ResolvedMove,
) -> bool {
    if box_.is_null() || out.is_null() {
        return false;
    }
    if num_boxes > 0 && boxes.is_null() {
        return false;
    }
    let list = unsafe { std::slice::from_raw_parts(boxes, num_boxes) };
    let mut work: AxisAlignedBB = unsafe { *box_ }.into();
    let (mut mx, mut my, mut mz) = (dx, dy, dz);
    for cb in list {
        let cb: AxisAlignedBB = (*cb).into();
        my = cb.calculate_y_offset(&work, my);
    }
    work.offset(0.0, my, 0.0);
    for cb in list {
        let cb: AxisAlignedBB = (*cb).into();
        mx = cb.calculate_x_offset(&work, mx);
    }
    work.offset(mx, 0.0, 0.0);
    for cb in list {
        let cb: AxisAlignedBB = (*cb).into();
        mz = cb.calculate_z_offset(&work, mz);
    }
    work.offset(0.0, 0.0, mz);
    unsafe {
        *out = ResolvedMove { box_: work.into(), dx: mx, dy: my, dz: mz };
    }
    true
}

/// Fall-state step (mirrors `Entity::updateFallState`). Returns the new
/// `fallDistance`; when landing with accumulated distance it also writes
/// that distance to `out_fall_event` (C++ fires `onFall` for it, `-1.0`
/// means no event).
#[no_mangle]
pub unsafe extern "C" fn alpha_entity_fall_step(
    on_ground: bool,
    dy: f64,
    fall_distance: f32,
    out_fall_event: *mut f32,
) -> f32 {
    if out_fall_event.is_null() {
        return fall_distance;
    }
    unsafe {
        *out_fall_event = -1.0;
    }
    if on_ground {
        if fall_distance > 0.0 {
            unsafe {
                *out_fall_event = fall_distance;
            }
            0.0
        } else {
            fall_distance
        }
    } else if dy < 0.0 {
        // C++ promotes to double before subtracting, then narrows back.
        (fall_distance as f64 - dy) as f32
    } else {
        fall_distance
    }
}

/// Push impulse between two entities (mirrors `Entity::applyEntityCollision`
/// after the identity/pushable guards, which stay in C++). Writes both
/// velocity deltas; C++ applies them via `addVelocity`. Returns false when
/// the pair is too close to push.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct PushOut {
    pub dvx1: f64,
    pub dvz1: f64,
    pub dvx2: f64,
    pub dvz2: f64,
}

#[no_mangle]
pub unsafe extern "C" fn alpha_entity_push(
    x1: f64,
    z1: f64,
    x2: f64,
    z2: f64,
    pushable1: bool,
    pushable2: bool,
    out: *mut PushOut,
) -> bool {
    if out.is_null() || !pushable1 || !pushable2 {
        return false;
    }
    let dx = x2 - x1;
    let dz = z2 - z1;
    let max_abs = abs_max(dx, dz);
    if max_abs < 0.01 {
        return false;
    }
    let norm = sqrt_double(max_abs) as f64;
    let (nx, nz) = (dx / norm, dz / norm);
    let mut scale = 1.0 / max_abs;
    if scale > 1.0 {
        scale = 1.0;
    }
    let (ix, iz) = (nx * scale * 0.05, nz * scale * 0.05);
    unsafe {
        *out = PushOut { dvx1: -ix, dvz1: -iz, dvx2: ix, dvz2: iz };
    }
    true
}

#[cfg(test)]
mod tests {
    use super::*;

    fn box_ffi(x0: f64, y0: f64, z0: f64, x1: f64, y1: f64, z1: f64) -> FfiAabb {
        FfiAabb { min_x: x0, min_y: y0, min_z: z0, max_x: x1, max_y: y1, max_z: z1 }
    }

    fn resolve(box_: FfiAabb, dx: f64, dy: f64, dz: f64, boxes: &[FfiAabb]) -> ResolvedMove {
        let mut out = ResolvedMove {
            box_: box_ffi(0.0, 0.0, 0.0, 0.0, 0.0, 0.0),
            dx: 0.0,
            dy: 0.0,
            dz: 0.0,
        };
        assert!(unsafe { alpha_entity_resolve_move(&box_, dx, dy, dz, boxes.as_ptr(), boxes.len(), &mut out) });
        out
    }

    #[test]
    fn test_fall_onto_floor_stops() {
        // Entity box minY=2 falling 5 onto a floor slab occupying y 0..1.
        let entity = box_ffi(-0.3, 2.0, -0.3, 0.3, 3.8, 0.3);
        let floor = box_ffi(-8.0, 0.0, -8.0, 8.0, 1.0, 8.0);
        let r = resolve(entity, 0.0, -5.0, 0.0, &[floor]);
        assert!((r.dy - -1.0).abs() < 1e-9);
        assert!((r.box_.min_y - 1.0).abs() < 1e-9);
        assert_eq!(r.dx, 0.0);
        assert_eq!(r.dz, 0.0);
    }

    #[test]
    fn test_wall_blocks_x_only() {
        // Moving +X into a wall: X clamped, Y/Z pass through.
        let entity = box_ffi(0.0, 1.0, 0.0, 0.6, 2.8, 0.6);
        let wall = box_ffi(1.0, 0.0, -8.0, 2.0, 4.0, 8.0);
        let r = resolve(entity, 2.0, -0.5, 0.3, &[wall]);
        assert!((r.dx - 0.4).abs() < 1e-9);
        assert_eq!(r.dy, -0.5);
        assert_eq!(r.dz, 0.3);
        assert!((r.box_.max_x - 1.0).abs() < 1e-9);
    }

    #[test]
    fn test_empty_boxes_full_move() {
        let entity = box_ffi(0.0, 1.0, 0.0, 0.6, 2.8, 0.6);
        let r = resolve(entity, 1.0, -2.0, 3.0, &[]);
        assert_eq!((r.dx, r.dy, r.dz), (1.0, -2.0, 3.0));
        assert!((r.box_.min_x - 1.0).abs() < 1e-9);
    }

    #[test]
    fn test_null_is_safe() {
        let mut out = ResolvedMove {
            box_: box_ffi(0.0, 0.0, 0.0, 0.0, 0.0, 0.0),
            dx: 0.0,
            dy: 0.0,
            dz: 0.0,
        };
        assert!(!unsafe {
            alpha_entity_resolve_move(
                std::ptr::null(),
                0.0,
                0.0,
                0.0,
                std::ptr::null(),
                0,
                &mut out,
            )
        });
        let b = box_ffi(0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
        assert!(!unsafe { alpha_entity_resolve_move(&b, 0.0, 0.0, 0.0, std::ptr::null(), 1, &mut out) });
    }

    #[test]
    fn test_fall_step_landing_fires_event() {
        let mut ev = -1.0f32;
        let next = unsafe { alpha_entity_fall_step(true, -3.0, 3.5, &mut ev) };
        assert_eq!(next, 0.0);
        assert_eq!(ev, 3.5);
    }

    #[test]
    fn test_fall_step_accumulates_in_air() {
        let mut ev = -1.0f32;
        let next = unsafe { alpha_entity_fall_step(false, -2.0, 1.5, &mut ev) };
        assert_eq!(ev, -1.0);
        assert!((next - 3.5).abs() < 1e-6);
    }

    #[test]
    fn test_fall_step_rising_keeps_distance() {
        let mut ev = -1.0f32;
        let next = unsafe { alpha_entity_fall_step(false, 1.0, 2.0, &mut ev) };
        assert_eq!((next, ev), (2.0, -1.0));
    }

    #[test]
    fn test_push_impulse() {
        // e1 at origin, e2 at (3,4): matches the C++ formula by hand
        // (norm 2, scale 1/4, factor 0.05).
        let mut out = PushOut { dvx1: 0.0, dvz1: 0.0, dvx2: 0.0, dvz2: 0.0 };
        assert!(unsafe { alpha_entity_push(0.0, 0.0, 3.0, 4.0, true, true, &mut out) });
        assert!((out.dvx2 - 0.01875).abs() < 1e-9);
        assert!((out.dvz2 - 0.025).abs() < 1e-9);
        assert!((out.dvx1 + 0.01875).abs() < 1e-9);
        assert!((out.dvz1 + 0.025).abs() < 1e-9);
    }

    #[test]
    fn test_push_too_close_or_locked() {
        let mut out = PushOut { dvx1: 0.0, dvz1: 0.0, dvx2: 0.0, dvz2: 0.0 };
        assert!(!unsafe { alpha_entity_push(0.0, 0.0, 0.005, 0.0, true, true, &mut out) });
        assert!(!unsafe { alpha_entity_push(0.0, 0.0, 3.0, 4.0, false, true, &mut out) });
        assert!(!unsafe { alpha_entity_push(0.0, 0.0, 3.0, 4.0, true, true, std::ptr::null_mut()) });
    }
}
