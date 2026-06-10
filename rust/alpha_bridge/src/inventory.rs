#[repr(C)]
#[derive(Clone, Copy)]
pub struct FfiItemStack {
    pub stack_size: i32,
    pub animations_to_go: i32,
    pub item_id: i32,
    pub item_damage: i32,
}

#[no_mangle]
pub extern "C" fn item_stack_create(item_id: i32, stack_size: i32, item_damage: i32) -> FfiItemStack {
    FfiItemStack {
        stack_size,
        animations_to_go: 0,
        item_id,
        item_damage,
    }
}

#[no_mangle]
pub extern "C" fn item_stack_copy(stack: *const FfiItemStack) -> FfiItemStack {
    unsafe { *stack }
}

// Returns true if the item broke (stack size changed).
// max_damage is passed from C++ (Rust cannot access Item::itemsList).
#[no_mangle]
pub extern "C" fn item_stack_damage(stack: *mut FfiItemStack, damage: i32, max_damage: i32) -> bool {
    let s = unsafe { &mut *stack };
    if max_damage <= 0 {
        return false;
    }
    s.item_damage += damage;
    if s.item_damage > max_damage {
        s.stack_size -= 1;
        if s.stack_size < 0 {
            s.stack_size = 0;
        }
        s.item_damage = 0;
        return true;
    }
    false
}
