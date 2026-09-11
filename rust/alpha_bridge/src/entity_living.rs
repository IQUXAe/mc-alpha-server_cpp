//! Living-entity logic ported from C++ `EntityLiving` (mirrors Java
//! `EntityLiving`): healing, the damage pipeline, per-tick suffocation /
//! drowning / timers, movement helpers, and fall damage.
//!
//! Split along the virtual boundary: Rust owns damage math, knockback, and
//! movement integration; C++ keeps virtual dispatch (`onDeath`, `onFall`
//! overrides elsewhere, `sendEntityStatus`), the entity table, and world
//! queries (passed in as scalars or narrow callbacks).
//!
//! RNG: the old knockback jitter used `std::rand()/RAND_MAX`; it now draws
//! `next_f01` (uniform `[0,1)`, same stream family as the spawner draws).

use crate::math_helper::{cos, sin};

/// Result of one damage event. C++ applies fields, motion, the hurt status
/// packet, and `onDeath` from these flags.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct AttackResult {
    pub health: i16,
    pub last_damage: i32,
    pub hurt_resist: i32,
    pub hurt_time: i32,
    pub attack_time: i32,
    pub knocked: bool,
    pub kmx: f64,
    pub kmy: f64,
    pub kmz: f64,
    pub send_status: bool,
    pub died: bool,
}

/// Pure heal (mirrors `EntityLiving::heal`).
#[no_mangle]
pub extern "C" fn alpha_living_heal(health: i16, max_health: i16, amount: i32, dead: bool) -> i16 {
    if amount <= 0 || dead || health <= 0 {
        return health;
    }
    (max_health as i32).min(health as i32 + amount) as i16
}

/// Damage pipeline (mirrors `EntityLiving::attackEntityFrom`). Returns false
/// (leaving `out` untouched) when the hit is ignored outright. Timer inputs
/// round-trip so the resist-window branch (which leaves them alone) applies
/// cleanly on the C++ side.
#[no_mangle]
#[allow(clippy::too_many_arguments)]
pub unsafe extern "C" fn alpha_living_attack(
    next_f01: Option<extern "C" fn() -> f64>,
    health: i16,
    hurt_resist: i32,
    max_hurt_resist: i32,
    last_damage: i32,
    hurt_time_in: i32,
    attack_time_in: i32,
    dead: bool,
    amount: i32,
    has_attacker: bool,
    self_x: f64,
    self_z: f64,
    atk_x: f64,
    atk_z: f64,
    motion_x: f64,
    motion_y: f64,
    motion_z: f64,
    out: *mut AttackResult,
) -> bool {
    if out.is_null() || amount <= 0 || dead || health <= 0 {
        return false;
    }
    let mut r = AttackResult {
        health,
        last_damage,
        hurt_resist,
        hurt_time: hurt_time_in,
        attack_time: attack_time_in,
        knocked: false,
        kmx: motion_x,
        kmy: motion_y,
        kmz: motion_z,
        send_status: false,
        died: false,
    };
    let mut knockback = true;
    if hurt_resist > max_hurt_resist / 2 {
        if amount <= last_damage {
            return false;
        }
        r.health = health.wrapping_sub((amount - last_damage) as i16);
        r.last_damage = amount;
        knockback = false;
    } else {
        r.last_damage = amount;
        r.hurt_resist = max_hurt_resist;
        r.hurt_time = 10;
        r.attack_time = 10;
        r.health = health.wrapping_sub(amount as i16);
    }

    if knockback && has_attacker {
        let mut dx = atk_x - self_x;
        let mut dz = atk_z - self_z;
        // Degenerate direction: jitter until non-trivial (C++ draw order kept).
        let mut guard = 0;
        while dx * dx + dz * dz < 1.0e-4 {
            let a = next_f01.map(|f| f()).unwrap_or(0.0);
            let b = next_f01.map(|f| f()).unwrap_or(0.0);
            let c = next_f01.map(|f| f()).unwrap_or(0.0);
            let d = next_f01.map(|f| f()).unwrap_or(0.0);
            dx = (a - b) * 0.01;
            dz = (c - d) * 0.01;
            guard += 1;
            if guard > 64 {
                break;
            }
        }
        let dist = (dx * dx + dz * dz).sqrt();
        if dist > 0.0 {
            let (mut mx, mut my, mut mz) = (motion_x * 0.5, motion_y * 0.5, motion_z * 0.5);
            mx -= dx / dist * 0.4;
            my += 0.4;
            mz -= dz / dist * 0.4;
            if my > 0.4 {
                my = 0.4;
            }
            r.knocked = true;
            r.kmx = mx;
            r.kmy = my;
            r.kmz = mz;
        }
        r.send_status = true;
    }

    if r.health <= 0 {
        r.died = true;
    }
    unsafe {
        *out = r;
    }
    true
}

/// Per-tick living state (mirrors `EntityLiving::tick` minus the virtual
/// calls, which C++ fires from the flags).
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct LivingTick {
    pub air: i32,
    pub hurt_time: i32,
    pub attack_time: i32,
    pub hurt_resist: i32,
    pub suffocate: bool,
    pub drown: bool,
}

#[no_mangle]
pub extern "C" fn alpha_living_tick(
    alive: bool,
    inside_opaque: bool,
    in_water: bool,
    air: i32,
    hurt_time: i32,
    attack_time: i32,
    hurt_resist: i32,
) -> LivingTick {
    let (mut a, mut h, mut at, mut hr) = (air, hurt_time, attack_time, hurt_resist);
    let mut suffocate = false;
    let mut drown = false;
    if alive && inside_opaque {
        suffocate = true;
    }
    if alive && in_water {
        a -= 1;
        if a <= -20 {
            a = 0;
            drown = true;
        }
    } else {
        a = 300;
    }
    if h > 0 {
        h -= 1;
    }
    if at > 0 {
        at -= 1;
    }
    if hr > 0 {
        hr -= 1;
    }
    LivingTick { air: a, hurt_time: h, attack_time: at, hurt_resist: hr, suffocate, drown }
}

/// Fall-damage amount for living entities (mirrors `EntityLiving::onFall`;
// the `ceil(distance - 3)` Java formula). Zero means no damage.
#[no_mangle]
pub extern "C" fn alpha_living_fall_damage(distance: f32) -> i32 {
    let damage = (distance - 3.0f32).ceil() as i32;
    if damage > 0 {
        damage
    } else {
        0
    }
}

/// Steering delta produced by `fly_apply` (internal; crosses no FFI).
#[derive(Clone, Copy, Debug)]
struct FlyOut {
    dmx: f32,
    dmz: f32,
}

/// World access for the heading driver. `do_move` runs C++ `moveEntity`/// and reports the post-move ground/contact state plus position.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct MoveFeedback {
    pub on_ground: bool,
    pub collided_vert: bool,
    pub pos_y: f64,
}

#[repr(C)]
pub struct HeadingWorld {
    pub touching_liquid: Option<extern "C" fn() -> bool>,
    pub on_ladder: Option<extern "C" fn() -> bool>,
    pub do_move: Option<extern "C" fn(dx: f64, dy: f64, dz: f64, out: *mut MoveFeedback) -> bool>,
}

/// Full heading integration (mirrors `EntityLiving::moveEntityWithHeading`).
/// Motion/fall state round-trips through `io`; exactly one `do_move` fires.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct HeadingIo {
    pub motion_x: f64,
    pub motion_y: f64,
    pub motion_z: f64,
    pub fall_distance: f32,
}

#[no_mangle]
pub unsafe extern "C" fn alpha_living_heading(
    world: *const HeadingWorld,
    strafe: f32,
    forward: f32,
    jumping: bool,
    on_ground: bool,
    yaw: f32,
    io: *mut HeadingIo,
) -> bool {
    if world.is_null() || io.is_null() {
        return false;
    }
    let w = unsafe { &*world };
    let io = unsafe { &mut *io };
    let liquid = w.touching_liquid.map(|f| f()).unwrap_or(false);
    let ladder = || w.on_ladder.map(|f| f()).unwrap_or(false);

    if jumping {
        if liquid {
            io.motion_y += 0.04;
        } else if on_ground {
            io.motion_y = 0.42;
        }
    }

    let Some(do_move) = w.do_move else {
        return false;
    };
    let mut fb = MoveFeedback { on_ground: false, collided_vert: false, pos_y: 0.0 };

    if liquid {
        let mut fly = FlyOut { dmx: 0.0, dmz: 0.0 };
        if fly_apply(strafe, forward, 0.02, yaw, &mut fly) {
            io.motion_x += fly.dmx as f64;
            io.motion_z += fly.dmz as f64;
        }
        let start_y = io.motion_y;
        if !do_move(io.motion_x, io.motion_y, io.motion_z, &mut fb) {
            return false;
        }
        io.motion_x *= 0.8;
        io.motion_y *= 0.8;
        io.motion_z *= 0.8;
        io.motion_y -= 0.02;
        if (fb.on_ground || fb.pos_y <= start_y) && jumping {
            io.motion_y = 0.3;
        }
        return true;
    }

    let friction: f32 = if on_ground { 0.546 } else { 0.91 };
    let accel = 0.16277136f32 / (friction * friction * friction);
    let applied = if on_ground { 0.1f32 * accel } else { 0.02f32 };
    let mut fly = FlyOut { dmx: 0.0, dmz: 0.0 };
    if fly_apply(strafe, forward, applied, yaw, &mut fly) {
        io.motion_x += fly.dmx as f64;
        io.motion_z += fly.dmz as f64;
    }

    if ladder() {
        io.fall_distance = 0.0;
        if io.motion_y < -0.15 {
            io.motion_y = -0.15;
        }
    }

    if !do_move(io.motion_x, io.motion_y, io.motion_z, &mut fb) {
        return false;
    }

    if fb.collided_vert && ladder() {
        io.motion_y = 0.2;
    }

    io.motion_y -= 0.08;
    io.motion_y *= 0.98;
    io.motion_x *= friction as f64;
    io.motion_z *= friction as f64;
    true
}

fn fly_apply(strafe: f32, forward: f32, acceleration: f32, yaw: f32, out: &mut FlyOut) -> bool {
    let mut magnitude = strafe * strafe + forward * forward;
    if magnitude < 1.0e-4f32 {
        return false;
    }
    magnitude = magnitude.sqrt();
    if magnitude < 1.0 {
        magnitude = 1.0;
    }
    magnitude = acceleration / magnitude;
    let (s, f) = (strafe * magnitude, forward * magnitude);
    let radians = yaw * (std::f32::consts::PI / 180.0);
    let (sy, cy) = (sin(radians), cos(radians));
    out.dmx = s * cy - f * sy;
    out.dmz = f * cy + s * sy;
    true
}

#[cfg(test)]
mod tests {
    use super::*;

    fn attack(
        health: i16,
        resist: i32,
        max_resist: i32,
        last: i32,
        amount: i32,
        attacker: Option<(f64, f64)>,
        me: (f64, f64),
        motion: (f64, f64, f64),
    ) -> Option<AttackResult> {
        let mut out = AttackResult {
            health: 0,
            last_damage: 0,
            hurt_resist: 0,
            hurt_time: 0,
            attack_time: 0,
            knocked: false,
            kmx: 0.0,
            kmy: 0.0,
            kmz: 0.0,
            send_status: false,
            died: false,
        };
        let ok = unsafe {
            alpha_living_attack(
                None,
                health,
                resist,
                max_resist,
                last,
                0,
                0,
                false,
                amount,
                attacker.is_some(),
                me.0,
                me.1,
                attacker.map(|a| a.0).unwrap_or(0.0),
                attacker.map(|a| a.1).unwrap_or(0.0),
                motion.0,
                motion.1,
                motion.2,
                &mut out,
            )
        };
        ok.then_some(out)
    }

    #[test]
    fn test_heal_clamps_and_guards() {
        assert_eq!(alpha_living_heal(10, 20, 5, false), 15);
        assert_eq!(alpha_living_heal(18, 20, 5, false), 20);
        assert_eq!(alpha_living_heal(10, 20, 0, false), 10);
        assert_eq!(alpha_living_heal(0, 20, 5, false), 0);
        assert_eq!(alpha_living_heal(10, 20, 5, true), 10);
    }

    #[test]
    fn test_attack_ignored() {
        assert!(attack(20, 0, 20, 0, 0, None, (0.0, 0.0), (0.0, 0.0, 0.0)).is_none());
        assert!(attack(0, 0, 20, 0, 5, None, (0.0, 0.0), (0.0, 0.0, 0.0)).is_none());
        assert!(attack(20, 0, 20, 0, -3, None, (0.0, 0.0), (0.0, 0.0, 0.0)).is_none());
    }

    #[test]
    fn test_attack_fresh_hit_sets_timers() {
        let r = attack(20, 0, 20, 0, 6, None, (0.0, 0.0), (1.0, 2.0, 3.0)).unwrap();
        assert_eq!((r.health, r.last_damage), (14, 6));
        assert_eq!((r.hurt_resist, r.hurt_time, r.attack_time), (20, 10, 10));
        assert!(!r.knocked); // no attacker
        assert!(!r.send_status);
        assert!(!r.died);
    }

    #[test]
    fn test_attack_resist_window() {
        // resist 15 > 10: weaker hit ignored, stronger hits the difference.
        assert!(attack(14, 15, 20, 6, 5, None, (0.0, 0.0), (0.0, 0.0, 0.0)).is_none());
        let r = attack(14, 15, 20, 6, 9, None, (0.0, 0.0), (0.0, 0.0, 0.0)).unwrap();
        assert_eq!((r.health, r.last_damage), (11, 9));
        assert!(!r.knocked);
        assert!(!r.send_status);
    }

    #[test]
    fn test_attack_kill_flags_death() {
        let r = attack(5, 0, 20, 0, 30, None, (0.0, 0.0), (0.0, 0.0, 0.0)).unwrap();
        assert!(r.health <= 0);
        assert!(r.died);
    }

    #[test]
    fn test_attack_knockback_direction() {
        // Attacker east of victim (+x): victim pushed west, motion damped.
        let r = attack(20, 0, 20, 0, 4, Some((5.0, 0.0)), (0.0, 0.0), (0.0, 0.0, 0.0)).unwrap();
        assert!(r.knocked);
        assert!(r.send_status);
        assert!(r.kmx < 0.0);
        assert_eq!(r.kmy, 0.4);
        assert_eq!(r.kmz, 0.0);
    }

    #[test]
    fn test_tick_timers_and_drown() {
        // Dry tick only decays timers.
        let t = alpha_living_tick(true, false, false, 123, 5, 3, 9);
        assert_eq!((t.air, t.hurt_time, t.attack_time, t.hurt_resist), (300, 4, 2, 8));
        assert!(!t.suffocate && !t.drown);
        // Suffocation flag.
        let t = alpha_living_tick(true, true, false, 300, 0, 0, 0);
        assert!(t.suffocate);
        // Drowning at the limit resets air and flags damage.
        let t = alpha_living_tick(true, false, true, -19, 0, 0, 0);
        assert_eq!(t.air, 0);
        assert!(t.drown);
        // Dead entity: timers still decay, no damage flags.
        let t = alpha_living_tick(false, true, true, 10, 2, 2, 2);
        assert!(!t.suffocate && !t.drown);
        assert_eq!((t.hurt_time, t.attack_time, t.hurt_resist), (1, 1, 1));
    }

    #[test]
    fn test_fall_damage_table() {
        assert_eq!(alpha_living_fall_damage(2.9), 0);
        assert_eq!(alpha_living_fall_damage(3.0), 0);
        assert_eq!(alpha_living_fall_damage(3.5), 1);
        assert_eq!(alpha_living_fall_damage(10.0), 7);
    }

    #[test]
    fn test_fly_negligible_input() {
        // Zero and sub-threshold inputs leave the caller's values alone.
        let mut out = FlyOut { dmx: 9.0, dmz: 9.0 };
        assert!(!fly_apply(0.0, 0.0, 0.1, 0.0, &mut out));
        assert_eq!((out.dmx, out.dmz), (9.0, 9.0));
        assert!(!fly_apply(0.0001, 0.0, 0.1, 0.0, &mut out));
        assert_eq!((out.dmx, out.dmz), (9.0, 9.0));
    }

    #[test]
    fn test_fly_forward_yaw_zero() {
        // Facing +z (yaw 0): forward maps to +z.
        let mut out = FlyOut { dmx: 0.0, dmz: 0.0 };
        assert!(fly_apply(0.0, 1.0, 0.1, 0.0, &mut out));
        assert!(out.dmx.abs() < 1e-6);
        assert!((out.dmz - 0.1).abs() < 1e-6);
    }
}
