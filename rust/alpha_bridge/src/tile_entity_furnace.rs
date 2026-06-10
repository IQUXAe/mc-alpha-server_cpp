use crate::inventory::FfiItemStack;

pub const SLOT_INPUT: usize = 0;
pub const SLOT_FUEL: usize = 1;
pub const SLOT_OUTPUT: usize = 2;
pub const FURNACE_SIZE: usize = 3;

#[repr(C)]
pub struct FfiFurnaceState {
    pub slots: [FfiItemStack; FURNACE_SIZE],
    pub burn_time: i16,
    pub cook_time: i16,
    pub current_item_burn_time: i16,
}

#[repr(C)]
pub struct FurnaceTickResult {
    pub changed: bool,
    pub needs_block_update: bool,
}

fn slot_empty(s: &FfiItemStack) -> bool {
    s.item_id < 0 || s.stack_size <= 0
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

#[no_mangle]
pub extern "C" fn furnace_create() -> FfiFurnaceState {
    FfiFurnaceState {
        slots: [FfiItemStack { stack_size: 0, animations_to_go: 0, item_id: -1, item_damage: 0 }; FURNACE_SIZE],
        burn_time: 0,
        cook_time: 0,
        current_item_burn_time: 0,
    }
}

#[no_mangle]
pub extern "C" fn furnace_tick(
    state: *mut FfiFurnaceState,
    fuel_burn_time_from_cpp: i32,
) -> FurnaceTickResult {
    let state = unsafe { &mut *state };
    let mut changed = false;

    let was_burning = state.burn_time > 0;

    if state.burn_time > 0 {
        state.burn_time -= 1;
    }

    // Try to start burning new fuel
    if state.burn_time == 0 && can_smelt(state) {
        if fuel_burn_time_from_cpp > 0 {
            state.current_item_burn_time = fuel_burn_time_from_cpp as i16;
            state.burn_time = fuel_burn_time_from_cpp as i16;
            changed = true;
            let fuel = &mut state.slots[SLOT_FUEL];
            fuel.stack_size -= 1;
            if fuel.stack_size <= 0 {
                fuel.item_id = -1;
                fuel.stack_size = 0;
                fuel.item_damage = 0;
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

#[no_mangle]
pub extern "C" fn furnace_get_slot(
    state: *const FfiFurnaceState,
    slot: i32,
) -> FfiItemStack {
    let state = unsafe { &*state };
    if slot < 0 || slot as usize >= FURNACE_SIZE {
        return FfiItemStack { stack_size: 0, animations_to_go: 0, item_id: -1, item_damage: 0 };
    }
    state.slots[slot as usize]
}

#[no_mangle]
pub extern "C" fn furnace_set_slot(
    state: *mut FfiFurnaceState,
    slot: i32,
    stack: FfiItemStack,
) {
    let state = unsafe { &mut *state };
    if slot < 0 || slot as usize >= FURNACE_SIZE {
        return;
    }
    state.slots[slot as usize] = stack;
}

fn can_smelt(state: &FfiFurnaceState) -> bool {
    let input = &state.slots[SLOT_INPUT];
    if slot_empty(input) {
        return false;
    }
    let result_id = get_smelting_result(input.item_id);
    if result_id < 0 || result_id >= 32000 {
        return false;
    }
    let output = &state.slots[SLOT_OUTPUT];
    if slot_empty(output) {
        return true;
    }
    if output.item_id != result_id {
        return false;
    }
    output.stack_size < 64
}

fn smelt_item(state: &mut FfiFurnaceState) {
    let result_id = get_smelting_result(state.slots[SLOT_INPUT].item_id);
    if result_id < 0 {
        return;
    }

    {
        let output = &mut state.slots[SLOT_OUTPUT];
        if slot_empty(output) {
            output.item_id = result_id;
            output.stack_size = 1;
            output.item_damage = 0;
        } else if output.item_id == result_id {
            output.stack_size += 1;
        }
    }

    {
        let input = &mut state.slots[SLOT_INPUT];
        input.stack_size -= 1;
        if input.stack_size <= 0 {
            input.item_id = -1;
            input.stack_size = 0;
            input.item_damage = 0;
        }
    }
}
