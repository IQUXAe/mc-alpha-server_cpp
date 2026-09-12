//! Pure item data ported from `src/core/Item.h`, `src/core/Item.cpp`,
//! `src/core/ItemStack.h`.
//!
//! Scope is strictly world-free data:
//! - numeric ids for all items/tools (`Item::initItems`, `Item.h`),
//! - durability (`maxDamage`),
//! - tool tier / base dig speed / explicit effective-block lists
//!   (`ItemTool`, `ItemPickaxe`, `ItemSpade`, `ItemAxe`, `ItemSword`),
//! - food heal amounts (`ItemFood`, `ItemSoup`),
//! - a small pure stack value (`PureItemStack`) with world-free helpers.
//!
//! Reused, not duplicated:
//! - `ItemStack` comes from `crate::inventory`,
//! - `max_stack_size` comes from
//!   `crate::player::inventory::inventory_max_stack_size`,
//! - armor durability comes from
//!   `crate::player::inventory::armor_max_damage`,
//! - degrade-on-hit logic stays in `crate::inventory::item_stack_damage`,
//! - weapon-vs-entity numbers stay in
//!   `crate::player::combat::combat_get_weapon_damage`,
//! - full `getStrVsBlock` / `canHarvestBlock` with material fallback stay in
//!   `crate::player::mining`.
//!
//! World-needing behaviours (`onItemUse`, `onItemRightClick`, boat spawn,
//! hoe/seed/flint/sign/block placement, soup/food healing via player,
//! `hitEntity` via entity) are NOT ported here.

use crate::inventory::ItemStack;
use crate::player::inventory::{armor_max_damage, inventory_max_stack_size};

// ---------------------------------------------------------------------------
// Id table (final `itemID` values, see `Item::initItems` in Item.cpp)
// ---------------------------------------------------------------------------

pub const MAX_ITEMS: i32 = 32000;
pub const FIRST_ITEM_ID: i32 = 256;
pub const LAST_ITEM_ID: i32 = 350;
pub const BLOCK_MAX_ID: i32 = 255;

pub const ITEM_SHOVEL_STEEL: i32 = 256;
pub const ITEM_PICKAXE_STEEL: i32 = 257;
pub const ITEM_AXE_STEEL: i32 = 258;
pub const ITEM_FLINT_AND_STEEL: i32 = 259;
pub const ITEM_APPLE_RED: i32 = 260;
pub const ITEM_BOW: i32 = 261;
pub const ITEM_ARROW: i32 = 262;
pub const ITEM_COAL: i32 = 263;
pub const ITEM_DIAMOND: i32 = 264;
pub const ITEM_INGOT_IRON: i32 = 265;
pub const ITEM_INGOT_GOLD: i32 = 266;
pub const ITEM_SWORD_STEEL: i32 = 267;
pub const ITEM_SWORD_WOOD: i32 = 268;
pub const ITEM_SHOVEL_WOOD: i32 = 269;
pub const ITEM_PICKAXE_WOOD: i32 = 270;
pub const ITEM_AXE_WOOD: i32 = 271;
pub const ITEM_SWORD_STONE: i32 = 272;
pub const ITEM_SHOVEL_STONE: i32 = 273;
pub const ITEM_PICKAXE_STONE: i32 = 274;
pub const ITEM_AXE_STONE: i32 = 275;
pub const ITEM_SWORD_DIAMOND: i32 = 276;
pub const ITEM_SHOVEL_DIAMOND: i32 = 277;
pub const ITEM_PICKAXE_DIAMOND: i32 = 278;
pub const ITEM_AXE_DIAMOND: i32 = 279;
pub const ITEM_STICK: i32 = 280;
pub const ITEM_BOWL_EMPTY: i32 = 281;
pub const ITEM_BOWL_SOUP: i32 = 282;
pub const ITEM_SWORD_GOLD: i32 = 283;
pub const ITEM_SHOVEL_GOLD: i32 = 284;
pub const ITEM_PICKAXE_GOLD: i32 = 285;
pub const ITEM_AXE_GOLD: i32 = 286;
pub const ITEM_SILK: i32 = 287;
pub const ITEM_FEATHER: i32 = 288;
pub const ITEM_GUNPOWDER: i32 = 289;
pub const ITEM_HOE_WOOD: i32 = 290;
pub const ITEM_HOE_STONE: i32 = 291;
pub const ITEM_HOE_STEEL: i32 = 292;
pub const ITEM_HOE_DIAMOND: i32 = 293;
pub const ITEM_HOE_GOLD: i32 = 294;
pub const ITEM_SEEDS: i32 = 295;
pub const ITEM_WHEAT: i32 = 296;
pub const ITEM_BREAD: i32 = 297;
pub const ITEM_HELMET_LEATHER: i32 = 298;
pub const ITEM_PLATE_LEATHER: i32 = 299;
pub const ITEM_LEGS_LEATHER: i32 = 300;
pub const ITEM_BOOTS_LEATHER: i32 = 301;
pub const ITEM_HELMET_CHAIN: i32 = 302;
pub const ITEM_PLATE_CHAIN: i32 = 303;
pub const ITEM_LEGS_CHAIN: i32 = 304;
pub const ITEM_BOOTS_CHAIN: i32 = 305;
pub const ITEM_HELMET_STEEL: i32 = 306;
pub const ITEM_PLATE_STEEL: i32 = 307;
pub const ITEM_LEGS_STEEL: i32 = 308;
pub const ITEM_BOOTS_STEEL: i32 = 309;
pub const ITEM_HELMET_DIAMOND: i32 = 310;
pub const ITEM_PLATE_DIAMOND: i32 = 311;
pub const ITEM_LEGS_DIAMOND: i32 = 312;
pub const ITEM_BOOTS_DIAMOND: i32 = 313;
pub const ITEM_HELMET_GOLD: i32 = 314;
pub const ITEM_PLATE_GOLD: i32 = 315;
pub const ITEM_LEGS_GOLD: i32 = 316;
pub const ITEM_BOOTS_GOLD: i32 = 317;
pub const ITEM_FLINT: i32 = 318;
pub const ITEM_PORK_RAW: i32 = 319;
pub const ITEM_PORK_COOKED: i32 = 320;
pub const ITEM_PAINTING: i32 = 321;
pub const ITEM_APPLE_GOLD: i32 = 322;
pub const ITEM_SIGN: i32 = 323;
pub const ITEM_DOOR_WOOD: i32 = 324;
pub const ITEM_BUCKET_EMPTY: i32 = 325;
pub const ITEM_BUCKET_WATER: i32 = 326;
pub const ITEM_BUCKET_LAVA: i32 = 327;
pub const ITEM_MINECART_EMPTY: i32 = 328;
pub const ITEM_SADDLE: i32 = 329;
pub const ITEM_DOOR_STEEL: i32 = 330;
pub const ITEM_REDSTONE: i32 = 331;
pub const ITEM_SNOWBALL: i32 = 332;
pub const ITEM_BOAT: i32 = 333;
pub const ITEM_LEATHER: i32 = 334;
pub const ITEM_BUCKET_MILK: i32 = 335;
pub const ITEM_BRICK: i32 = 336;
pub const ITEM_CLAY: i32 = 337;
pub const ITEM_REED: i32 = 338;
pub const ITEM_PAPER: i32 = 339;
pub const ITEM_BOOK: i32 = 340;
pub const ITEM_SLIME_BALL: i32 = 341;
pub const ITEM_MINECART_CRATE: i32 = 342;
pub const ITEM_MINECART_POWERED: i32 = 343;
pub const ITEM_EGG: i32 = 344;
pub const ITEM_COMPASS: i32 = 345;
pub const ITEM_FISHING_ROD: i32 = 346;
pub const ITEM_POCKET_SUNDIAL: i32 = 347;
pub const ITEM_LIGHTSTONE_DUST: i32 = 348;
pub const ITEM_FISH_RAW: i32 = 349;
pub const ITEM_FISH_COOKED: i32 = 350;

/// True for ids that can appear in `Item::itemsList` after `initItems`:
/// block ids 1..=255 plus item ids 256..=350.
pub fn item_is_valid(item_id: i32) -> bool {
    item_id >= 1 && item_id <= LAST_ITEM_ID
}

// ---------------------------------------------------------------------------
// Durability (`maxDamage`)
// ---------------------------------------------------------------------------
//
// C++ sources:
// - `ItemTool` ctor (Java `ItemTool.java`): `maxDamage = 32 << level`,
//   `if (level == 3) maxDamage *= 4`.
//   Gold tools are built with `level == 0`, so they share wood values.
// - `ItemSword` ctor: same formula.
// - `ItemHoe` ctor (Java `ItemHoe.java:4-8`): `maxDamage = 32 << level`
//   with NO x4 for diamond: wood 32, stone/gold 64, steel 128,
//   diamond 256. (The old 59/131/250/1561 table was Beta values.)
// - `flintAndSteel` 64, `bow` 32 (default Item.maxDamage, only stackSize=1),
//   `fishingRod` 64, saddle/sign/door/bucket/painting 64.
// - armor via `setMaxDamage`, reused through `armor_max_damage`.
// - everything else keeps the `Item` default `maxDamage = 32`, but for
//   non-damageable stacks the damage field is unused (vanilla still stores
//   32 as the cap).

/// Unified durability table. Armor range 298..=317 is served by the
/// existing `armor_max_damage` helper (no second table here).
pub fn item_max_damage(item_id: i32) -> i32 {
    match item_id {
        ITEM_SHOVEL_WOOD | ITEM_PICKAXE_WOOD | ITEM_AXE_WOOD | ITEM_SWORD_WOOD
        | ITEM_SHOVEL_GOLD | ITEM_PICKAXE_GOLD | ITEM_AXE_GOLD | ITEM_SWORD_GOLD => 32,
        ITEM_SHOVEL_STONE | ITEM_PICKAXE_STONE | ITEM_AXE_STONE | ITEM_SWORD_STONE => 64,
        ITEM_SHOVEL_STEEL | ITEM_PICKAXE_STEEL | ITEM_AXE_STEEL | ITEM_SWORD_STEEL => 128,
        ITEM_SHOVEL_DIAMOND | ITEM_PICKAXE_DIAMOND | ITEM_AXE_DIAMOND | ITEM_SWORD_DIAMOND => 1024,
        ITEM_HOE_WOOD => 32,
        ITEM_HOE_STONE => 64,
        ITEM_HOE_STEEL => 128,
        ITEM_HOE_DIAMOND => 256,
        ITEM_HOE_GOLD => 64,
        ITEM_FLINT_AND_STEEL => 64,
        ITEM_BOW => 32,
        ITEM_FISHING_ROD => 64,
        ITEM_SADDLE | ITEM_SIGN | ITEM_DOOR_WOOD | ITEM_DOOR_STEEL | ITEM_BUCKET_EMPTY
        | ITEM_BUCKET_WATER | ITEM_BUCKET_LAVA | ITEM_BUCKET_MILK | ITEM_PAINTING
        | ITEM_MINECART_EMPTY | ITEM_MINECART_CRATE | ITEM_MINECART_POWERED | ITEM_BOAT => 64,
        298..=317 => armor_max_damage(item_id),
        _ => 0,
    }
}

// ---------------------------------------------------------------------------
// Food (`ItemFood` / `ItemSoup` heal amounts, `Item.cpp:239/261/276/298-301/328-329`)
// ---------------------------------------------------------------------------
// No string names exist in `Item.h` / `Item.cpp`, so no name table is ported.

/// Heal amount for food items, 0 for non-food.
/// Values: apple 4, soup 10, bread 5, pork raw 3, pork cooked 8,
/// golden apple 42, fish raw 2, fish cooked 5.
pub fn item_food_heal(item_id: i32) -> i32 {
    match item_id {
        ITEM_APPLE_RED => 4,
        ITEM_BOWL_SOUP => 10,
        ITEM_BREAD => 5,
        ITEM_PORK_RAW => 3,
        ITEM_PORK_COOKED => 8,
        ITEM_APPLE_GOLD => 42,
        ITEM_FISH_RAW => 2,
        ITEM_FISH_COOKED => 5,
        _ => 0,
    }
}

/// True for the eight `ItemFood` / `ItemSoup` ids listed above.
pub fn item_is_food(item_id: i32) -> bool {
    item_food_heal(item_id) > 0
}

// ---------------------------------------------------------------------------
// Tool data (`ItemTool` / `ItemSword` in `Item.h`)
// ---------------------------------------------------------------------------

/// Tool family.
#[repr(i32)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ItemToolKind {
    Other = 0,
    Pickaxe = 1,
    Spade = 2,
    Axe = 3,
    Sword = 4,
    Hoe = 5,
}

/// Family lookup. Hoes (290..=294) report `Hoe`; everything that is not a
/// pick/spade/axe/sword/hoe reports `Other`.
pub fn item_tool_kind(item_id: i32) -> i32 {
    match item_id {
        ITEM_PICKAXE_STEEL | ITEM_PICKAXE_WOOD | ITEM_PICKAXE_STONE | ITEM_PICKAXE_DIAMOND
        | ITEM_PICKAXE_GOLD => ItemToolKind::Pickaxe as i32,
        ITEM_SHOVEL_STEEL | ITEM_SHOVEL_WOOD | ITEM_SHOVEL_STONE | ITEM_SHOVEL_DIAMOND
        | ITEM_SHOVEL_GOLD => ItemToolKind::Spade as i32,
        ITEM_AXE_STEEL | ITEM_AXE_WOOD | ITEM_AXE_STONE | ITEM_AXE_DIAMOND | ITEM_AXE_GOLD => {
            ItemToolKind::Axe as i32
        }
        ITEM_SWORD_STEEL | ITEM_SWORD_WOOD | ITEM_SWORD_STONE | ITEM_SWORD_DIAMOND
        | ITEM_SWORD_GOLD => ItemToolKind::Sword as i32,
        ITEM_HOE_WOOD | ITEM_HOE_STONE | ITEM_HOE_STEEL | ITEM_HOE_DIAMOND | ITEM_HOE_GOLD => {
            ItemToolKind::Hoe as i32
        }
        _ => ItemToolKind::Other as i32,
    }
}

fn tool_tier_opt(item_id: i32) -> Option<i32> {
    match item_id {
        ITEM_SWORD_WOOD | ITEM_SHOVEL_WOOD | ITEM_PICKAXE_WOOD | ITEM_AXE_WOOD
        | ITEM_SWORD_GOLD | ITEM_SHOVEL_GOLD | ITEM_PICKAXE_GOLD | ITEM_AXE_GOLD => Some(0),
        ITEM_SWORD_STONE | ITEM_SHOVEL_STONE | ITEM_PICKAXE_STONE | ITEM_AXE_STONE => Some(1),
        ITEM_SWORD_STEEL | ITEM_SHOVEL_STEEL | ITEM_PICKAXE_STEEL | ITEM_AXE_STEEL => Some(2),
        ITEM_SWORD_DIAMOND | ITEM_SHOVEL_DIAMOND | ITEM_PICKAXE_DIAMOND | ITEM_AXE_DIAMOND => {
            Some(3)
        }
        _ => None,
    }
}

/// Harvest level from `Item.h` (`0=wood, 1=stone, 2=steel, 3=diamond`).
/// Gold tools were built with level 0 in `Item.cpp`, so they yield 0.
/// Returns -1 for non-tiered ids (hoes, armor, misc).
pub fn item_tool_tier(item_id: i32) -> i32 {
    match tool_tier_opt(item_id) {
        Some(t) => t,
        None => -1,
    }
}

/// Base dig speed from `ItemTool` ctor: `(level + 1) * 2.0`.
/// Wood/gold 2.0, stone 4.0, steel 6.0, diamond 8.0.
/// Returns 1.0 for ids without a tier (matches the `getStrVsBlock`
/// fallback in `Item.cpp:459-467`).
pub fn item_tool_speed(item_id: i32) -> f32 {
    match tool_tier_opt(item_id) {
        Some(0) => 2.0,
        Some(1) => 4.0,
        Some(2) => 6.0,
        Some(_) => 8.0,
        None => 1.0,
    }
}

/// Explicit effective-block lists from `ItemPickaxe` / `ItemSpade` /
/// `ItemAxe` ctors in Java (`ItemPickaxe.java`, `ItemSpade.java:4`,
/// `ItemAxe.java:4`). Pick: [4,43,44,1,48,15,42,16,41,14,56,57,79,87].
pub const PICKAXE_EFFECTIVE_BLOCKS: [i32; 14] =
    [4, 43, 44, 1, 48, 15, 42, 16, 41, 14, 56, 57, 79, 87];
pub const SPADE_EFFECTIVE_BLOCKS: [i32; 7] = [2, 3, 12, 13, 78, 80, 82];
pub const AXE_EFFECTIVE_BLOCKS: [i32; 4] = [5, 47, 17, 54];

fn list_has(hay: &[i32], needle: i32) -> bool {
    for x in hay.iter() {
        if *x == needle {
            return true;
        }
    }
    false
}

/// True when `block_id` is in the explicit ctor list for the tool family.
/// This is only the first half of `ItemTool::getStrVsBlock`; the
/// material-based fallback lives in `player_mining` and is reused there.
pub fn item_is_effective_explicit(item_id: i32, block_id: i32) -> bool {
    if block_id <= 0 || block_id >= 256 {
        return false;
    }
    match item_tool_kind(item_id) {
        x if x == ItemToolKind::Pickaxe as i32 => list_has(&PICKAXE_EFFECTIVE_BLOCKS, block_id),
        x if x == ItemToolKind::Spade as i32 => list_has(&SPADE_EFFECTIVE_BLOCKS, block_id),
        x if x == ItemToolKind::Axe as i32 => list_has(&AXE_EFFECTIVE_BLOCKS, block_id),
        _ => false,
    }
}

// ---------------------------------------------------------------------------
// Pure stack value (`ItemStack` fields without world access)
// ---------------------------------------------------------------------------
//
// Mirrors the data layout of `ItemStack` (`ItemStack.h:14-17`):
// `stackSize / animationsToGo / itemID / itemDamage`.
// `animationsToGo` is a client render timer and is not modelled here;
// `ItemStack.animations_to_go` is the client render timer, unused here.
//
// C++ has no split/merge/can-stack helpers on `ItemStack` itself, so the
// helpers below are new pure utilities built on the reused
// `inventory_max_stack_size` table (same match rule as
// `inventory_add_item`: same id + same damage + room left).

/// World-free copy of `ItemStack` fields.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct PureItemStack {
    pub item_id: i32,
    pub count: i32,
    pub damage: i32,
}

impl PureItemStack {
    pub const fn new(item_id: i32, count: i32, damage: i32) -> Self {
        Self {
            item_id,
            count,
            damage,
        }
    }

    pub const fn empty() -> Self {
        Self {
            item_id: 0,
            count: 0,
            damage: 0,
        }
    }

    pub const fn is_empty(self) -> bool {
        self.item_id <= 0 || self.count <= 0
    }

    /// Mirrors `ItemStack::copy()` (`ItemStack.h:120-122`).
    pub const fn copy(self) -> Self {
        Self {
            item_id: self.item_id,
            count: self.count,
            damage: self.damage,
        }
    }

    pub fn clear(&mut self) {
        self.item_id = 0;
        self.count = 0;
        self.damage = 0;
    }

    pub fn from_stack(s: &ItemStack) -> Self {
        Self {
            item_id: s.item_id,
            count: s.stack_size,
            damage: s.item_damage,
        }
    }

    pub fn to_stack(self) -> ItemStack {
        ItemStack::new(self.item_id, self.count, self.damage)
    }

    fn max_for(self) -> i32 {
        inventory_max_stack_size(self.item_id)
    }

    /// Stacking rule used by inventory filling: same item, same damage,
    /// both non-empty, and the shared limit is above 1.
    pub fn can_stack_with(self, other: Self) -> bool {
        if self.is_empty() || other.is_empty() {
            return false;
        }
        if self.item_id != other.item_id {
            return false;
        }
        if self.damage != other.damage {
            return false;
        }
        self.max_for() > 1 && other.max_for() > 1
    }

    /// Alias kept for the `can_merge` name used in the task brief.
    pub fn can_merge_with(self, other: Self) -> bool {
        self.can_stack_with(other)
    }

    /// Take up to `amount` units out of `self`, shrinking `self`.
    /// Non-positive `amount`, empty `self`, or invalid ids yield an empty
    /// stack and leave `self` untouched.
    pub fn split(&mut self, amount: i32) -> Self {
        if amount <= 0 || self.is_empty() {
            return Self::empty();
        }
        let mut take = amount;
        if take > self.count {
            take = self.count;
        }
        self.count -= take;
        let out = Self {
            item_id: self.item_id,
            count: take,
            damage: self.damage,
        };
        if self.count <= 0 {
            self.clear();
        }
        out
    }

    /// Move units from `other` into `self` while ids/damage match and
    /// room is left under the shared stack limit. Leftover stays in `other`.
    pub fn merge_from(&mut self, other: &mut Self) {
        if other.is_empty() {
            return;
        }
        if self.is_empty() {
            // Empty slot adopts the incoming kind, capped by the limit.
            let limit = other.max_for();
            if limit <= 0 {
                return;
            }
            let mut take = other.count;
            if take > limit {
                take = limit;
            }
            self.item_id = other.item_id;
            self.damage = other.damage;
            self.count = take;
            other.count -= take;
            if other.count <= 0 {
                other.clear();
            }
            return;
        }
        if !self.can_stack_with(*other) {
            return;
        }
        let limit = self.max_for();
        let other_limit = other.max_for();
        let mut cap = limit;
        if other_limit < cap {
            cap = other_limit;
        }
        if cap <= 0 {
            return;
        }
        let mut room = cap - self.count;
        if room <= 0 {
            return;
        }
        if other.count < room {
            room = other.count;
        }
        self.count += room;
        other.count -= room;
        if other.count <= 0 {
            other.clear();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::inventory::item_stack_damage;
    use crate::player::mining::{mining_can_harvest, mining_get_str_vs_block};

    fn pure(id: i32, count: i32, damage: i32) -> PureItemStack {
        PureItemStack::new(id, count, damage)
    }

    #[test]
    fn default_constructor_is_empty() {
        let s = PureItemStack::empty();
        assert_eq!(s.count, 0);
        assert_eq!(s.item_id, 0);
        assert!(s.is_empty());
    }

    #[test]
    fn construct_from_block_ids() {
        // Mirrors TestItemStack.ConstructFromBlock (stone id 1, count 4).
        let s = pure(1, 4, 0);
        assert_eq!(s.item_id, 1);
        assert_eq!(s.count, 4);
    }

    #[test]
    fn construct_from_item_diamond() {
        // Mirrors ConstructFromItem (diamond id 264, count 2).
        let s = pure(ITEM_DIAMOND, 2, 0);
        assert_eq!(s.item_id, 264);
        assert_eq!(s.count, 2);
    }

    #[test]
    fn construct_from_id_and_damage() {
        // Mirrors ConstructFromId / ConstructFromIdDamage.
        let a = pure(264, 3, 0);
        assert_eq!(a.item_id, 264);
        assert_eq!(a.count, 3);
        let b = pure(264, 1, 5);
        assert_eq!(b.damage, 5);
    }

    #[test]
    fn valid_and_invalid_ids() {
        // Mirrors GetItem / GetItemNullptrForInvalidId.
        assert!(item_is_valid(ITEM_DIAMOND));
        assert!(item_is_valid(1));
        assert!(item_is_valid(ITEM_FISH_COOKED));
        assert!(!item_is_valid(0));
        assert!(!item_is_valid(9999));
        assert!(!item_is_valid(-1));
        assert!(!item_is_valid(351));
    }

    #[test]
    fn id_constants_match_init_items() {
        assert_eq!(ITEM_SHOVEL_STEEL, 256);
        assert_eq!(ITEM_PICKAXE_STEEL, 257);
        assert_eq!(ITEM_AXE_STEEL, 258);
        assert_eq!(ITEM_FLINT_AND_STEEL, 259);
        assert_eq!(ITEM_APPLE_RED, 260);
        assert_eq!(ITEM_DIAMOND, 264);
        assert_eq!(ITEM_SWORD_STEEL, 267);
        assert_eq!(ITEM_SWORD_DIAMOND, 276);
        assert_eq!(ITEM_STICK, 280);
        assert_eq!(ITEM_BOWL_SOUP, 282);
        assert_eq!(ITEM_HOE_WOOD, 290);
        assert_eq!(ITEM_SEEDS, 295);
        assert_eq!(ITEM_BREAD, 297);
        assert_eq!(ITEM_HELMET_LEATHER, 298);
        assert_eq!(ITEM_BOOTS_GOLD, 317);
        assert_eq!(ITEM_SIGN, 323);
        assert_eq!(ITEM_BUCKET_EMPTY, 325);
        assert_eq!(ITEM_SNOWBALL, 332);
        assert_eq!(ITEM_BOAT, 333);
        assert_eq!(ITEM_EGG, 344);
        assert_eq!(ITEM_FISHING_ROD, 346);
        assert_eq!(ITEM_FISH_COOKED, 350);
        assert_eq!(FIRST_ITEM_ID, 256);
        assert_eq!(LAST_ITEM_ID, 350);
    }

    #[test]
    fn copy_preserves_fields() {
        // Mirrors TestItemStack.Copy + NBTRoundTrip field mapping.
        let s = pure(264, 5, 2);
        let c = s.copy();
        assert_eq!(c.item_id, 264);
        assert_eq!(c.count, 5);
        assert_eq!(c.damage, 2);
        // Stack round trip keeps id/count/damage.
        let ffi = s.to_stack();
        assert_eq!(ffi.item_id, 264);
        assert_eq!(ffi.stack_size, 5);
        assert_eq!(ffi.item_damage, 2);
        let back = PureItemStack::from_stack(&ffi);
        assert_eq!(back, s);
    }

    #[test]
    fn max_stack_size_reused() {
        // Mirrors GetMaxStackSizeDefault / GetMaxStackSizeLimited.
        assert_eq!(inventory_max_stack_size(264), 64);
        assert_eq!(inventory_max_stack_size(325), 1);
        assert_eq!(inventory_max_stack_size(332), 16);
        assert_eq!(inventory_max_stack_size(344), 64);
        assert_eq!(inventory_max_stack_size(276), 1);
    }

    #[test]
    fn max_damage_tools_follow_shift_formula() {
        // 32 << level, x4 for diamond (Item.h tool/sword ctors).
        assert_eq!(item_max_damage(ITEM_SWORD_WOOD), 32);
        assert_eq!(item_max_damage(ITEM_SHOVEL_WOOD), 32);
        assert_eq!(item_max_damage(ITEM_PICKAXE_WOOD), 32);
        assert_eq!(item_max_damage(ITEM_AXE_WOOD), 32);
        assert_eq!(item_max_damage(ITEM_SWORD_GOLD), 32);
        assert_eq!(item_max_damage(ITEM_SWORD_STONE), 64);
        assert_eq!(item_max_damage(ITEM_PICKAXE_STONE), 64);
        assert_eq!(item_max_damage(ITEM_SWORD_STEEL), 128);
        assert_eq!(item_max_damage(ITEM_PICKAXE_STEEL), 128);
        assert_eq!(item_max_damage(ITEM_AXE_STEEL), 128);
        assert_eq!(item_max_damage(ITEM_SWORD_DIAMOND), 1024);
        assert_eq!(item_max_damage(ITEM_PICKAXE_DIAMOND), 1024);
        assert_eq!(item_max_damage(ITEM_SHOVEL_DIAMOND), 1024);
    }

    #[test]
    fn max_damage_special_and_armor() {
        assert_eq!(item_max_damage(ITEM_FLINT_AND_STEEL), 64);
        assert_eq!(item_max_damage(ITEM_BOW), 32);
        assert_eq!(item_max_damage(ITEM_FISHING_ROD), 64);
        assert_eq!(item_max_damage(ITEM_HOE_WOOD), 32);
        assert_eq!(item_max_damage(ITEM_HOE_STONE), 64);
        assert_eq!(item_max_damage(ITEM_HOE_STEEL), 128);
        assert_eq!(item_max_damage(ITEM_HOE_DIAMOND), 256);
        assert_eq!(item_max_damage(ITEM_HOE_GOLD), 64);
        assert_eq!(item_max_damage(ITEM_HELMET_LEATHER), 33);
        assert_eq!(item_max_damage(ITEM_PLATE_DIAMOND), 384);
        assert_eq!(item_max_damage(ITEM_BOOTS_GOLD), 78);
        assert_eq!(item_max_damage(ITEM_DIAMOND), 0);
        assert_eq!(item_max_damage(ITEM_STICK), 0);
        assert_eq!(item_max_damage(9999), 0);
        assert_eq!(item_max_damage(0), 0);
    }

    #[test]
    fn damage_item_break_uses_table() {
        // Mirrors DamageItemBreak / DamageItemNoDurability via the
        // existing item_stack_damage helper + our durability table.
        let max = item_max_damage(ITEM_SWORD_WOOD);
        assert_eq!(max, 32);
        let mut s = ItemStack::new(ITEM_SWORD_WOOD, 1, 31);
        let broke = item_stack_damage(&mut s, 1, max);
        assert!(!broke);
        assert_eq!(s.item_damage, 32);
        let broke2 = item_stack_damage(&mut s, 1, max);
        assert!(broke2);
        assert_eq!(s.item_damage, 0);
        assert_eq!(s.stack_size, 0);

        let mut plain = ItemStack::new(ITEM_DIAMOND, 1, 0);
        let max_plain = item_max_damage(ITEM_DIAMOND);
        assert_eq!(max_plain, 0);
        let broke3 = item_stack_damage(&mut plain, 100, max_plain);
        assert!(!broke3);
        assert_eq!(plain.stack_size, 1);
    }

    #[test]
    fn food_heal_values() {
        assert_eq!(item_food_heal(ITEM_APPLE_RED), 4);
        assert_eq!(item_food_heal(ITEM_BOWL_SOUP), 10);
        assert_eq!(item_food_heal(ITEM_BREAD), 5);
        assert_eq!(item_food_heal(ITEM_PORK_RAW), 3);
        assert_eq!(item_food_heal(ITEM_PORK_COOKED), 8);
        assert_eq!(item_food_heal(ITEM_APPLE_GOLD), 42);
        assert_eq!(item_food_heal(ITEM_FISH_RAW), 2);
        assert_eq!(item_food_heal(ITEM_FISH_COOKED), 5);
        assert_eq!(item_food_heal(ITEM_DIAMOND), 0);
        assert_eq!(item_food_heal(ITEM_STICK), 0);
        assert!(item_is_food(ITEM_BREAD));
        assert!(!item_is_food(ITEM_DIAMOND));
    }

    #[test]
    fn tool_tier_and_speed() {
        assert_eq!(item_tool_tier(ITEM_PICKAXE_WOOD), 0);
        assert_eq!(item_tool_tier(ITEM_PICKAXE_GOLD), 0);
        assert_eq!(item_tool_tier(ITEM_PICKAXE_STONE), 1);
        assert_eq!(item_tool_tier(ITEM_PICKAXE_STEEL), 2);
        assert_eq!(item_tool_tier(ITEM_PICKAXE_DIAMOND), 3);
        assert_eq!(item_tool_tier(ITEM_HOE_STEEL), -1);
        assert_eq!(item_tool_tier(ITEM_DIAMOND), -1);
        assert_eq!(item_tool_tier(9999), -1);

        assert_eq!(item_tool_speed(ITEM_PICKAXE_STEEL), 6.0);
        assert_eq!(item_tool_speed(ITEM_AXE_STEEL), 6.0);
        assert_eq!(item_tool_speed(ITEM_SHOVEL_STEEL), 6.0);
        assert_eq!(item_tool_speed(ITEM_PICKAXE_WOOD), 2.0);
        assert_eq!(item_tool_speed(ITEM_PICKAXE_DIAMOND), 8.0);
        assert_eq!(item_tool_speed(ITEM_DIAMOND), 1.0);
    }

    #[test]
    fn tool_kind_mapping() {
        assert_eq!(
            item_tool_kind(ITEM_PICKAXE_STEEL),
            ItemToolKind::Pickaxe as i32
        );
        assert_eq!(
            item_tool_kind(ITEM_SHOVEL_STEEL),
            ItemToolKind::Spade as i32
        );
        assert_eq!(
            item_tool_kind(ITEM_AXE_STEEL),
            ItemToolKind::Axe as i32
        );
        assert_eq!(
            item_tool_kind(ITEM_SWORD_DIAMOND),
            ItemToolKind::Sword as i32
        );
        assert_eq!(
            item_tool_kind(ITEM_HOE_DIAMOND),
            ItemToolKind::Hoe as i32
        );
        assert_eq!(
            item_tool_kind(ITEM_DIAMOND),
            ItemToolKind::Other as i32
        );
    }

    #[test]
    fn explicit_effective_lists() {
        // Pickaxe list from Item.h ctor.
        assert!(item_is_effective_explicit(ITEM_PICKAXE_STEEL, 1));
        assert!(item_is_effective_explicit(ITEM_PICKAXE_STEEL, 4));
        assert!(!item_is_effective_explicit(ITEM_PICKAXE_STEEL, 3));
        // Spade list.
        assert!(item_is_effective_explicit(ITEM_SHOVEL_STEEL, 3));
        assert!(!item_is_effective_explicit(ITEM_SHOVEL_STEEL, 1));
        // Axe list.
        assert!(item_is_effective_explicit(ITEM_AXE_STEEL, 17));
        assert!(!item_is_effective_explicit(ITEM_AXE_STEEL, 1));
        // Swords / misc have no explicit list.
        assert!(!item_is_effective_explicit(ITEM_SWORD_STEEL, 1));
        assert!(!item_is_effective_explicit(ITEM_DIAMOND, 1));
        // Boundary ids mirror ToolGetStrVsBlock* tests.
        assert!(!item_is_effective_explicit(ITEM_PICKAXE_STEEL, -1));
        assert!(!item_is_effective_explicit(ITEM_PICKAXE_STEEL, 0));
        assert!(!item_is_effective_explicit(ITEM_PICKAXE_STEEL, 9999));
        assert!(!item_is_effective_explicit(ITEM_PICKAXE_STEEL, 256));
    }

    #[test]
    fn mining_module_agrees_with_tool_data() {
        // Mirrors PickaxeStrVsBlock / AxeEffectiveAgainstWood /
        // SpadeEffectiveAgainstDirt / harvest checks.
        assert_eq!(mining_get_str_vs_block(1, ITEM_PICKAXE_STEEL), 6.0);
        assert_eq!(mining_get_str_vs_block(17, ITEM_AXE_STEEL), 6.0);
        assert_eq!(mining_get_str_vs_block(3, ITEM_SHOVEL_STEEL), 6.0);
        assert_eq!(mining_get_str_vs_block(1, ITEM_DIAMOND), 1.0);
        assert!(mining_can_harvest(1, ITEM_PICKAXE_STEEL));
        assert!(!mining_can_harvest(1, ITEM_SHOVEL_WOOD));
        assert!(mining_can_harvest(61, ITEM_PICKAXE_STEEL));
        assert_eq!(mining_get_str_vs_block(-1, ITEM_PICKAXE_STEEL), 1.0);
        assert_eq!(mining_get_str_vs_block(0, ITEM_PICKAXE_STEEL), 1.0);
        assert_eq!(mining_get_str_vs_block(9999, ITEM_PICKAXE_STEEL), 1.0);
        assert_eq!(mining_get_str_vs_block(256, ITEM_PICKAXE_STEEL), 1.0);
    }

    #[test]
    fn use_right_click_returns_copy_semantics() {
        // Mirrors UseItemRightClickReturnsCopy for the base item path.
        let s = pure(264, 1, 0);
        let r = s.copy();
        assert_eq!(r.item_id, 264);
        assert_eq!(r.count, 1);
    }

    #[test]
    fn can_stack_rules() {
        let a = pure(ITEM_DIAMOND, 10, 0);
        let b = pure(ITEM_DIAMOND, 5, 0);
        assert!(a.can_stack_with(b));
        assert!(a.can_merge_with(b));
        // Different damage blocks stacking (matches inventory fill rule).
        let c = pure(ITEM_DIAMOND, 5, 1);
        assert!(!a.can_stack_with(c));
        // Different id blocks stacking.
        let d = pure(ITEM_COAL, 5, 0);
        assert!(!a.can_stack_with(d));
        // Non-stackable tools never stack.
        let t1 = pure(ITEM_SWORD_DIAMOND, 1, 0);
        let t2 = pure(ITEM_SWORD_DIAMOND, 1, 0);
        assert!(!t1.can_stack_with(t2));
        // Empty never stacks.
        assert!(!PureItemStack::empty().can_stack_with(a));
        assert!(!a.can_stack_with(PureItemStack::empty()));
    }

    #[test]
    fn split_takes_and_shrinks() {
        let mut s = pure(ITEM_DIAMOND, 10, 0);
        let part = s.split(4);
        assert_eq!(part, pure(ITEM_DIAMOND, 4, 0));
        assert_eq!(s, pure(ITEM_DIAMOND, 6, 0));
        // Over-take clamps and clears the source.
        let rest = s.split(99);
        assert_eq!(rest.count, 6);
        assert!(s.is_empty());
        // Non-positive take leaves source alone.
        let mut t = pure(ITEM_DIAMOND, 5, 0);
        let none = t.split(0);
        assert!(none.is_empty());
        assert_eq!(t.count, 5);
        let neg = t.split(-3);
        assert!(neg.is_empty());
        assert_eq!(t.count, 5);
    }

    #[test]
    fn merge_respects_limit() {
        // 60 + 10 with limit 64 -> 64 + 6 left.
        let mut dst = pure(ITEM_DIAMOND, 60, 0);
        let mut src = pure(ITEM_DIAMOND, 10, 0);
        dst.merge_from(&mut src);
        assert_eq!(dst.count, 64);
        assert_eq!(src.count, 6);
        // Mismatched damage does not move.
        let mut d2 = pure(ITEM_DIAMOND, 10, 0);
        let mut s2 = pure(ITEM_DIAMOND, 10, 1);
        d2.merge_from(&mut s2);
        assert_eq!(d2.count, 10);
        assert_eq!(s2.count, 10);
        // Empty slot adopts kind capped by limit (snowball limit 16).
        let mut e = PureItemStack::empty();
        let mut snow = pure(ITEM_SNOWBALL, 20, 0);
        e.merge_from(&mut snow);
        assert_eq!(e.item_id, ITEM_SNOWBALL);
        assert_eq!(e.count, 16);
        assert_eq!(snow.count, 4);
        // Full slot keeps source intact.
        let mut full = pure(ITEM_DIAMOND, 64, 0);
        let mut extra = pure(ITEM_DIAMOND, 1, 0);
        full.merge_from(&mut extra);
        assert_eq!(full.count, 64);
        assert_eq!(extra.count, 1);
    }
}
