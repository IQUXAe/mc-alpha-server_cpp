use crate::inventory::FfiItemStack;

pub const CHEST_SIZE: usize = 27;

#[repr(C)]
pub struct FfiChestState {
    pub slots: [FfiItemStack; CHEST_SIZE],
}

#[no_mangle]
pub extern "C" fn chest_create() -> FfiChestState {
    FfiChestState {
        slots: [FfiItemStack { stack_size: 0, animations_to_go: 0, item_id: -1, item_damage: 0 }; CHEST_SIZE],
    }
}
