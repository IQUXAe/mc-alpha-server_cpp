#[derive(Clone, Copy, Debug, PartialEq)]
pub struct CombatResult {
    pub damage_after_armor: i32,
    pub new_armor_damage_carry: i32,
    pub scaled_damage: i32,
}

/// Calculates combat damage scaling by difficulty and armor absorption.
/// Follows Minecraft Alpha 1.2.6 rules (EntityPlayerMP.attackEntityFrom).
pub fn combat_calculate_damage(
    raw_damage: i32,
    attacker_is_player: bool,
    difficulty: i32,
    armor_value: i32,
    armor_damage_carry: i32,
) -> CombatResult {
    if raw_damage <= 0 {
        return CombatResult {
            damage_after_armor: 0,
            new_armor_damage_carry: armor_damage_carry,
            scaled_damage: 0,
        };
    }

    let mut amount = raw_damage;
    if !attacker_is_player {
        if difficulty <= 0 {
            amount = 0;
        } else if difficulty == 1 {
            amount = amount / 3 + 1;
        } else if difficulty >= 3 {
            amount = amount * 3 / 2;
        }
    }

    if amount <= 0 {
        return CombatResult {
            damage_after_armor: 0,
            new_armor_damage_carry: armor_damage_carry,
            scaled_damage: 0,
        };
    }

    let armor = armor_value.clamp(0, 25);
    let scaled = amount * (25 - armor) + armor_damage_carry;
    let damage_after_armor = scaled / 25;
    let new_carry = scaled % 25;

    CombatResult {
        damage_after_armor,
        new_armor_damage_carry: new_carry,
        scaled_damage: amount,
    }
}

/// Returns damage dealt to entities based on held item ID in Minecraft Alpha 1.2.6.
pub fn combat_get_weapon_damage(item_id: i32) -> i32 {
    match item_id {
        // Swords: 4 + level * 2
        // Wood (level 0), Gold (level 0)
        268 | 283 => 4,
        // Stone (level 1)
        272 => 6,
        // Iron (level 2)
        267 => 8,
        // Diamond (level 3)
        276 => 10,

        // Axes: 3 + level
        271 | 286 => 3, // Wood, Gold
        275 => 4,       // Stone
        258 => 5,       // Iron
        279 => 6,       // Diamond

        // Pickaxes: 2 + level
        270 | 285 => 2, // Wood, Gold
        274 => 3,       // Stone
        257 => 4,       // Iron
        278 => 5,       // Diamond

        // Shovels: 1 + level
        269 | 284 => 1, // Wood, Gold
        273 => 2,       // Stone
        256 => 3,       // Iron
        277 => 4,       // Diamond

        // Bare hand or other items
        _ => 1,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_combat_difficulty_scaling() {
        // Peaceful mob attack: 0 damage
        let res_peaceful = combat_calculate_damage(10, false, 0, 0, 0);
        assert_eq!(res_peaceful.scaled_damage, 0);
        assert_eq!(res_peaceful.damage_after_armor, 0);

        // Easy mob attack: 10 / 3 + 1 = 4
        let res_easy = combat_calculate_damage(10, false, 1, 0, 0);
        assert_eq!(res_easy.scaled_damage, 4);
        assert_eq!(res_easy.damage_after_armor, 4);

        // Normal mob attack: 10
        let res_normal = combat_calculate_damage(10, false, 2, 0, 0);
        assert_eq!(res_normal.scaled_damage, 10);
        assert_eq!(res_normal.damage_after_armor, 10);

        // Hard mob attack: 10 * 3 / 2 = 15
        let res_hard = combat_calculate_damage(10, false, 3, 0, 0);
        assert_eq!(res_hard.scaled_damage, 15);
        assert_eq!(res_hard.damage_after_armor, 15);

        // Player attacker ignores difficulty scaling
        let res_pvp = combat_calculate_damage(10, true, 0, 0, 0);
        assert_eq!(res_pvp.scaled_damage, 10);
        assert_eq!(res_pvp.damage_after_armor, 10);
    }

    #[test]
    fn test_combat_armor_absorption() {
        // 20 armor points, 10 damage:
        // scaled = 10 * (25 - 20) + 0 = 50
        // damage_after_armor = 50 / 25 = 2, carry = 0
        let res = combat_calculate_damage(10, false, 2, 20, 0);
        assert_eq!(res.damage_after_armor, 2);
        assert_eq!(res.new_armor_damage_carry, 0);

        // 10 armor points, 3 damage, carry 0:
        // scaled = 3 * (25 - 10) + 0 = 45
        // damage = 45 / 25 = 1, carry = 20
        let res2 = combat_calculate_damage(3, false, 2, 10, 0);
        assert_eq!(res2.damage_after_armor, 1);
        assert_eq!(res2.new_armor_damage_carry, 20);

        // Next hit with carry 20:
        // scaled = 3 * 15 + 20 = 65
        // damage = 65 / 25 = 2, carry = 15
        let res3 = combat_calculate_damage(3, false, 2, 10, res2.new_armor_damage_carry);
        assert_eq!(res3.damage_after_armor, 2);
        assert_eq!(res3.new_armor_damage_carry, 15);
    }

    #[test]
    fn test_weapon_damages() {
        assert_eq!(combat_get_weapon_damage(0), 1);
        assert_eq!(combat_get_weapon_damage(268), 4);  // Wood sword
        assert_eq!(combat_get_weapon_damage(272), 6);  // Stone sword
        assert_eq!(combat_get_weapon_damage(267), 8);  // Iron sword
        assert_eq!(combat_get_weapon_damage(276), 10); // Diamond sword
        assert_eq!(combat_get_weapon_damage(283), 4);  // Gold sword

        assert_eq!(combat_get_weapon_damage(279), 6);  // Diamond axe
        assert_eq!(combat_get_weapon_damage(278), 5);  // Diamond pickaxe
        assert_eq!(combat_get_weapon_damage(277), 4);  // Diamond shovel
    }
}
