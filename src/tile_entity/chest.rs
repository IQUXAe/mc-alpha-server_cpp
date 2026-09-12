use crate::inventory::ItemStack;

pub const CHEST_SIZE: usize = 27;

#[derive(Clone, Copy, Debug)]
pub struct ChestState {
    pub slots: [ItemStack; CHEST_SIZE],
}

pub fn chest_create() -> ChestState {
    ChestState {
        slots: [ItemStack { stack_size: 0, animations_to_go: 0, item_id: -1, item_damage: 0 }; CHEST_SIZE],
    }
}
