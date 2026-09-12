pub const SIGN_LINES: usize = 4;

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct FfiSignState {
    pub lines: [[u8; 16]; SIGN_LINES],
}

pub fn sign_create() -> FfiSignState {
    FfiSignState {
        lines: [[0u8; 16]; SIGN_LINES],
    }
}

pub fn sign_set_line(state: &mut FfiSignState, line: i32, text: &str) {
    if line < 0 || line as usize >= SIGN_LINES {
        return;
    }
    let bytes = text.as_bytes();
    let len = bytes.len().min(15);
    let buf = &mut state.lines[line as usize];
    buf[..len].copy_from_slice(&bytes[..len]);
    // Zero the tail so a short rewrite never leaves stale bytes visible.
    for b in buf[len..].iter_mut() {
        *b = 0;
    }
}
