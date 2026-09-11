//! Player-specific helpers ported from C++ `EntityPlayerMP` (mirrors Java
//! `EntityPlayerMP` / `EntityPlayer`).
//!
//! Only closed decisions move: death-cause priority and death-drop
//! velocities. Name formatting (usernames), inventory iteration, packets,
//! and the entity table stay in C++.

/// Death-cause codes in `updateDeathMessage` priority order. C++ resolves
/// the attacker kind (0 = player, 1 = mob, 2 = animal, else environmental)
/// and formats the username-bearing message itself.
#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DeathCause {
    SlainByPlayer = 0,
    SlainByMob = 1,
    SlainByAnimal = 2,
    Fall = 3,
    Cactus = 4,
    Drown = 5,
    Lava = 6,
    Fire = 7,
    Generic = 8,
}

pub fn alpha_player_death_cause(
    has_attacker: bool,
    attacker_kind: u8,
    fall_distance: f32,
    on_cactus: bool,
    drowning: bool,
    in_lava: bool,
    on_fire: bool,
) -> u8 {
    if has_attacker {
        match attacker_kind {
            0 => return DeathCause::SlainByPlayer as u8,
            1 => return DeathCause::SlainByMob as u8,
            2 => return DeathCause::SlainByAnimal as u8,
            _ => {}
        }
    }
    if fall_distance > 3.0 {
        return DeathCause::Fall as u8;
    }
    if on_cactus {
        return DeathCause::Cactus as u8;
    }
    if drowning {
        return DeathCause::Drown as u8;
    }
    if in_lava {
        return DeathCause::Lava as u8;
    }
    if on_fire {
        return DeathCause::Fire as u8;
    }
    DeathCause::Generic as u8
}

/// Death-drop velocities (mirrors the `onDeath` drop loop:
/// `(rand - 0.5) * 0.2` sideways, `0.2 + rand * 0.1` up). Draws stay in
/// C++ (`rngNextDouble`); this owns the shape.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct DropVelocity {
    pub mx: f64,
    pub my: f64,
    pub mz: f64,
}

pub fn alpha_player_drop_velocity(ra: f64, rb: f64, rc: f64) -> DropVelocity {
    DropVelocity { mx: (ra - 0.5) * 0.2, my: 0.2 + rb * 0.1, mz: (rc - 0.5) * 0.2 }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_death_cause_priority() {
        assert_eq!(alpha_player_death_cause(true, 0, 99.0, true, true, true, true), 0);
        assert_eq!(alpha_player_death_cause(true, 1, 99.0, true, true, true, true), 1);
        assert_eq!(alpha_player_death_cause(true, 2, 99.0, true, true, true, true), 2);
        // Unknown attacker kind falls through to the environment.
        assert_eq!(alpha_player_death_cause(true, 9, 99.0, false, false, false, false), 3);
        assert_eq!(alpha_player_death_cause(false, 0, 2.0, true, true, true, true), 4);
        assert_eq!(alpha_player_death_cause(false, 0, 2.0, false, true, true, true), 5);
        assert_eq!(alpha_player_death_cause(false, 0, 2.0, false, false, true, true), 6);
        assert_eq!(alpha_player_death_cause(false, 0, 2.0, false, false, false, true), 7);
        assert_eq!(alpha_player_death_cause(false, 0, 2.0, false, false, false, false), 8);
        // Boundary: exactly 3.0 is not a fall.
        assert_eq!(alpha_player_death_cause(false, 0, 3.0, false, false, false, false), 8);
    }

    #[test]
    fn test_drop_velocity_shape() {
        let v = alpha_player_drop_velocity(0.5, 0.5, 0.5);
        assert_eq!((v.mx, v.my, v.mz), (0.0, 0.25, 0.0));
        let v = alpha_player_drop_velocity(0.0, 0.0, 1.0);
        assert_eq!((v.mx, v.my, v.mz), (-0.1, 0.2, 0.1));
    }
}
