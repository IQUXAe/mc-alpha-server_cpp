//! Safe port of `src/core/NibbleArray.h`.
//!
//! The C++ class stores a raw `uint8_t*` plus an `owns_data` flag and deletes
//! the copy constructor to avoid double-free. The Rust version owns a
//! `Vec<u8>` instead, so move semantics are handled by the compiler.
//!
//! Layout is 1:1 with C++: nibble `index = (x << 11) | (z << 7) | y`, the low
//! nibble lives in even indices and the high nibble in odd indices. A chunk
//! section holds 16 x 128 x 16 = 32768 nibbles = 16384 bytes.
//!
//! Differences: out-of-range coordinates return `0` on read and are ignored
//! on write (C++ would read/write out of bounds), and an empty array reports
//! invalid instead of dereferencing null.

/// Half-byte array for block light / sky light / metadata storage.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct NibbleArray {
    data: Option<Vec<u8>>,
}

impl NibbleArray {
    /// Empty, invalid array (mirrors the C++ default constructor).
    pub fn new() -> Self {
        Self { data: None }
    }

    /// Zeroed array holding `nibbles` 4-bit entries.
    ///
    /// Mirrors `explicit NibbleArray(int size)`, which allocates `size >> 1`
    /// bytes value-initialized to zero.
    pub fn with_nibbles(nibbles: usize) -> Self {
        Self {
            data: Some(vec![0u8; nibbles >> 1]),
        }
    }

    /// Take ownership of already-packed bytes.
    ///
    /// Mirrors `explicit NibbleArray(std::vector<uint8_t> d)`.
    pub fn from_bytes(bytes: Vec<u8>) -> Self {
        Self { data: Some(bytes) }
    }

    /// Mirrors `isValid()`: false only for the default-constructed array.
    pub fn is_valid(&self) -> bool {
        self.data.is_some()
    }

    /// Byte count of the backing store (mirrors the C++ `size` field).
    pub fn byte_len(&self) -> usize {
        match &self.data {
            Some(data) => data.len(),
            None => 0,
        }
    }

    /// Read-only view of the packed bytes (mirrors `view()`).
    pub fn view(&self) -> &[u8] {
        match &self.data {
            Some(data) => data.as_slice(),
            None => &[],
        }
    }

    /// Packed nibble index for chunk-local coordinates, or `None` when any
    /// coordinate is negative.
    fn nibble_index(x: i32, y: i32, z: i32) -> Option<usize> {
        if x < 0 || y < 0 || z < 0 {
            return None;
        }
        Some(((x as usize) << 11) | ((z as usize) << 7) | (y as usize))
    }

    /// Mirrors `getNibble()`. Returns `0` for invalid arrays and for
    /// coordinates outside the backing store.
    pub fn get_nibble(&self, x: i32, y: i32, z: i32) -> u8 {
        let data = match &self.data {
            Some(data) => data,
            None => return 0,
        };
        let index = match Self::nibble_index(x, y, z) {
            Some(index) => index,
            None => return 0,
        };
        let byte = match data.get(index >> 1) {
            Some(byte) => *byte,
            None => return 0,
        };
        if index & 1 == 1 {
            (byte >> 4) & 0xF
        } else {
            byte & 0xF
        }
    }

    /// Mirrors `setNibble()`. Only the low 4 bits of `value` are stored.
    /// No-op for invalid arrays and out-of-range coordinates.
    pub fn set_nibble(&mut self, x: i32, y: i32, z: i32, value: u8) {
        let data = match &mut self.data {
            Some(data) => data,
            None => return,
        };
        let index = match Self::nibble_index(x, y, z) {
            Some(index) => index,
            None => return,
        };
        let byte = match data.get_mut(index >> 1) {
            Some(byte) => byte,
            None => return,
        };
        if index & 1 == 1 {
            *byte = (*byte & 0x0F) | ((value & 0xF) << 4);
        } else {
            *byte = (*byte & 0xF0) | (value & 0xF);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // Chunk-section dimensions shared by the C++ TestNibbleArray suite.
    const NIBBLES: usize = 16 * 128 * 16;

    #[test]
    fn default_array_is_invalid() {
        let arr = NibbleArray::new();
        assert!(!arr.is_valid());
        assert_eq!(arr.byte_len(), 0);
        assert!(arr.view().is_empty());
    }

    #[test]
    fn size_constructor_halves_nibble_count() {
        let arr = NibbleArray::with_nibbles(32768);
        assert!(arr.is_valid());
        assert_eq!(arr.byte_len(), 16384);
    }

    #[test]
    fn set_and_get_roundtrip() {
        let mut arr = NibbleArray::with_nibbles(NIBBLES);
        arr.set_nibble(5, 64, 7, 0xA);
        assert_eq!(arr.get_nibble(5, 64, 7), 0xA);
    }

    #[test]
    fn fresh_array_reads_zero_at_corners() {
        let arr = NibbleArray::with_nibbles(NIBBLES);
        assert_eq!(arr.get_nibble(0, 0, 0), 0);
        assert_eq!(arr.get_nibble(15, 127, 15), 0);
    }

    #[test]
    fn max_value_at_far_corner() {
        let mut arr = NibbleArray::with_nibbles(NIBBLES);
        arr.set_nibble(15, 127, 15, 0xF);
        assert_eq!(arr.get_nibble(15, 127, 15), 0xF);
    }

    #[test]
    fn low_and_high_nibbles_share_byte_without_clobbering() {
        let mut arr = NibbleArray::with_nibbles(NIBBLES);
        // (0,0,0) -> index 0 (low), (0,1,0) -> index 1 (high): same byte.
        arr.set_nibble(0, 0, 0, 0xA);
        arr.set_nibble(0, 1, 0, 0xB);
        assert_eq!(arr.get_nibble(0, 0, 0), 0xA);
        assert_eq!(arr.get_nibble(0, 1, 0), 0xB);
        // Overwriting one half must preserve the other half.
        arr.set_nibble(0, 0, 0, 0x1);
        assert_eq!(arr.get_nibble(0, 0, 0), 0x1);
        assert_eq!(arr.get_nibble(0, 1, 0), 0xB);
    }

    #[test]
    fn index_formula_mapping() {
        let mut arr = NibbleArray::with_nibbles(NIBBLES);
        arr.set_nibble(1, 0, 2, 0xC);
        assert_eq!(arr.get_nibble(1, 0, 2), 0xC);
    }

    #[test]
    fn from_bytes_constructor() {
        let arr = NibbleArray::from_bytes(vec![0xAB; 16384]);
        assert!(arr.is_valid());
        // 0xAB: low nibble of byte 0 is 0xB at index 0 == (0,0,0).
        assert_eq!(arr.get_nibble(0, 0, 0), 0xB);
        assert_eq!(arr.get_nibble(0, 1, 0), 0xA);
    }

    #[test]
    fn view_exposes_backing_bytes() {
        let arr = NibbleArray::with_nibbles(32768);
        assert_eq!(arr.view().len(), 16384);
    }

    #[test]
    fn value_is_masked_to_four_bits() {
        let mut arr = NibbleArray::with_nibbles(NIBBLES);
        arr.set_nibble(3, 10, 4, 0x1A);
        assert_eq!(arr.get_nibble(3, 10, 4), 0xA);
    }

    #[test]
    fn invalid_and_out_of_range_access_is_safe() {
        let mut arr = NibbleArray::new();
        // Must not panic; reads mirror the C++ null-guard returning 0.
        arr.set_nibble(0, 0, 0, 0xF);
        assert_eq!(arr.get_nibble(0, 0, 0), 0);

        let mut arr = NibbleArray::with_nibbles(NIBBLES);
        assert_eq!(arr.get_nibble(-1, 0, 0), 0);
        assert_eq!(arr.get_nibble(0, -1, 0), 0);
        assert_eq!(arr.get_nibble(0, 0, -1), 0);
        assert_eq!(arr.get_nibble(16, 0, 0), 0);
        assert_eq!(arr.get_nibble(0, 128, 0), 0);
        arr.set_nibble(-1, 0, 0, 0xF);
        arr.set_nibble(0, 200, 0, 0xF);
        // Untouched storage still reads zero.
        assert_eq!(arr.get_nibble(0, 0, 0), 0);
    }
}
