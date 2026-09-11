#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum MovementValidationStatus {
    Ok = 0,
    IllegalStance = 1,
    IllegalPosition = 2,
    MovedTooQuickly = 3,
    MovedWrongly = 4, // Soft reject (> 225.0, requires teleport reset)
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct FfiMovementInput {
    pub from_x: f64,
    pub from_y: f64,
    pub from_z: f64,
    pub to_x: f64,
    pub to_y: f64,
    pub to_z: f64,
    pub stance: f64,
    pub on_ground: bool,
    pub is_in_water: bool,
    pub fall_distance: f32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct FfiMovementResult {
    pub status: u8,
    pub new_fall_distance: f32,
    pub fall_damage: i32,
    pub move_sq: f64,
}

pub const SOFT_MOVEMENT_REJECT_SQ: f64 = 225.0;
pub const HARD_MOVEMENT_REJECT_SQ: f64 = 900.0;
pub const MAX_COORDINATE: f64 = 3.2e7;

/// Validates movement packet inputs and updates fall distance / damage according to Alpha 1.2.6 rules.
pub fn alpha_movement_validate(input: &FfiMovementInput) -> FfiMovementResult {
    let inp = input;

    // 1. Check stance: stance - y must be within [0.1, 1.65]
    let stance_diff = inp.stance - inp.to_y;
    if stance_diff < 0.1 || stance_diff > 1.65 {
        return FfiMovementResult {
            status: MovementValidationStatus::IllegalStance as u8,
            new_fall_distance: inp.fall_distance,
            fall_damage: 0,
            move_sq: 0.0,
        };
    }

    // 2. Check world boundary
    if inp.to_x.abs() > MAX_COORDINATE || inp.to_z.abs() > MAX_COORDINATE {
        return FfiMovementResult {
            status: MovementValidationStatus::IllegalPosition as u8,
            new_fall_distance: inp.fall_distance,
            fall_damage: 0,
            move_sq: 0.0,
        };
    }

    // 3. Movement delta squared
    let dx = inp.to_x - inp.from_x;
    let dy = inp.to_y - inp.from_y;
    let dz = inp.to_z - inp.from_z;
    let move_sq = dx * dx + dy * dy + dz * dz;

    if move_sq > HARD_MOVEMENT_REJECT_SQ {
        return FfiMovementResult {
            status: MovementValidationStatus::MovedTooQuickly as u8,
            new_fall_distance: inp.fall_distance,
            fall_damage: 0,
            move_sq,
        };
    }

    if move_sq > SOFT_MOVEMENT_REJECT_SQ {
        return FfiMovementResult {
            status: MovementValidationStatus::MovedWrongly as u8,
            new_fall_distance: inp.fall_distance,
            fall_damage: 0,
            move_sq,
        };
    }

    // 4. Fall distance and damage calculations
    let mut fall_distance = inp.fall_distance;
    let mut fall_damage = 0;

    if inp.on_ground {
        if inp.is_in_water {
            fall_distance = 0.0;
        } else if fall_distance > 0.0 {
            let dmg = (fall_distance - 3.0).ceil() as i32;
            if dmg > 0 {
                fall_damage = dmg;
            }
            fall_distance = 0.0;
        }
    } else if dy < 0.0 {
        fall_distance -= dy as f32;
    }

    FfiMovementResult {
        status: MovementValidationStatus::Ok as u8,
        new_fall_distance: fall_distance,
        fall_damage,
        move_sq,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_valid_movement_on_ground() {
        let input = FfiMovementInput {
            from_x: 0.0,
            from_y: 64.0,
            from_z: 0.0,
            to_x: 0.2,
            to_y: 64.0,
            to_z: 0.0,
            stance: 65.62,
            on_ground: true,
            is_in_water: false,
            fall_distance: 0.0,
        };
        let res = alpha_movement_validate(&input);
        assert_eq!(res.status, MovementValidationStatus::Ok as u8);
        assert_eq!(res.fall_damage, 0);
        assert_eq!(res.new_fall_distance, 0.0);
    }

    #[test]
    fn test_illegal_stance() {
        let input = FfiMovementInput {
            from_x: 0.0,
            from_y: 64.0,
            from_z: 0.0,
            to_x: 0.0,
            to_y: 64.0,
            to_z: 0.0,
            stance: 64.0, // difference = 0.0 < 0.1
            on_ground: true,
            is_in_water: false,
            fall_distance: 0.0,
        };
        let res = alpha_movement_validate(&input);
        assert_eq!(res.status, MovementValidationStatus::IllegalStance as u8);
    }

    #[test]
    fn test_illegal_position() {
        let input = FfiMovementInput {
            from_x: 0.0,
            from_y: 64.0,
            from_z: 0.0,
            to_x: 4.0e7,
            to_y: 64.0,
            to_z: 0.0,
            stance: 65.62,
            on_ground: true,
            is_in_water: false,
            fall_distance: 0.0,
        };
        let res = alpha_movement_validate(&input);
        assert_eq!(res.status, MovementValidationStatus::IllegalPosition as u8);
    }

    #[test]
    fn test_moved_too_quickly() {
        let input = FfiMovementInput {
            from_x: 0.0,
            from_y: 64.0,
            from_z: 0.0,
            to_x: 40.0, // 40^2 = 1600 > 900
            to_y: 64.0,
            to_z: 0.0,
            stance: 65.62,
            on_ground: true,
            is_in_water: false,
            fall_distance: 0.0,
        };
        let res = alpha_movement_validate(&input);
        assert_eq!(res.status, MovementValidationStatus::MovedTooQuickly as u8);
    }

    #[test]
    fn test_fall_damage() {
        // Fall 10 blocks in air
        let input_falling = FfiMovementInput {
            from_x: 0.0,
            from_y: 74.0,
            from_z: 0.0,
            to_x: 0.0,
            to_y: 64.0,
            to_z: 0.0,
            stance: 65.62,
            on_ground: false,
            is_in_water: false,
            fall_distance: 0.0,
        };
        let res1 = alpha_movement_validate(&input_falling);
        assert_eq!(res1.status, MovementValidationStatus::Ok as u8);
        assert_eq!(res1.new_fall_distance, 10.0);
        assert_eq!(res1.fall_damage, 0);

        // Land on ground: damage = ceil(10.0 - 3.0) = 7
        let input_landing = FfiMovementInput {
            from_x: 0.0,
            from_y: 64.0,
            from_z: 0.0,
            to_x: 0.0,
            to_y: 64.0,
            to_z: 0.0,
            stance: 65.62,
            on_ground: true,
            is_in_water: false,
            fall_distance: res1.new_fall_distance,
        };
        let res2 = alpha_movement_validate(&input_landing);
        assert_eq!(res2.status, MovementValidationStatus::Ok as u8);
        assert_eq!(res2.fall_damage, 7);
        assert_eq!(res2.new_fall_distance, 0.0);
    }

    #[test]
    fn test_water_landing_negates_fall() {
        let input = FfiMovementInput {
            from_x: 0.0,
            from_y: 64.0,
            from_z: 0.0,
            to_x: 0.0,
            to_y: 64.0,
            to_z: 0.0,
            stance: 65.62,
            on_ground: true,
            is_in_water: true,
            fall_distance: 20.0,
        };
        let res = alpha_movement_validate(&input);
        assert_eq!(res.status, MovementValidationStatus::Ok as u8);
        assert_eq!(res.fall_damage, 0);
        assert_eq!(res.new_fall_distance, 0.0);
    }
}
