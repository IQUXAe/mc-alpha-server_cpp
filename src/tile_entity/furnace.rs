use crate::block::table::{BlockMaterial, block_properties_get};
use crate::inventory::ItemStack;

pub const SLOT_INPUT: usize = 0;
pub const SLOT_FUEL: usize = 1;
pub const SLOT_OUTPUT: usize = 2;
pub const FURNACE_SIZE: usize = 3;

#[derive(Clone, Copy, Debug)]
pub struct FurnaceState {
    pub slots: [ItemStack; FURNACE_SIZE],
    pub burn_time: i16,
    pub cook_time: i16,
    pub current_item_burn_time: i16,
}

pub struct FurnaceTickResult {
    pub changed: bool,
    pub needs_block_update: bool,
}

fn get_smelting_result(item_id: i32) -> i32 {
    match item_id {
        15 => 265,   // Iron Ore -> Iron Ingot
        14 => 266,   // Gold Ore -> Gold Ingot
        56 => 264,   // Diamond Ore -> Diamond
        12 => 20,    // Sand -> Glass
        4 => 1,      // Cobblestone -> Stone
        319 => 320,  // Raw Pork -> Cooked Pork
        349 => 350,  // Raw Fish -> Cooked Fish
        337 => 336,  // Clay (item) -> Brick (item)
        _ => -1,
    }
}

pub fn furnace_create() -> FurnaceState {
    FurnaceState {
        slots: [ItemStack::empty_tile(); FURNACE_SIZE],
        burn_time: 0,
        cook_time: 0,
        current_item_burn_time: 0,
    }
}

/// Mirrors `TileEntityFurnace::getItemBurnTime` 1:1: wood-material blocks
/// burn 300 ticks, stick 100, coal 1600, lava bucket 20000, else 0.
/// (C++ gates blocks on `blocksList[id] != null`, but block ids register
/// exactly when their material is not air, so a wood check alone matches;
/// the `id >= 0` guard hardens the C++ `blocksList[-1]` read for the
/// empty-slot id, which observably yields 0 there too.)
pub fn fuel_burn_time(item_id: i32) -> i32 {
    if item_id >= 0 && item_id < 256 {
        if block_properties_get(item_id as u32).material == BlockMaterial::Wood as u8 {
            return 300;
        }
    }
    match item_id {
        280 => 100,
        263 => 1600,
        327 => 20000,
        _ => 0,
    }
}

/// Native tick: the fuel burn time is looked up from the fuel slot,
/// then the shared core runs.
pub fn furnace_tick_native(state: &mut FurnaceState) -> FurnaceTickResult {
    let fuel = fuel_burn_time(state.slots[SLOT_FUEL].item_id);
    tick_core(state, fuel)
}

fn tick_core(state: &mut FurnaceState, fuel: i32) -> FurnaceTickResult {
    let mut changed = false;

    let was_burning = state.burn_time > 0;

    if state.burn_time > 0 {
        state.burn_time -= 1;
    }

    // Try to start burning new fuel
    if state.burn_time == 0 && can_smelt(state) {
        if fuel > 0 {
            state.current_item_burn_time = fuel as i16;
            state.burn_time = fuel as i16;
            changed = true;
            let fuel_slot = &mut state.slots[SLOT_FUEL];
            fuel_slot.count -= 1;
            if fuel_slot.count <= 0 {
                fuel_slot.item_id = -1;
                fuel_slot.count = 0;
                fuel_slot.damage = 0;
            }
        }
    }

    // Cook
    if state.burn_time > 0 && can_smelt(state) {
        state.cook_time += 1;
        if state.cook_time >= 200 {
            state.cook_time = 0;
            smelt_item(state);
            changed = true;
        }
    } else {
        state.cook_time = 0;
    }

    let is_burning = state.burn_time > 0;
    FurnaceTickResult {
        changed,
        needs_block_update: was_burning != is_burning,
    }
}

fn can_smelt(state: &FurnaceState) -> bool {
    let input = &state.slots[SLOT_INPUT];
    if input.is_empty() {
        return false;
    }
    let result_id = get_smelting_result(input.item_id);
    // Matches C++ Item::itemsList[32000] — reject out-of-range item IDs
    if result_id < 0 || result_id >= 32000 {
        return false;
    }
    let output = &state.slots[SLOT_OUTPUT];
    if output.is_empty() {
        return true;
    }
    if output.item_id != result_id {
        return false;
    }
    output.count < 64
}

fn smelt_item(state: &mut FurnaceState) {
    let result_id = get_smelting_result(state.slots[SLOT_INPUT].item_id);
    if result_id < 0 {
        return;
    }

    {
        let output = &mut state.slots[SLOT_OUTPUT];
        if output.is_empty() {
            output.item_id = result_id;
            output.count = 1;
            output.damage = 0;
        } else if output.item_id == result_id {
            output.count += 1;
        }
    }

    {
        let input = &mut state.slots[SLOT_INPUT];
        input.count -= 1;
        if input.count <= 0 {
            input.item_id = -1;
            input.count = 0;
            input.damage = 0;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn stack(item_id: i32, count: i32) -> ItemStack {
        ItemStack::new(item_id, count, 0)
    }

    fn empty() -> ItemStack {
        ItemStack::empty_tile()
    }

    fn state_with(input: ItemStack, fuel: ItemStack) -> FurnaceState {
        FurnaceState {
            slots: [input, fuel, empty()],
            burn_time: 0,
            cook_time: 0,
            current_item_burn_time: 0,
        }
    }

    #[test]
    fn fuel_table() {
        // Wood-material blocks burn 300 (planks, log, bookshelf, workbench...).
        // 25 is null in vanilla (no fuel).
        for id in [5, 17, 47, 53, 54, 58, 63, 64, 65, 68, 72, 84, 85] {
            assert_eq!(fuel_burn_time(id), 300, "wood block {id}");
        }
        // Ordinary blocks and air burn nothing.
        for id in [0, 1, 3, 4, 20, 61, 62] {
            assert_eq!(fuel_burn_time(id), 0, "block {id}");
        }
        assert_eq!(fuel_burn_time(280), 100);
        assert_eq!(fuel_burn_time(263), 1600);
        assert_eq!(fuel_burn_time(327), 20000);
        // Empty slot and junk ids.
        assert_eq!(fuel_burn_time(-1), 0);
        assert_eq!(fuel_burn_time(256), 0);
        assert_eq!(fuel_burn_time(32000), 0);
    }

    #[test]
    fn native_tick_lights_fuel_from_slot() {
        let mut s = state_with(stack(4, 1), stack(5, 2));
        let r = furnace_tick_native(&mut s);
        assert!(r.needs_block_update);
        assert_eq!(s.burn_time, 300);
        assert_eq!(s.current_item_burn_time, 300);
        assert_eq!(s.slots[SLOT_FUEL].count, 1);
        // Already burning: no block update, burn counts down.
        let r = furnace_tick_native(&mut s);
        assert!(!r.needs_block_update);
        assert_eq!(s.burn_time, 299);
    }

    #[test]
    fn native_tick_without_fuel_stays_dark() {
        let mut s = state_with(stack(4, 1), empty());
        let r = furnace_tick_native(&mut s);
        assert!(!r.needs_block_update);
        assert_eq!(s.burn_time, 0);
        assert_eq!(s.cook_time, 0);
    }

    #[test]
    fn native_tick_smelts_cobble_to_stone() {
        let mut s = state_with(stack(4, 1), stack(263, 1));
        for _ in 0..200 {
            furnace_tick_native(&mut s);
        }
        assert_eq!(s.slots[SLOT_OUTPUT].item_id, 1);
        assert_eq!(s.slots[SLOT_OUTPUT].count, 1);
        assert_eq!(s.slots[SLOT_INPUT].item_id, -1);
        // Coal still burning (1600 - 200).
        assert!(s.burn_time > 0);
    }
}
