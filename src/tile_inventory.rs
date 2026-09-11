//! Tile-inventory slot operations ported from C++ `TileEntityChest` and
//! `TileEntityFurnace` (mirrors Java `TileEntityChest` / `TileEntityFurnace`
//! slot handling).
//!
//! The slot arrays (`FfiItemStack`) already live in Rust-owned state; these
//! are the branchy take/store/clear operations over them. NBT translation,
//! `ItemStack` allocation, and dirty marking stay in C++ (they need the C++
//! NBT DOM and object model).
//!
//! This also removes the `memcpy(&ffi, stack, sizeof(ffi))` hack in
//! `setInventorySlotContents`: C++ now passes explicit fields, so the copy
//! no longer depends on `ItemStack` field layout matching `FfiItemStack`.

use crate::inventory::FfiItemStack;

/// Empty-slot sentinel (mirrors the `{0, 0, -1, 0}` initializer).
pub const TILE_EMPTY_ID: i32 = -1;

fn slots_mut(slots: *mut FfiItemStack, len: usize) -> Option<&'static mut [FfiItemStack]> {
    if slots.is_null() {
        return None;
    }
    Some(unsafe { std::slice::from_raw_parts_mut(slots, len) })
}

/// Taken item out-param for `tile_slot_take`.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct TileTaken {
    pub has_item: bool,
    pub item_id: i32,
    pub count: i32,
    pub damage: i32,
}

/// Take up to `amount` from a slot (mirrors `decrStackSize`). Returns true
/// when something was taken (C++ marks dirty and wraps `out` in an
/// `ItemStack`); `out.has_item` is false and the slot untouched otherwise.
#[no_mangle]
pub unsafe extern "C" fn tile_slot_take(
    slots: *mut FfiItemStack,
    len: usize,
    idx: i32,
    amount: i32,
    out: *mut TileTaken,
) -> bool {
    if out.is_null() {
        return false;
    }
    let empty = TileTaken { has_item: false, item_id: 0, count: 0, damage: 0 };
    let Some(list) = slots_mut(slots, len) else {
        unsafe {
            *out = empty;
        }
        return false;
    };
    if idx < 0 {
        unsafe {
            *out = empty;
        }
        return false;
    }
    let Some(slot) = list.get_mut(idx as usize) else {
        unsafe {
            *out = empty;
        }
        return false;
    };
    if slot.item_id < 0 {
        unsafe {
            *out = empty;
        }
        return false;
    }
    let taken = TileTaken { has_item: true, item_id: slot.item_id, count: slot.stack_size, damage: slot.item_damage };
    if slot.stack_size <= amount {
        slot.item_id = TILE_EMPTY_ID;
        slot.stack_size = 0;
        slot.item_damage = 0;
    } else {
        slot.stack_size -= amount;
    }
    // Partial takes report the requested amount (mirrors the C++ partial path).
    let taken = if taken.count > amount { TileTaken { count: amount, ..taken } } else { taken };
    unsafe {
        *out = taken;
    }
    true
}

/// Store into a slot (mirrors `setInventorySlotContents` with explicit
/// fields instead of `memcpy`). Returns true when C++ must mark dirty
/// (bad index is a silent no-op like C++).
#[no_mangle]
pub unsafe extern "C" fn tile_slot_store(
    slots: *mut FfiItemStack,
    len: usize,
    idx: i32,
    has_item: bool,
    item_id: i32,
    count: i32,
    damage: i32,
    limit: i32,
) -> bool {
    let Some(list) = slots_mut(slots, len) else {
        return false;
    };
    if idx < 0 {
        return false;
    }
    let Some(slot) = list.get_mut(idx as usize) else {
        return false;
    };
    if has_item {
        slot.item_id = item_id;
        slot.stack_size = count;
        slot.item_damage = damage;
        if slot.stack_size > limit {
            slot.stack_size = limit;
        }
    } else {
        slot.item_id = TILE_EMPTY_ID;
        slot.stack_size = 0;
        slot.item_damage = 0;
    }
    true
}

/// Reset every slot to the empty sentinel (mirrors the NBT-read preamble).
#[no_mangle]
pub unsafe extern "C" fn tile_slots_clear(slots: *mut FfiItemStack, len: usize) -> bool {
    let Some(list) = slots_mut(slots, len) else {
        return false;
    };
    for slot in list.iter_mut() {
        slot.item_id = TILE_EMPTY_ID;
        slot.stack_size = 0;
        slot.item_damage = 0;
    }
    true
}

#[cfg(test)]
mod tests {
    use super::*;

    fn slots() -> [FfiItemStack; 3] {
        [
            FfiItemStack { stack_size: 10, animations_to_go: 0, item_id: 5, item_damage: 0 },
            FfiItemStack { stack_size: 0, animations_to_go: 0, item_id: TILE_EMPTY_ID, item_damage: 0 },
            FfiItemStack { stack_size: 1, animations_to_go: 0, item_id: 7, item_damage: 2 },
        ]
    }

    fn taken() -> TileTaken {
        TileTaken { has_item: false, item_id: 0, count: 0, damage: 0 }
    }

    #[test]
    fn test_take_partial_and_full() {
        let mut s = slots();
        let mut out = taken();
        assert!(unsafe { tile_slot_take(s.as_mut_ptr(), 3, 0, 4, &mut out) });
        assert_eq!(out, TileTaken { has_item: true, item_id: 5, count: 4, damage: 0 });
        assert_eq!(s[0].stack_size, 6);
        // Full take empties the slot but reports the whole stack.
        assert!(unsafe { tile_slot_take(s.as_mut_ptr(), 3, 0, 6, &mut out) });
        assert_eq!(out, TileTaken { has_item: true, item_id: 5, count: 6, damage: 0 });
        assert_eq!(s[0].item_id, TILE_EMPTY_ID);
        // Over-take clamps to the stack (mirrors stack_size <= amount).
        let mut s = slots();
        assert!(unsafe { tile_slot_take(s.as_mut_ptr(), 3, 2, 99, &mut out) });
        assert_eq!(out, TileTaken { has_item: true, item_id: 7, count: 1, damage: 2 });
        assert_eq!(s[2].item_id, TILE_EMPTY_ID);
    }

    #[test]
    fn test_take_guards() {
        let mut s = slots();
        let mut out = taken();
        assert!(!unsafe { tile_slot_take(s.as_mut_ptr(), 3, 1, 4, &mut out) });
        assert!(!out.has_item);
        assert!(!unsafe { tile_slot_take(s.as_mut_ptr(), 3, 9, 4, &mut out) });
        assert!(!unsafe { tile_slot_take(s.as_mut_ptr(), 3, -1, 4, &mut out) });
        assert!(!unsafe { tile_slot_take(std::ptr::null_mut(), 3, 0, 4, &mut out) });
        assert!(!unsafe { tile_slot_take(s.as_mut_ptr(), 3, 0, 4, std::ptr::null_mut()) });
    }

    #[test]
    fn test_store_clamps_and_clears() {
        let mut s = slots();
        assert!(unsafe { tile_slot_store(s.as_mut_ptr(), 3, 0, true, 9, 99, 3, 64) });
        assert_eq!((s[0].item_id, s[0].stack_size, s[0].item_damage), (9, 64, 3));
        assert!(unsafe { tile_slot_store(s.as_mut_ptr(), 3, 0, false, 0, 0, 0, 64) });
        assert_eq!(s[0].item_id, TILE_EMPTY_ID);
        assert!(!unsafe { tile_slot_store(s.as_mut_ptr(), 3, 7, true, 9, 1, 0, 64) });
        assert!(!unsafe { tile_slot_store(std::ptr::null_mut(), 3, 0, true, 9, 1, 0, 64) });
    }

    #[test]
    fn test_clear_all() {
        let mut s = slots();
        assert!(unsafe { tile_slots_clear(s.as_mut_ptr(), 3) });
        assert!(s.iter().all(|slot| slot.item_id == TILE_EMPTY_ID && slot.stack_size == 0));
        assert!(!unsafe { tile_slots_clear(std::ptr::null_mut(), 3) });
    }
}
