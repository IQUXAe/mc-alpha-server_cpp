//! Creature steering math ported from C++ `EntityCreature` (mirrors Java
//! `EntityCreature`/`EntityLiving.func_147_b`).
//!
//! Only the closed-form angle math moves here: turn clamping, facing, and
//! path-point steering. Phase control (`updateAI`), wander scoring (needs
//! the virtual `getBlockPathWeight`), and path objects stay in C++ for now
//! — they need virtual dispatch and the entity table.
//!
//! Angle convention notes (kept 1:1): yaw is degrees, `atan2(dz, dx)` in
//! `f64` narrowed to `f32`, and the strafe helpers use the quantized
//! `math_helper` sin/cos tables exactly like C++ `MathHelper::sin/cos`.

use crate::math_helper::{cos, sin};

/// Turn clamp (mirrors `EntityCreature::clampAngle`).
#[no_mangle]
pub extern "C" fn alpha_ai_clamp_angle(current: f32, target: f32, max_delta: f32) -> f32 {
    let mut delta = target - current;
    while delta < -180.0 {
        delta += 360.0;
    }
    while delta >= 180.0 {
        delta -= 360.0;
    }
    if delta > max_delta {
        delta = max_delta;
    }
    if delta < -max_delta {
        delta = -max_delta;
    }
    current + delta
}

/// Facing computation (mirrors `EntityCreature::faceEntity` after C++
/// resolves `dy`: living eye height above, else bounding-box center).
/// Writes the new yaw/pitch; the pitch result already includes the leading
/// negation from C++.
#[no_mangle]
pub unsafe extern "C" fn alpha_ai_face_angles(
    dx: f64,
    dz: f64,
    dy: f64,
    cur_yaw: f32,
    cur_pitch: f32,
    max_turn: f32,
    out_yaw: *mut f32,
    out_pitch: *mut f32,
) -> bool {
    if out_yaw.is_null() || out_pitch.is_null() {
        return false;
    }
    let dist = crate::math_helper::sqrt_double(dx * dx + dz * dz);
    let yaw = (dz.atan2(dx) * 180.0 / std::f64::consts::PI) as f32 - 90.0;
    let pitch = (dy.atan2(dist as f64) * 180.0 / std::f64::consts::PI) as f32;
    unsafe {
        *out_pitch = -clamp_inner(cur_pitch, pitch, max_turn);
        *out_yaw = clamp_inner(cur_yaw, yaw, max_turn);
    }
    true
}

fn clamp_inner(current: f32, target: f32, max_delta: f32) -> f32 {
    let mut delta = target - current;
    while delta < -180.0 {
        delta += 360.0;
    }
    while delta >= 180.0 {
        delta -= 360.0;
    }
    if delta > max_delta {
        delta = max_delta;
    }
    if delta < -max_delta {
        delta = -max_delta;
    }
    current + delta
}

/// Path-point steering intent (mirrors the steering block in
/// `EntityCreature::followPath`). `forward_in` is the pre-steering forward
/// value (C++ passes its just-zeroed `moveForward_`; the formula is kept
/// general). The caller still applies `moveSpeed` afterwards, exactly like
/// C++.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct SteerOut {
    pub new_yaw: f32,
    pub strafe: f32,
    pub forward: f32,
    pub jump: bool,
}

#[no_mangle]
pub unsafe extern "C" fn alpha_ai_steer_to_point(
    dx: f64,
    dz: f64,
    dy: f64,
    cur_yaw: f32,
    is_attacking: bool,
    has_target: bool,
    tgt_dx: f64,
    tgt_dz: f64,
    forward_in: f32,
    out: *mut SteerOut,
) -> bool {
    if out.is_null() {
        return false;
    }
    let target_yaw = (dz.atan2(dx) * 180.0 / std::f64::consts::PI) as f32 - 90.0;
    let mut yaw_delta = target_yaw - cur_yaw;
    while yaw_delta < -180.0 {
        yaw_delta += 360.0;
    }
    while yaw_delta >= 180.0 {
        yaw_delta -= 360.0;
    }
    if yaw_delta > 30.0 {
        yaw_delta = 30.0;
    }
    if yaw_delta < -30.0 {
        yaw_delta = -30.0;
    }
    let new_yaw = cur_yaw + yaw_delta;

    let (mut strafe, mut forward) = (0.0f32, forward_in);
    if is_attacking && has_target {
        let backup_yaw = new_yaw;
        let face_yaw = (tgt_dz.atan2(tgt_dx) * 180.0 / std::f64::consts::PI) as f32 - 90.0;
        let strafe_angle = (backup_yaw - face_yaw + 90.0) * (std::f32::consts::PI / 180.0);
        strafe = -sin(strafe_angle) * forward_in;
        forward = cos(strafe_angle) * forward_in;
    }

    unsafe {
        *out = SteerOut { new_yaw, strafe, forward, jump: dy > 0.0 };
    }
    true
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_clamp_angle() {
        assert_eq!(alpha_ai_clamp_angle(0.0, 10.0, 30.0), 10.0);
        assert_eq!(alpha_ai_clamp_angle(0.0, 100.0, 30.0), 30.0);
        assert_eq!(alpha_ai_clamp_angle(0.0, -100.0, 30.0), -30.0);
        // Wrap: 170 -> -170 turns +20, clamped to 30.
        assert_eq!(alpha_ai_clamp_angle(170.0, -170.0, 30.0), 190.0);
        // Exact 180 wraps to -180 first.
        assert_eq!(alpha_ai_clamp_angle(0.0, 180.0, 30.0), -30.0);
    }

    #[test]
    fn test_face_angles_east() {
        // Target due east (+x): yaw = atan2(0,1)*180/pi - 90 = -90.
        let (mut yaw, mut pitch) = (0.0f32, 0.0f32);
        assert!(unsafe { alpha_ai_face_angles(5.0, 0.0, 0.0, 0.0, 0.0, 30.0, &mut yaw, &mut pitch) });
        assert_eq!(yaw, -30.0); // clamped turn toward -90
        assert_eq!(pitch, -0.0);
    }

    #[test]
    fn test_face_angles_null_is_safe() {
        let mut yaw = 0.0f32;
        assert!(!unsafe { alpha_ai_face_angles(1.0, 0.0, 0.0, 0.0, 0.0, 30.0, &mut yaw, std::ptr::null_mut()) });
    }

    #[test]
    fn test_steer_straight_ahead() {
        // Point due east of a creature facing east (yaw -90): no turn.
        let mut out = SteerOut { new_yaw: 0.0, strafe: 0.0, forward: 0.0, jump: false };
        assert!(unsafe {
            alpha_ai_steer_to_point(5.0, 0.0, 0.0, -90.0, false, false, 0.0, 0.0, 0.0, &mut out)
        });
        assert!((out.new_yaw + 90.0).abs() < 1e-4);
        assert_eq!(out.strafe, 0.0);
        assert!(!out.jump);
    }

    #[test]
    fn test_steer_turn_rate_limited() {
        // Point due west while facing east: 180 normalised to -180,
        // clamped to a -30 turn.
        let mut out = SteerOut { new_yaw: 0.0, strafe: 0.0, forward: 0.0, jump: false };
        assert!(unsafe {
            alpha_ai_steer_to_point(-5.0, 0.0, 1.0, -90.0, false, false, 0.0, 0.0, 0.0, &mut out)
        });
        assert!((out.new_yaw + 120.0).abs() < 1e-4);
        assert!(out.jump);
    }

    #[test]
    fn test_steer_strafe_uses_input_forward() {
        // Attacking: strafe formula applied to the passed forward value.
        let mut out = SteerOut { new_yaw: 0.0, strafe: 0.0, forward: 0.0, jump: false };
        assert!(unsafe {
            alpha_ai_steer_to_point(5.0, 0.0, 0.0, -90.0, true, true, 5.0, 0.0, 0.7, &mut out)
        });
        // Target straight ahead: strafe angle 90deg -> sin=1, cos~0.
        assert!((out.strafe + 0.7).abs() < 1e-4);
        assert!(out.forward.abs() < 1e-4);
    }
}
