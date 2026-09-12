use crate::block::table::{block_properties_get, BlockMaterial};

/// Checks if a player holding `held_item_id` can harvest drops from `block_id`.
/// Follows Minecraft Alpha 1.2.6 rules.
pub fn mining_can_harvest(block_id: i32, held_item_id: i32) -> bool {
    if block_id <= 0 || block_id >= 256 {
        return false;
    }

    let props = block_properties_get(block_id as u32);
    let mat = props.material;

    // Blocks not requiring tools to drop
    let requires_tool = mat == BlockMaterial::Rock as u8
        || mat == BlockMaterial::Iron as u8
        || mat == BlockMaterial::Snow as u8
        || mat == BlockMaterial::BuiltSnow as u8;

    if !requires_tool {
        return true;
    }

    // Snow materials: require spade (snow layer 78, snow block 80)
    if mat == BlockMaterial::Snow as u8 || mat == BlockMaterial::BuiltSnow as u8 {
        return matches!(held_item_id, 256 | 269 | 273 | 277 | 284) && (block_id == 78 || block_id == 80);
    }

    // Rock and Iron materials: require pickaxe of suitable tier
    let pick_tier = match held_item_id {
        270 | 285 => Some(0), // Wood, Gold
        274 => Some(1),       // Stone
        257 => Some(2),       // Iron
        278 => Some(3),       // Diamond
        _ => None,
    };

    let Some(tier) = pick_tier else {
        return false;
    };

    // Obsidian (49): diamond pickaxe only (tier 3)
    if block_id == 49 {
        return tier >= 3;
    }

    // Diamond (56, 57) and Gold (14, 41): iron pickaxe or better (tier >= 2)
    if block_id == 56 || block_id == 57 || block_id == 14 || block_id == 41 {
        return tier >= 2;
    }

    // Redstone (73, 74): iron pickaxe or better (tier >= 2) — Java nests
    // redstone under a separate >=2 check (ItemPickaxe.java:13).
    if block_id == 73 || block_id == 74 {
        return tier >= 2;
    }

    // Iron (15, 42): stone pickaxe or better (tier >= 1)
    if block_id == 15 || block_id == 42 {
        return tier >= 1;
    }

    // Other rock/iron blocks (stone, cobblestone, stairs, etc.): any pickaxe (tier >= 0)
    true
}

/// Returns digging speed multiplier for a held item against a specific block.
pub fn mining_get_str_vs_block(block_id: i32, held_item_id: i32) -> f32 {
    if block_id <= 0 || block_id >= 256 {
        return 1.0;
    }

    // Swords: 1.5x vs all blocks
    if matches!(held_item_id, 267 | 268 | 272 | 276 | 283) {
        return 1.5;
    }

    // Pickaxes
    let pick_speed = match held_item_id {
        270 | 285 => Some(2.0), // Wood, Gold
        274 => Some(4.0),       // Stone
        257 => Some(6.0),       // Iron
        278 => Some(8.0),       // Diamond
        _ => None,
    };

    if let Some(speed) = pick_speed {
        // Java ItemTool.getStrVsBlock: only the explicit ctor list, else 1.0.
        // No material fallback (a furnace/obsidian/brick is 1.0x without the
        // right pick in vanilla).
        let is_effective = matches!(
            block_id,
            4 | 43 | 44 | 1 | 48 | 15 | 42 | 16 | 41 | 14 | 56 | 57 | 79 | 87
        );
        if is_effective {
            return speed;
        }
    }

    // Spades
    let spade_speed = match held_item_id {
        269 | 284 => Some(2.0), // Wood, Gold
        273 => Some(4.0),       // Stone
        256 => Some(6.0),       // Iron
        277 => Some(8.0),       // Diamond
        _ => None,
    };

    if let Some(speed) = spade_speed {
        // Java: explicit list only — soil (60) is NOT spade-effective.
        let is_effective = matches!(block_id, 2 | 3 | 12 | 13 | 78 | 80 | 82);
        if is_effective {
            return speed;
        }
    }

    // Axes
    let axe_speed = match held_item_id {
        271 | 286 => Some(2.0), // Wood, Gold
        275 => Some(4.0),       // Stone
        258 => Some(6.0),       // Iron
        279 => Some(8.0),       // Diamond
        _ => None,
    };

    if let Some(speed) = axe_speed {
        // Java: explicit list only — doors etc. are 1.0x.
        let is_effective = matches!(block_id, 5 | 47 | 17 | 54);
        if is_effective {
            return speed;
        }
    }

    1.0
}

/// Returns block hardness progress per tick applied by player.
pub fn mining_check_hardness(
    block_id: i32,
    held_item_id: i32,
    in_water: bool,
    on_ground: bool,
) -> f32 {
    if block_id <= 0 || block_id >= 256 {
        return 0.0;
    }

    let props = block_properties_get(block_id as u32);
    if props.hardness < 0.0 {
        return 0.0; // Bedrock / unbreakable
    }

    if props.hardness == 0.0 {
        return 1.0; // Instant break (e.g. torch, redstone, sapling)
    }

    let mut str_val = mining_get_str_vs_block(block_id, held_item_id);
    if in_water {
        str_val /= 5.0;
    }
    if !on_ground {
        str_val /= 5.0;
    }

    let can_harvest = mining_can_harvest(block_id, held_item_id);
    if can_harvest {
        str_val / props.hardness / 30.0
    } else {
        1.0 / props.hardness / 100.0
    }
}

/// Returns number of ticks required to break a block, or -1 if unbreakable.
pub fn mining_get_destroy_ticks(
    block_id: i32,
    held_item_id: i32,
    in_water: bool,
    on_ground: bool,
) -> i32 {
    if block_id <= 0 || block_id >= 256 {
        return -1;
    }

    let props = block_properties_get(block_id as u32);
    if props.hardness < 0.0 {
        return -1;
    }

    let hardness_per_tick = mining_check_hardness(block_id, held_item_id, in_water, on_ground);
    if hardness_per_tick >= 1.0 {
        return 0; // Instant
    }

    (1.0 / hardness_per_tick).ceil() as i32
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_harvestability() {
        // Dirt (3) can be harvested by hand
        assert!(mining_can_harvest(3, 0));

        // Stone (1) cannot be harvested by hand
        assert!(!mining_can_harvest(1, 0));
        // Stone (1) can be harvested by wood pickaxe (270)
        assert!(mining_can_harvest(1, 270));

        // Iron ore (15) cannot be harvested by wood pickaxe (270)
        assert!(!mining_can_harvest(15, 270));
        // Iron ore (15) can be harvested by stone pickaxe (274)
        assert!(mining_can_harvest(15, 274));

        // Diamond ore (56) requires iron pickaxe (257) or diamond pickaxe (278)
        assert!(!mining_can_harvest(56, 274)); // stone fails
        assert!(mining_can_harvest(56, 257));  // iron succeeds
        assert!(mining_can_harvest(56, 278));  // diamond succeeds

        // Obsidian (49) requires diamond pickaxe (278)
        assert!(!mining_can_harvest(49, 257)); // iron fails
        assert!(mining_can_harvest(49, 278));  // diamond succeeds
    }

    #[test]
    fn test_tool_speed_multipliers() {
        // Bare hand vs stone = 1.0
        assert_eq!(mining_get_str_vs_block(1, 0), 1.0);

        // Diamond pickaxe (278) vs stone (1) = 8.0
        assert_eq!(mining_get_str_vs_block(1, 278), 8.0);

        // Diamond axe (279) vs wood log (17) = 8.0
        assert_eq!(mining_get_str_vs_block(17, 279), 8.0);

        // Diamond spade (277) vs dirt (3) = 8.0
        assert_eq!(mining_get_str_vs_block(3, 277), 8.0);

        // Sword vs stone = 1.5
        assert_eq!(mining_get_str_vs_block(1, 276), 1.5);
    }

    #[test]
    fn test_destroy_ticks() {
        // Bedrock (7) -> unbreakable (-1)
        assert_eq!(mining_get_destroy_ticks(7, 278, false, true), -1);

        // Torch (50) -> instant (0 ticks)
        assert_eq!(mining_get_destroy_ticks(50, 0, false, true), 0);

        // Stone (1, hardness 1.5) with diamond pickaxe (str 8.0, can harvest):
        // hardness_per_tick = 8.0 / 1.5 / 30.0 = 0.177777...
        // destroy_ticks = ceil(1.0 / 0.177777) = 6 ticks
        let ticks = mining_get_destroy_ticks(1, 278, false, true);
        assert_eq!(ticks, 6);

        // Submerged in water and in air reduces speed by 5x each (25x total)
        let ticks_water_air = mining_get_destroy_ticks(1, 278, true, false);
        assert!(ticks_water_air > ticks * 20);
    }
}
