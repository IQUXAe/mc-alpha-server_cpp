use crate::inventory::ItemStack;

pub const CHEST_SIZE: usize = 27;

#[derive(Clone, Copy, Debug)]
pub struct ChestState {
    pub slots: [ItemStack; CHEST_SIZE],
}

pub fn chest_create() -> ChestState {
    ChestState {
        slots: [ItemStack::empty_tile(); CHEST_SIZE],
    }
}
