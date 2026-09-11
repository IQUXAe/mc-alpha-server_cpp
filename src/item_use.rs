//! Item-use kernels ported from C++ `Item.cpp` (mirrors Java `Item*`).
//!
//! Only closed-form pieces move: furnace facing from yaw, sign-post yaw
//! metadata, and the food-bite step. Block placement, spawning, packets,
//! and stack objects stay in C++ until the item-verb phase.

/// Furnace facing metadata from player yaw (mirrors the
/// `furnaceFacingFromYaw` helper for `ItemBlock::onItemUse`):
/// 0→2(N), 1→5(E), 2→3(S), 3→4(W).
pub fn alpha_item_furnace_facing(yaw: f32) -> u8 {
    // Java: floor(yaw * 4/360 + 0.5) & 3, float arithmetic like C++.
    let facing = (yaw * 4.0f32 / 360.0f32 + 0.5f32).floor() as i32 & 3;
    const TABLE: [u8; 4] = [2, 5, 3, 4];
    TABLE[(facing & 3) as usize]
}

/// Sign-post yaw metadata (mirrors `ItemSign::onItemUse`):
/// `floor(double(yaw + 180.0f) * 16.0 / 360.0 + 0.5) & 15`.
/// Note the C++ double arithmetic (unlike the furnace f32 case).
pub fn alpha_item_sign_yaw_meta(yaw: f32) -> u8 {
    (((yaw + 180.0f32) as f64 * 16.0 / 360.0 + 0.5).floor() as i32 & 15) as u8
}

/// One food bite (mirrors `ItemFood::onItemRightClick` bookkeeping).
/// Returns the new stack count and the heal to apply (0 when empty).
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct FoodBite {
    pub new_count: i32,
    pub heal: i32,
}

pub fn alpha_item_food_bite(count: i32, heal_amount: i32) -> FoodBite {
    if count > 0 {
        FoodBite { new_count: count - 1, heal: heal_amount }
    } else {
        FoodBite { new_count: count, heal: 0 }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_furnace_facing_cardinals() {
        // Java mapping spot-checks through the quantizer.
        assert_eq!(alpha_item_furnace_facing(0.0), 2);
        assert_eq!(alpha_item_furnace_facing(90.0), 5);
        assert_eq!(alpha_item_furnace_facing(180.0), 3);
        assert_eq!(alpha_item_furnace_facing(270.0), 4);
        assert_eq!(alpha_item_furnace_facing(360.0), 2);
        assert_eq!(alpha_item_furnace_facing(-90.0), 4);
    }

    #[test]
    fn test_sign_yaw_meta_range() {
        for deg in (-720..=720).step_by(15) {
            let m = alpha_item_sign_yaw_meta(deg as f32);
            assert!(m < 16, "{deg} -> {m}");
        }
        assert_eq!(alpha_item_sign_yaw_meta(0.0), 8);
        assert_eq!(alpha_item_sign_yaw_meta(180.0), 0);
    }

    #[test]
    fn test_food_bite() {
        assert_eq!(
            alpha_item_food_bite(3, 4),
            FoodBite { new_count: 2, heal: 4 }
        );
        assert_eq!(
            alpha_item_food_bite(0, 4),
            FoodBite { new_count: 0, heal: 0 }
        );
    }
}
