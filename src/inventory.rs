#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ItemStack {
    pub count: i32,
    pub item_id: i32,
    pub damage: i32,
}

impl ItemStack {
    pub const fn new(item_id: i32, count: i32, damage: i32) -> Self {
        Self { count, item_id, damage }
    }

    /// Empty player-style slot (`item_id: 0`).
    pub const fn empty() -> Self {
        Self::new(0, 0, 0)
    }

    /// Empty tile-entity slot (`item_id: -1`, as stored in chest/furnace rows).
    pub const fn empty_tile() -> Self {
        Self::new(-1, 0, 0)
    }

    pub const fn is_empty(self) -> bool {
        self.item_id <= 0 || self.count <= 0
    }
}

// Returns true if the item broke (stack size changed).
// max_damage comes from the item table (this module owns no registry).
pub fn item_stack_damage(stack: &mut ItemStack, damage: i32, max_damage: i32) -> bool {
    if max_damage <= 0 {
        return false;
    }
    stack.damage += damage;
    if stack.damage > max_damage {
        stack.count -= 1;
        if stack.count < 0 {
            stack.count = 0;
        }
        stack.damage = 0;
        return true;
    }
    false
}
