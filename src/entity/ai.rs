//! Creature steering math ported from C++ `EntityCreature` (mirrors Java
//! `EntityCreature`/`EntityLiving.func_147_b`).
//!
//! Closed-form angle math plus the pure AI decision kernels (wander pick,
//! chase-speed factor). The FFI shells (`alpha_ai_*`) serve the live C++
//! server; the native world (`world.rs`) calls the `_run` cores directly so
//! both paths share one flow. RNG draws stay with the caller (C++ feeds the
//! global bridge RNG, the native world feeds its own `JavaRandom`).
//!
//! Angle convention notes (kept 1:1): yaw is degrees, `atan2(dz, dx)` in
//! `f64` narrowed to `f32`, and the strafe helpers use the quantized
//! `math_helper` sin/cos tables exactly like C++ `MathHelper::sin/cos`.

use crate::math_helper::{cos, sin};

/// Turn clamp (mirrors `EntityCreature::clampAngle`).
pub fn alpha_ai_clamp_angle(current: f32, target: f32, max_delta: f32) -> f32 {
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
/// Pure facing core shared by the FFI shell and the native world.
/// Returns `(new_yaw, new_pitch)`; the pitch already includes the leading
/// negation from C++.
pub fn face_run(dx: f64, dz: f64, dy: f64, cur_yaw: f32, cur_pitch: f32, max_turn: f32) -> (f32, f32) {
    let dist = crate::math_helper::sqrt_double(dx * dx + dz * dz);
    let yaw = (dz.atan2(dx) * 180.0 / std::f64::consts::PI) as f32 - 90.0;
    let pitch = (dy.atan2(dist as f64) * 180.0 / std::f64::consts::PI) as f32;
    (clamp_inner(cur_yaw, yaw, max_turn), -clamp_inner(cur_pitch, pitch, max_turn))
}

pub fn alpha_ai_face_angles(
    dx: f64,
    dz: f64,
    dy: f64,
    cur_yaw: f32,
    cur_pitch: f32,
    max_turn: f32,
    out_yaw: &mut f32,
    out_pitch: &mut f32,
) -> bool {
    let (yaw, pitch) = face_run(dx, dz, dy, cur_yaw, cur_pitch, max_turn);
    *out_yaw = yaw;
    *out_pitch = pitch;
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

/// Wander weights (mirrors `getBlockPathWeight`): animals prefer grass
/// (10.0), else light brightness (0..1 float) minus a half; mobs score
/// 0.5 minus brightness, so the darkest candidate wins.
pub fn alpha_ai_animal_path_weight(below_grass: bool, brightness: f32) -> f32 {
    if below_grass {
        10.0
    } else {
        brightness - 0.5
    }
}

pub fn alpha_ai_mob_path_weight(brightness: f32) -> f32 {
    0.5 - brightness
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

/// Pure steering core shared by the FFI shell and the native world
/// (mirrors the steering block in `EntityCreature::followPath`).
pub fn steer_run(
    dx: f64,
    dz: f64,
    dy: f64,
    cur_yaw: f32,
    is_attacking: bool,
    has_target: bool,
    tgt_dx: f64,
    tgt_dz: f64,
    forward_in: f32,
) -> SteerOut {
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

    SteerOut { new_yaw, strafe, forward, jump: dy > 0.0 }
}

pub fn alpha_ai_steer_to_point(
    dx: f64,
    dz: f64,
    dy: f64,
    cur_yaw: f32,
    is_attacking: bool,
    has_target: bool,
    tgt_dx: f64,
    tgt_dz: f64,
    forward_in: f32,
    out: &mut SteerOut,
) -> bool {
    *out = steer_run(dx, dz, dy, cur_yaw, is_attacking, has_target, tgt_dx, tgt_dz, forward_in);
    true
}

/// Wander destination pick (mirrors `EntityCreature::pickWanderDestination`):
/// best of 10 random points in the 13x7x13 area around `base`, strict `>`
/// comparison from `bestWeight = -99999.0`, so ties keep the first
/// candidate. Draw order per candidate is x-size, y-size, x-size like C++.
/// Returns `None` only when zero iterations run (never with the C++ count).
pub fn wander_pick(
    base: [i32; 3],
    next: &mut dyn FnMut(i32) -> i32,
    weight: &mut dyn FnMut(i32, i32, i32) -> f32,
) -> Option<[i32; 3]> {
    let mut best: Option<[i32; 3]> = None;
    let mut best_weight = -99999.0f32;
    for _ in 0..10 {
        let cx = base[0] + next(13) - 6;
        let cy = base[1] + next(7) - 3;
        let cz = base[2] + next(13) - 6;
        let w = weight(cx, cy, cz);
        if w > best_weight {
            best_weight = w;
            best = Some([cx, cy, cz]);
        }
    }
    best
}

/// Chase speed: vanilla `EntityCreature` uses `moveSpeed` directly
/// (`field_9130_bp = field_9126_bt`); no 1.2x/0.85x factor exists.
/// Kept as a function so call sites stay explicit.
pub fn chase_speed(base: f32, _dist: f32, _reach: f32) -> f32 {
    base
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
        assert!(alpha_ai_face_angles(5.0, 0.0, 0.0, 0.0, 0.0, 30.0, &mut yaw, &mut pitch));
        assert_eq!(yaw, -30.0); // clamped turn toward -90
        assert_eq!(pitch, -0.0);
    }

    #[test]
    fn test_steer_straight_ahead() {
        // Point due east of a creature facing east (yaw -90): no turn.
        let mut out = SteerOut { new_yaw: 0.0, strafe: 0.0, forward: 0.0, jump: false };
        assert!(
            alpha_ai_steer_to_point(5.0, 0.0, 0.0, -90.0, false, false, 0.0, 0.0, 0.0, &mut out)
        );
        assert!((out.new_yaw + 90.0).abs() < 1e-4);
        assert_eq!(out.strafe, 0.0);
        assert!(!out.jump);
    }

    #[test]
    fn test_steer_turn_rate_limited() {
        // Point due west while facing east: 180 normalised to -180,
        // clamped to a -30 turn.
        let mut out = SteerOut { new_yaw: 0.0, strafe: 0.0, forward: 0.0, jump: false };
        assert!(
            alpha_ai_steer_to_point(-5.0, 0.0, 1.0, -90.0, false, false, 0.0, 0.0, 0.0, &mut out)
        );
        assert!((out.new_yaw + 120.0).abs() < 1e-4);
        assert!(out.jump);
    }

        #[test]
    fn test_steer_strafe_uses_input_forward() {
        // Attacking: strafe formula applied to the passed forward value.
        let mut out = SteerOut { new_yaw: 0.0, strafe: 0.0, forward: 0.0, jump: false };
        assert!(
            alpha_ai_steer_to_point(5.0, 0.0, 0.0, -90.0, true, true, 5.0, 0.0, 0.7, &mut out)
        );
        // Target straight ahead: strafe angle 90deg -> sin=1, cos~0.
        assert!((out.strafe + 0.7).abs() < 1e-4);
        assert!(out.forward.abs() < 1e-4);
    }

    #[test]
    fn test_face_run_matches_ffi_shell() {
        let (mut yaw, mut pitch) = (0.0f32, 0.0f32);
        assert!(alpha_ai_face_angles(5.0, 0.0, 0.0, 0.0, 0.0, 30.0, &mut yaw, &mut pitch));
        assert_eq!(face_run(5.0, 0.0, 0.0, 0.0, 0.0, 30.0), (yaw, pitch));
    }

    #[test]
    fn test_steer_run_matches_ffi_shell() {
        let a = steer_run(5.0, 1.0, 1.0, -90.0, true, true, 5.0, 0.0, 0.7);
        let mut b = SteerOut { new_yaw: 0.0, strafe: 0.0, forward: 0.0, jump: false };
        assert!(
            alpha_ai_steer_to_point(5.0, 1.0, 1.0, -90.0, true, true, 5.0, 0.0, 0.7, &mut b)
        );
        assert_eq!((a.new_yaw, a.strafe, a.forward, a.jump), (b.new_yaw, b.strafe, b.forward, b.jump));
    }

    #[test]
    fn test_wander_pick_prefers_best_weight() {
        // Candidate k sits at base + (k, 0, k); weight grows with x.
        // Draws arrive bound-first: 13, 7, 13 per candidate.
        let mut seq = (0..30).map(|i| match i % 3 {
            1 => 3,
            _ => 6 + (i / 3),
        }).collect::<Vec<_>>().into_iter();
        let mut next = |bound: i32| {
            let v = seq.next().unwrap();
            assert!(bound == 13 || bound == 7);
            v
        };
        let mut weight = |x: i32, _: i32, _: i32| x as f32;
        assert_eq!(
            wander_pick([100, 64, 100], &mut next, &mut weight),
            Some([109, 64, 109])
        );
    }

    #[test]
    fn test_wander_pick_ties_keep_first() {
        let mut next = |bound: i32| bound / 2; // 13->6, 7->3: offset 0 every time
        let mut weight = |_: i32, _: i32, _: i32| 0.0; // mob-style flat score
        assert_eq!(
            wander_pick([10, 64, 10], &mut next, &mut weight),
            Some([10, 64, 10])
        );
    }

    #[test]
    fn test_chase_speed_is_base() {
        // Vanilla uses moveSpeed directly, no distance factor.
        assert_eq!(chase_speed(0.5, 5.0, 2.5), 0.5);
        assert_eq!(chase_speed(0.5, 3.0, 2.5), 0.5);
    }
}
