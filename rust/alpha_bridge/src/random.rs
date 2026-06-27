use std::sync::{Mutex, OnceLock};

pub const MULT: u64 = 0x5DEECE66D;
pub const ADD: u64 = 0xB;
pub const MASK: u64 = (1 << 48) - 1;

fn global_rng() -> &'static Mutex<JavaRandom> {
    static RNG: OnceLock<Mutex<JavaRandom>> = OnceLock::new();
    RNG.get_or_init(|| {
        let seed = match get_seed_from_os() {
            Some(s) => s,
            None => {
                let pid = std::process::id() as i64;
                let time = std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap_or_default()
                    .as_nanos() as i64;
                pid.wrapping_mul(0x9E3779B97F4A7C15u64 as i64).wrapping_add(time)
            }
        };
        Mutex::new(JavaRandom::new(seed))
    })
}

fn get_seed_from_os() -> Option<i64> {
    let mut buf = [0u8; 8];
    let ret = unsafe {
        libc::getrandom(buf.as_mut_ptr() as *mut libc::c_void, buf.len(), 0)
    };
    if ret == buf.len() as isize {
        Some(i64::from_ne_bytes(buf))
    } else {
        None
    }
}

#[no_mangle]
pub extern "C" fn alpha_rng_next_int(bound: i32) -> i32 {
    let mut rng = global_rng().lock().unwrap();
    rng.next_int_bound(bound)
}

#[no_mangle]
pub extern "C" fn alpha_rng_next_float() -> f32 {
    let mut rng = global_rng().lock().unwrap();
    rng.next_float()
}

#[no_mangle]
pub extern "C" fn alpha_rng_next_double() -> f64 {
    let mut rng = global_rng().lock().unwrap();
    rng.next_double()
}

#[derive(Clone, Copy, Debug)]
pub struct JavaRandom {
    seed: u64,
}

impl JavaRandom {
    pub fn new(seed: i64) -> Self {
        let mut rand = JavaRandom { seed: 0 };
        rand.set_seed(seed);
        rand
    }

    pub fn set_seed(&mut self, seed: i64) {
        self.seed = ((seed as u64) ^ MULT) & MASK;
    }

    pub fn next(&mut self, bits: u32) -> i32 {
        self.seed = (self.seed.wrapping_mul(MULT).wrapping_add(ADD)) & MASK;
        (self.seed >> (48 - bits)) as i32
    }

    pub fn next_int(&mut self) -> i32 {
        self.next(32)
    }

    pub fn next_int_bound(&mut self, bound: i32) -> i32 {
        if bound <= 0 {
            return 0; // fallback
        }
        let ubound = bound as u32;
        if (ubound & ubound.wrapping_neg()) == ubound {
            // Power of 2
            return ((bound as i64).wrapping_mul(self.next(31) as i64) >> 31) as i32;
        }
        let mut bits;
        let mut val;
        loop {
            bits = self.next(31);
            val = bits % bound;
            if bits.wrapping_sub(val).wrapping_add(bound - 1) >= 0 {
                break;
            }
        }
        val
    }

    pub fn next_long(&mut self) -> i64 {
        ((self.next(32) as i64) << 32).wrapping_add(self.next(32) as i64)
    }

    pub fn next_float(&mut self) -> f32 {
        self.next(24) as f32 / (1 << 24) as f32
    }

    pub fn next_double(&mut self) -> f64 {
        let h = (self.next(26) as i64) << 27;
        let l = self.next(27) as i64;
        h.wrapping_add(l) as f64 / (1i64 << 53) as f64
    }
}
