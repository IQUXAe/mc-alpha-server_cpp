#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ItemStack {
    pub stack_size: i32,
    pub animations_to_go: i32,
    pub item_id: i32,
    pub item_damage: i32,
}

impl ItemStack {
    pub const fn new(item_id: i32, stack_size: i32, item_damage: i32) -> Self {
        Self { stack_size, animations_to_go: 0, item_id, item_damage }
    }

    /// Empty player-style slot (`item_id: 0`).
    pub const fn empty() -> Self {
        Self::new(0, 0, 0)
    }

    /// Empty tile-entity slot (`item_id: -1`, as stored in chest/furnace rows).
    pub const fn empty_tile() -> Self {
        Self::new(-1, 0, 0)
    }
}

// Returns true if the item broke (stack size changed).
// max_damage comes from the item table (this module owns no registry).
pub fn item_stack_damage(stack: &mut ItemStack, damage: i32, max_damage: i32) -> bool {
    if max_damage <= 0 {
        return false;
    }
    stack.item_damage += damage;
    if stack.item_damage > max_damage {
        stack.stack_size -= 1;
        if stack.stack_size < 0 {
            stack.stack_size = 0;
        }
        stack.item_damage = 0;
        return true;
    }
    false
}
