use crate::inventory::FfiItemStack;

/// Returns the maximum stack size for a given item or block ID in Minecraft Alpha 1.2.6.
#[no_mangle]
pub extern "C" fn alpha_inventory_max_stack_size(item_id: i32) -> i32 {
    if item_id <= 0 {
        return 0;
    }

    // Blocks (IDs 1-255) default to 64 stack size
    if item_id < 256 {
        return 64;
    }

    match item_id {
        // Shovels
        256 | 269 | 273 | 277 | 284 => 1,
        // Pickaxes
        257 | 270 | 274 | 278 | 285 => 1,
        // Axes
        258 | 271 | 275 | 279 | 286 => 1,
        // Swords
        267 | 268 | 272 | 276 | 283 => 1,
        // Hoes
        290 | 291 | 292 | 293 | 294 => 1,
        // Flint and steel, Bow, Fishing rod
        259 | 261 | 346 => 1,
        // Armor (Leather, Chain, Iron, Diamond, Gold)
        298..=317 => 1,
        // Signs, Doors, Buckets, Minecarts, Boats, Saddle, Stew
        282 | 323 | 324 | 325 | 326 | 327 | 328 | 329 | 330 | 333 | 335 | 342 | 343 => 1,
        // Stack of 16
        332 | 344 => 16, // Snowball, Egg
        // Default stack size for items
        _ => 64,
    }
}

/// Helper: returns the maximum durability for an armor item ID.
pub fn alpha_armor_max_damage(item_id: i32) -> i32 {
    match item_id {
        // Leather: helmet, chest, legs, boots
        298 => 33,
        299 => 48,
        300 => 45,
        301 => 39,
        // Chain
        302 => 66,
        303 => 96,
        304 => 90,
        305 => 78,
        // Iron
        306 => 132,
        307 => 192,
        308 => 180,
        309 => 156,
        // Diamond
        310 => 264,
        311 => 384,
        312 => 360,
        313 => 312,
        // Gold
        314 => 66,
        315 => 96,
        316 => 90,
        317 => 78,
        _ => 0,
    }
}

/// Helper: returns base defense points for an armor item ID.
pub fn alpha_armor_base_points(item_id: i32) -> i32 {
    match item_id {
        298 | 302 | 306 | 310 | 314 => 3, // Helmets
        299 | 303 | 307 | 311 | 315 => 8, // Chestplates
        300 | 304 | 308 | 312 | 316 => 6, // Leggings
        301 | 305 | 309 | 313 | 317 => 3, // Boots
        _ => 0,
    }
}

/// Adds items to an array of inventory slots (e.g. 36 slots).
/// Updates `slots` in place and decreases `stack.stack_size`.
/// Returns remaining `stack_size` (0 if completely added).
#[no_mangle]
pub unsafe extern "C" fn alpha_inventory_add_item(
    slots: *mut FfiItemStack,
    slots_len: usize,
    stack: *mut FfiItemStack,
    stack_limit: i32,
) -> i32 {
    if slots.is_null() || stack.is_null() || slots_len == 0 {
        return if stack.is_null() { 0 } else { (*stack).stack_size };
    }

    let incoming = &mut *stack;
    if incoming.stack_size <= 0 || incoming.item_id <= 0 {
        return 0;
    }

    let slots_slice = std::slice::from_raw_parts_mut(slots, slots_len);
    let max_stack = alpha_inventory_max_stack_size(incoming.item_id);
    let stackable = max_stack > 1;

    let eff_limit = if stack_limit > 0 { stack_limit } else { 64 };

    if stackable {
        while incoming.stack_size > 0 {
            // Find first partial matching slot
            let mut target_slot: Option<usize> = None;
            for (i, slot) in slots_slice.iter().enumerate() {
                if slot.item_id == incoming.item_id
                    && slot.item_damage == incoming.item_damage
                    && slot.stack_size < max_stack
                    && slot.stack_size < eff_limit
                {
                    target_slot = Some(i);
                    break;
                }
            }

            // If no matching slot found, find first empty slot
            if target_slot.is_none() {
                for (i, slot) in slots_slice.iter().enumerate() {
                    if slot.item_id <= 0 || slot.stack_size <= 0 {
                        target_slot = Some(i);
                        break;
                    }
                }
            }

            let Some(slot_idx) = target_slot else {
                break;
            };

            let slot = &mut slots_slice[slot_idx];
            if slot.item_id <= 0 || slot.stack_size <= 0 {
                slot.item_id = incoming.item_id;
                slot.item_damage = incoming.item_damage;
                slot.stack_size = 0;
            }

            let limit = std::cmp::min(max_stack, eff_limit);
            let free_space = limit - slot.stack_size;
            let amount_to_add = std::cmp::min(incoming.stack_size, free_space);

            if amount_to_add > 0 {
                incoming.stack_size -= amount_to_add;
                slot.stack_size += amount_to_add;
                slot.animations_to_go = 5;
            } else {
                break;
            }
        }
    } else {
        // Non-stackable: place 1 into each empty slot
        while incoming.stack_size > 0 {
            let mut target_slot: Option<usize> = None;
            for (i, slot) in slots_slice.iter().enumerate() {
                if slot.item_id <= 0 || slot.stack_size <= 0 {
                    target_slot = Some(i);
                    break;
                }
            }

            let Some(slot_idx) = target_slot else {
                break;
            };

            let slot = &mut slots_slice[slot_idx];
            slot.item_id = incoming.item_id;
            slot.item_damage = incoming.item_damage;
            slot.stack_size = 1;
            slot.animations_to_go = 5;
            incoming.stack_size -= 1;
        }
    }

    incoming.stack_size
}

/// Calculates total armor defense value from worn armor pieces (0-20 points).
/// Uses Alpha 1.2.6 durability-weighted reduction formula:
/// `(total_reduction - 1) * remaining_durability / total_durability + 1`
#[no_mangle]
pub unsafe extern "C" fn alpha_inventory_calc_armor(
    armor_slots: *const FfiItemStack,
    armor_len: usize,
) -> i32 {
    if armor_slots.is_null() || armor_len == 0 {
        return 0;
    }

    let armor = std::slice::from_raw_parts(armor_slots, armor_len);
    let mut total_reduction = 0;
    let mut total_durability = 0;
    let mut remaining_durability = 0;

    for item in armor {
        if item.item_id <= 0 || item.stack_size <= 0 {
            continue;
        }

        let base_points = alpha_armor_base_points(item.item_id);
        if base_points <= 0 {
            continue;
        }

        let max_damage = alpha_armor_max_damage(item.item_id);
        if max_damage <= 0 {
            continue;
        }

        total_reduction += base_points;
        total_durability += max_damage;
        let rem = (max_damage - item.item_damage).max(0);
        remaining_durability += rem;
    }

    if total_durability == 0 {
        return 0;
    }

    (total_reduction - 1) * remaining_durability / total_durability + 1
}

/// Damages all worn armor pieces by `damage_amount`. Breaks items when durability is depleted.
#[no_mangle]
pub unsafe extern "C" fn alpha_inventory_damage_armor(
    armor_slots: *mut FfiItemStack,
    armor_len: usize,
    damage_amount: i32,
) {
    if armor_slots.is_null() || armor_len == 0 || damage_amount <= 0 {
        return;
    }

    let armor = std::slice::from_raw_parts_mut(armor_slots, armor_len);
    for item in armor.iter_mut() {
        if item.item_id <= 0 || item.stack_size <= 0 {
            continue;
        }

        let max_damage = alpha_armor_max_damage(item.item_id);
        if max_damage <= 0 {
            continue;
        }

        item.item_damage += damage_amount;
        if item.item_damage > max_damage {
            item.stack_size -= 1;
            if item.stack_size <= 0 {
                item.item_id = 0;
                item.stack_size = 0;
                item.item_damage = 0;
                item.animations_to_go = 0;
            } else {
                item.item_damage = 0;
            }
        }
    }
}

/// Resolves crafting recipes in the player's 2x2 crafting grid.
/// `grid` contains 4 slots: [0]=(0,0), [1]=(1,0), [2]=(0,1), [3]=(1,1).
/// Returns `FfiItemStack` containing crafted output or empty item (item_id=0, stack_size=0).
#[no_mangle]
pub unsafe extern "C" fn alpha_inventory_craft_2x2(grid: *const FfiItemStack) -> FfiItemStack {
    let empty = FfiItemStack {
        item_id: 0,
        stack_size: 0,
        item_damage: 0,
        animations_to_go: 0,
    };

    if grid.is_null() {
        return empty;
    }

    let g = std::slice::from_raw_parts(grid, 4);

    let id = |idx: usize| -> i32 {
        if g[idx].item_id > 0 && g[idx].stack_size > 0 {
            g[idx].item_id
        } else {
            0
        }
    };

    let count_items = (0..4).filter(|&i| id(i) != 0).count();

    // 1-item recipes
    if count_items == 1 {
        // Wood (17) -> 4 Planks (5)
        for i in 0..4 {
            if id(i) == 17 {
                return FfiItemStack {
                    item_id: 5,
                    stack_size: 4,
                    item_damage: 0,
                    animations_to_go: 0,
                };
            }
        }
    }

    // 2-item recipes
    if count_items == 2 {
        // Vertical planks: sticks (id 280, count 4)
        // Col 0: (0, 2)
        if (id(0) == 5 && id(2) == 5 && id(1) == 0 && id(3) == 0)
            || (id(1) == 5 && id(3) == 5 && id(0) == 0 && id(2) == 0)
        {
            return FfiItemStack {
                item_id: 280,
                stack_size: 4,
                item_damage: 0,
                animations_to_go: 0,
            };
        }

        // Coal over stick: 4 torches (id 50)
        if (id(0) == 263 && id(2) == 280 && id(1) == 0 && id(3) == 0)
            || (id(1) == 263 && id(3) == 280 && id(0) == 0 && id(2) == 0)
        {
            return FfiItemStack {
                item_id: 50,
                stack_size: 4,
                item_damage: 0,
                animations_to_go: 0,
            };
        }

        // Redstone over stick: 1 redstone torch (id 76)
        if (id(0) == 331 && id(2) == 280 && id(1) == 0 && id(3) == 0)
            || (id(1) == 331 && id(3) == 280 && id(0) == 0 && id(2) == 0)
        {
            return FfiItemStack {
                item_id: 76,
                stack_size: 1,
                item_damage: 0,
                animations_to_go: 0,
            };
        }

        // Lever: stick (280) over cobblestone (4)
        if (id(0) == 280 && id(2) == 4 && id(1) == 0 && id(3) == 0)
            || (id(1) == 280 && id(3) == 4 && id(0) == 0 && id(2) == 0)
        {
            return FfiItemStack {
                item_id: 69,
                stack_size: 1,
                item_damage: 0,
                animations_to_go: 0,
            };
        }

        // Flint & Steel: iron ingot (265) + flint (318) shapeless
        let has_iron = (0..4).any(|i| id(i) == 265);
        let has_flint = (0..4).any(|i| id(i) == 318);
        if has_iron && has_flint {
            return FfiItemStack {
                item_id: 259,
                stack_size: 1,
                item_damage: 0,
                animations_to_go: 0,
            };
        }

        // Wooden pressure plate: 2 planks horizontal
        if (id(0) == 5 && id(1) == 5 && id(2) == 0 && id(3) == 0)
            || (id(2) == 5 && id(3) == 5 && id(0) == 0 && id(1) == 0)
        {
            return FfiItemStack {
                item_id: 72,
                stack_size: 1,
                item_damage: 0,
                animations_to_go: 0,
            };
        }

        // Stone pressure plate: 2 stone (1) horizontal
        if (id(0) == 1 && id(1) == 1 && id(2) == 0 && id(3) == 0)
            || (id(2) == 1 && id(3) == 1 && id(0) == 0 && id(1) == 0)
        {
            return FfiItemStack {
                item_id: 70,
                stack_size: 1,
                item_damage: 0,
                animations_to_go: 0,
            };
        }

        // Stone button: 2 stone (1) vertical
        if (id(0) == 1 && id(2) == 1 && id(1) == 0 && id(3) == 0)
            || (id(1) == 1 && id(3) == 1 && id(0) == 0 && id(2) == 0)
        {
            return FfiItemStack {
                item_id: 77,
                stack_size: 1,
                item_damage: 0,
                animations_to_go: 0,
            };
        }

        // Jack-o'-lantern: pumpkin (86) over torch (50)
        if (id(0) == 86 && id(2) == 50 && id(1) == 0 && id(3) == 0)
            || (id(1) == 86 && id(3) == 50 && id(0) == 0 && id(2) == 0)
        {
            return FfiItemStack {
                item_id: 91,
                stack_size: 1,
                item_damage: 0,
                animations_to_go: 0,
            };
        }
    }

    // 4-item recipes
    if count_items == 4 {
        // 4 Planks -> 1 Workbench (58)
        if id(0) == 5 && id(1) == 5 && id(2) == 5 && id(3) == 5 {
            return FfiItemStack {
                item_id: 58,
                stack_size: 1,
                item_damage: 0,
                animations_to_go: 0,
            };
        }

        // 4 Snowballs (332) -> 1 Snow block (80)
        if id(0) == 332 && id(1) == 332 && id(2) == 332 && id(3) == 332 {
            return FfiItemStack {
                item_id: 80,
                stack_size: 1,
                item_damage: 0,
                animations_to_go: 0,
            };
        }

        // 4 Clay (337) -> 1 Clay block (82)
        if id(0) == 337 && id(1) == 337 && id(2) == 337 && id(3) == 337 {
            return FfiItemStack {
                item_id: 82,
                stack_size: 1,
                item_damage: 0,
                animations_to_go: 0,
            };
        }

        // 4 Bricks (336) -> 1 Brick block (45)
        if id(0) == 336 && id(1) == 336 && id(2) == 336 && id(3) == 336 {
            return FfiItemStack {
                item_id: 45,
                stack_size: 1,
                item_damage: 0,
                animations_to_go: 0,
            };
        }
    }

    empty
}

/// Decreases stack sizes of crafting grid items by 1 after a successful craft.
#[no_mangle]
pub unsafe extern "C" fn alpha_inventory_consume_craft_2x2(grid: *mut FfiItemStack) {
    if grid.is_null() {
        return;
    }

    let g = std::slice::from_raw_parts_mut(grid, 4);
    for item in g.iter_mut() {
        if item.item_id > 0 && item.stack_size > 0 {
            item.stack_size -= 1;
            if item.stack_size <= 0 {
                item.item_id = 0;
                item.stack_size = 0;
                item.item_damage = 0;
                item.animations_to_go = 0;
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_inventory_add_item_stackable() {
        let mut slots = vec![
            FfiItemStack { item_id: 0, stack_size: 0, item_damage: 0, animations_to_go: 0 };
            36
        ];
        slots[0] = FfiItemStack { item_id: 1, stack_size: 50, item_damage: 0, animations_to_go: 0 };

        let mut incoming = FfiItemStack { item_id: 1, stack_size: 20, item_damage: 0, animations_to_go: 0 };
        let rem = unsafe { alpha_inventory_add_item(slots.as_mut_ptr(), slots.len(), &mut incoming, 64) };

        assert_eq!(rem, 0);
        assert_eq!(incoming.stack_size, 0);
        assert_eq!(slots[0].stack_size, 64);
        assert_eq!(slots[1].item_id, 1);
        assert_eq!(slots[1].stack_size, 6);
    }

    #[test]
    fn test_inventory_add_item_non_stackable() {
        let mut slots = vec![
            FfiItemStack { item_id: 0, stack_size: 0, item_damage: 0, animations_to_go: 0 };
            36
        ];

        let mut incoming = FfiItemStack { item_id: 276, stack_size: 2, item_damage: 0, animations_to_go: 0 }; // Diamond sword
        let rem = unsafe { alpha_inventory_add_item(slots.as_mut_ptr(), slots.len(), &mut incoming, 64) };

        assert_eq!(rem, 0);
        assert_eq!(slots[0].item_id, 276);
        assert_eq!(slots[0].stack_size, 1);
        assert_eq!(slots[1].item_id, 276);
        assert_eq!(slots[1].stack_size, 1);
    }

    #[test]
    fn test_armor_points_full_diamond() {
        let armor = vec![
            FfiItemStack { item_id: 310, stack_size: 1, item_damage: 0, animations_to_go: 0 }, // Diamond helmet (3)
            FfiItemStack { item_id: 311, stack_size: 1, item_damage: 0, animations_to_go: 0 }, // Diamond chest (8)
            FfiItemStack { item_id: 312, stack_size: 1, item_damage: 0, animations_to_go: 0 }, // Diamond legs (6)
            FfiItemStack { item_id: 313, stack_size: 1, item_damage: 0, animations_to_go: 0 }, // Diamond boots (3)
        ];
        // Total base reduction: 3 + 8 + 6 + 3 = 20 points
        let pts = unsafe { alpha_inventory_calc_armor(armor.as_ptr(), armor.len()) };
        assert_eq!(pts, 20);
    }

    #[test]
    fn test_armor_degradation_and_breakage() {
        let mut armor = vec![
            FfiItemStack { item_id: 298, stack_size: 1, item_damage: 30, animations_to_go: 0 }, // Leather helmet max 33
        ];

        unsafe { alpha_inventory_damage_armor(armor.as_mut_ptr(), armor.len(), 5) };
        // 30 + 5 = 35 > 33 -> broken!
        assert_eq!(armor[0].stack_size, 0);
        assert_eq!(armor[0].item_id, 0);
    }

    #[test]
    fn test_2x2_crafting_recipes() {
        // Wood to Planks
        let grid = [
            FfiItemStack { item_id: 17, stack_size: 1, item_damage: 0, animations_to_go: 0 },
            FfiItemStack { item_id: 0, stack_size: 0, item_damage: 0, animations_to_go: 0 },
            FfiItemStack { item_id: 0, stack_size: 0, item_damage: 0, animations_to_go: 0 },
            FfiItemStack { item_id: 0, stack_size: 0, item_damage: 0, animations_to_go: 0 },
        ];
        let out = unsafe { alpha_inventory_craft_2x2(grid.as_ptr()) };
        assert_eq!(out.item_id, 5);
        assert_eq!(out.stack_size, 4);

        // 4 Planks to Workbench
        let grid_wb = [
            FfiItemStack { item_id: 5, stack_size: 1, item_damage: 0, animations_to_go: 0 },
            FfiItemStack { item_id: 5, stack_size: 1, item_damage: 0, animations_to_go: 0 },
            FfiItemStack { item_id: 5, stack_size: 1, item_damage: 0, animations_to_go: 0 },
            FfiItemStack { item_id: 5, stack_size: 1, item_damage: 0, animations_to_go: 0 },
        ];
        let out_wb = unsafe { alpha_inventory_craft_2x2(grid_wb.as_ptr()) };
        assert_eq!(out_wb.item_id, 58);
        assert_eq!(out_wb.stack_size, 1);
    }
}
