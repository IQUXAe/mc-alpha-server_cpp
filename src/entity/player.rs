//! Player-specific helpers ported from C++ `EntityPlayerMP` (mirrors Java
//! `EntityPlayerMP` / `EntityPlayer`).
//!
//! Only closed decisions move: death-drop velocities. Name formatting
//! (usernames), inventory iteration, packets, and the entity table stay
//! in the server. (Alpha 1.2.6 has no death messages — `onDeath` only
//! drops the inventory — so no death-cause table is ported.)

// NOTE: no death-cause priority table. It belonged to newer versions with
// chat death messages; Alpha 1.2.6 multiplayer has none.

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
    fn test_drop_velocity_shape() {
        let v = alpha_player_drop_velocity(0.5, 0.5, 0.5);
        assert_eq!((v.mx, v.my, v.mz), (0.0, 0.25, 0.0));
        let v = alpha_player_drop_velocity(0.0, 0.0, 1.0);
        assert_eq!((v.mx, v.my, v.mz), (-0.1, 0.2, 0.1));
    }
}
