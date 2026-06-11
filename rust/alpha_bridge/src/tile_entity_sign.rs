use std::ffi::CStr;

pub const SIGN_LINES: usize = 4;

#[repr(C)]
pub struct FfiSignState {
    pub lines: [[u8; 16]; SIGN_LINES],
}

#[no_mangle]
pub extern "C" fn sign_create() -> FfiSignState {
    FfiSignState {
        lines: [[0u8; 16]; SIGN_LINES],
    }
}

#[no_mangle]
pub extern "C" fn sign_set_line(state: *mut FfiSignState, line: i32, text: *const std::ffi::c_char) {
    if state.is_null() || text.is_null() {
        return;
    }
    let state = unsafe { &mut *state };
    if line < 0 || line as usize >= SIGN_LINES {
        return;
    }
    let bytes = unsafe { CStr::from_ptr(text) }.to_bytes();
    let len = bytes.len().min(15);
    let buf = &mut state.lines[line as usize];
    buf[..len].copy_from_slice(&bytes[..len]);
    buf[len] = 0;
}
