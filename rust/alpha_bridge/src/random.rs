pub const MULT: u64 = 0x5DEECE66D;
pub const ADD: u64 = 0xB;
pub const MASK: u64 = (1 << 48) - 1;

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
            return (((bound as u64).wrapping_mul(self.next(31) as u64)) >> 31) as i32;
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
