use flate2::read::{GzDecoder, GzEncoder};
use flate2::Compression;
use libc::{c_char, c_int, c_uchar, size_t};
use std::io::Read;
use std::ffi::{CStr, CString};
use std::ptr;
use std::slice;

#[repr(C)]
pub struct AlphaBuffer {
    pub data: *mut c_uchar,
    pub len: size_t,
}

#[repr(C)]
pub struct AlphaLevelDat {
    pub random_seed: i64,
    pub spawn_x: i32,
    pub spawn_y: i32,
    pub spawn_z: i32,
    pub world_time: i64,
    pub size_on_disk: i64,
    pub version: i32,
    pub level_name: *const c_char,
}

impl AlphaBuffer {
    fn empty() -> Self {
        Self {
            data: ptr::null_mut(),
            len: 0,
        }
    }

    fn from_vec(mut bytes: Vec<u8>) -> Self {
        bytes.shrink_to_fit();
        let data = bytes.as_mut_ptr();
        let len = bytes.len();
        std::mem::forget(bytes);
        Self {
            data,
            len,
        }
    }
}

fn write_u16(buf: &mut Vec<u8>, value: u16) {
    buf.extend_from_slice(&value.to_be_bytes());
}

fn write_i32(buf: &mut Vec<u8>, value: i32) {
    buf.extend_from_slice(&value.to_be_bytes());
}

fn write_i64(buf: &mut Vec<u8>, value: i64) {
    buf.extend_from_slice(&value.to_be_bytes());
}

fn write_string(buf: &mut Vec<u8>, value: &str) -> Option<()> {
    let bytes = value.as_bytes();
    let len = u16::try_from(bytes.len()).ok()?;
    write_u16(buf, len);
    buf.extend_from_slice(bytes);
    Some(())
}

fn write_tag_i32(buf: &mut Vec<u8>, name: &str, value: i32) -> Option<()> {
    buf.push(3);
    write_string(buf, name)?;
    write_i32(buf, value);
    Some(())
}

fn write_tag_i64(buf: &mut Vec<u8>, name: &str, value: i64) -> Option<()> {
    buf.push(4);
    write_string(buf, name)?;
    write_i64(buf, value);
    Some(())
}

fn write_tag_string(buf: &mut Vec<u8>, name: &str, value: &str) -> Option<()> {
    buf.push(8);
    write_string(buf, name)?;
    write_string(buf, value)?;
    Some(())
}

struct Cursor<'a> {
    data: &'a [u8],
    pos: usize,
}

impl<'a> Cursor<'a> {
    fn new(data: &'a [u8]) -> Self {
        Self { data, pos: 0 }
    }

    fn read_u8(&mut self) -> Option<u8> {
        let value = *self.data.get(self.pos)?;
        self.pos += 1;
        Some(value)
    }

    fn read_u16(&mut self) -> Option<u16> {
        let bytes = self.data.get(self.pos..self.pos + 2)?;
        self.pos += 2;
        Some(u16::from_be_bytes([bytes[0], bytes[1]]))
    }

    fn read_i32(&mut self) -> Option<i32> {
        let bytes = self.data.get(self.pos..self.pos + 4)?;
        self.pos += 4;
        Some(i32::from_be_bytes([bytes[0], bytes[1], bytes[2], bytes[3]]))
    }

    fn read_i64(&mut self) -> Option<i64> {
        let bytes = self.data.get(self.pos..self.pos + 8)?;
        self.pos += 8;
        Some(i64::from_be_bytes([
            bytes[0], bytes[1], bytes[2], bytes[3],
            bytes[4], bytes[5], bytes[6], bytes[7],
        ]))
    }

    fn read_string(&mut self) -> Option<String> {
        let len = self.read_u16()? as usize;
        let bytes = self.data.get(self.pos..self.pos + len)?;
        self.pos += len;
        std::str::from_utf8(bytes).ok().map(ToOwned::to_owned)
    }
}

fn encode_level_dat_nbt(level: &AlphaLevelDat) -> Option<Vec<u8>> {
    let level_name = if level.level_name.is_null() {
        "world".to_string()
    } else {
        unsafe { CStr::from_ptr(level.level_name) }.to_str().ok()?.to_string()
    };

    let mut raw = Vec::with_capacity(128);
    raw.push(10);
    write_string(&mut raw, "")?;
    raw.push(10);
    write_string(&mut raw, "Data")?;
    write_tag_i64(&mut raw, "RandomSeed", level.random_seed)?;
    write_tag_i32(&mut raw, "SpawnX", level.spawn_x)?;
    write_tag_i32(&mut raw, "SpawnY", level.spawn_y)?;
    write_tag_i32(&mut raw, "SpawnZ", level.spawn_z)?;
    write_tag_i64(&mut raw, "Time", level.world_time)?;
    write_tag_i64(&mut raw, "SizeOnDisk", level.size_on_disk)?;
    write_tag_i32(&mut raw, "version", level.version)?;
    write_tag_string(&mut raw, "LevelName", &level_name)?;
    raw.push(0);
    raw.push(0);
    Some(raw)
}

fn decode_level_dat_nbt(input: &[u8]) -> Option<AlphaLevelDat> {
    let mut cursor = Cursor::new(input);
    if cursor.read_u8()? != 10 {
        return None;
    }
    let _root_name = cursor.read_string()?;
    if cursor.read_u8()? != 10 {
        return None;
    }
    if cursor.read_string()? != "Data" {
        return None;
    }

    let mut random_seed = 0_i64;
    let mut spawn_x = 0_i32;
    let mut spawn_y = 0_i32;
    let mut spawn_z = 0_i32;
    let mut world_time = 0_i64;
    let mut size_on_disk = 0_i64;
    let mut version = 19132_i32;
    let mut level_name = String::from("world");

    loop {
        let tag = cursor.read_u8()?;
        if tag == 0 {
            break;
        }

        let name = cursor.read_string()?;
        match tag {
            3 => {
                let value = cursor.read_i32()?;
                match name.as_str() {
                    "SpawnX" => spawn_x = value,
                    "SpawnY" => spawn_y = value,
                    "SpawnZ" => spawn_z = value,
                    "version" => version = value,
                    _ => {}
                }
            }
            4 => {
                let value = cursor.read_i64()?;
                match name.as_str() {
                    "RandomSeed" => random_seed = value,
                    "Time" => world_time = value,
                    "SizeOnDisk" => size_on_disk = value,
                    _ => {}
                }
            }
            8 => {
                let value = cursor.read_string()?;
                if name == "LevelName" {
                    level_name = value;
                }
            }
            _ => return None,
        }
    }

    if cursor.read_u8()? != 0 {
        return None;
    }

    let level_name = CString::new(level_name).ok()?;
    Some(AlphaLevelDat {
        random_seed,
        spawn_x,
        spawn_y,
        spawn_z,
        world_time,
        size_on_disk,
        version,
        level_name: level_name.into_raw(),
    })
}

fn copy_input(input: *const c_uchar, input_len: size_t) -> Option<Vec<u8>> {
    if input_len == 0 {
        return Some(Vec::new());
    }

    if input.is_null() {
        return None;
    }

    let bytes = unsafe { slice::from_raw_parts(input, input_len) };
    Some(bytes.to_vec())
}

#[no_mangle]
pub extern "C" fn alpha_buffer_free(buffer: AlphaBuffer) {
    if buffer.data.is_null() || buffer.len == 0 {
        return;
    }

    unsafe {
        drop(Vec::from_raw_parts(buffer.data, buffer.len, buffer.len));
    }
}

#[no_mangle]
pub extern "C" fn alpha_gzip_compress(
    input: *const c_uchar,
    input_len: size_t,
    level: c_int,
) -> AlphaBuffer {
    let Some(bytes) = copy_input(input, input_len) else {
        return AlphaBuffer::empty();
    };

    let compression = if level < 0 {
        Compression::default()
    } else {
        Compression::new(level.clamp(0, 9) as u32)
    };

    let mut encoder = GzEncoder::new(bytes.as_slice(), compression);
    let mut output = Vec::new();
    if encoder.read_to_end(&mut output).is_err() {
        return AlphaBuffer::empty();
    }

    AlphaBuffer::from_vec(output)
}

#[no_mangle]
pub extern "C" fn alpha_gzip_decompress(
    input: *const c_uchar,
    input_len: size_t,
) -> AlphaBuffer {
    let Some(bytes) = copy_input(input, input_len) else {
        return AlphaBuffer::empty();
    };

    let mut decoder = GzDecoder::new(bytes.as_slice());
    let mut output = Vec::new();
    if decoder.read_to_end(&mut output).is_err() {
        return AlphaBuffer::empty();
    }

    AlphaBuffer::from_vec(output)
}

#[no_mangle]
pub extern "C" fn alpha_zstd_compress(
    input: *const c_uchar,
    input_len: size_t,
    level: c_int,
) -> AlphaBuffer {
    let Some(bytes) = copy_input(input, input_len) else {
        return AlphaBuffer::empty();
    };

    let level = level.clamp(1, 22);
    match zstd::stream::encode_all(bytes.as_slice(), level) {
        Ok(output) => AlphaBuffer::from_vec(output),
        Err(_) => AlphaBuffer::empty(),
    }
}

#[no_mangle]
pub extern "C" fn alpha_zstd_decompress(
    input: *const c_uchar,
    input_len: size_t,
) -> AlphaBuffer {
    let Some(bytes) = copy_input(input, input_len) else {
        return AlphaBuffer::empty();
    };

    match zstd::stream::decode_all(bytes.as_slice()) {
        Ok(output) => AlphaBuffer::from_vec(output),
        Err(_) => AlphaBuffer::empty(),
    }
}

#[no_mangle]
pub extern "C" fn alpha_level_dat_encode(level: *const AlphaLevelDat) -> AlphaBuffer {
    if level.is_null() {
        return AlphaBuffer::empty();
    }

    let Some(raw_nbt) = encode_level_dat_nbt(unsafe { &*level }) else {
        return AlphaBuffer::empty();
    };

    let mut encoder = GzEncoder::new(raw_nbt.as_slice(), Compression::default());
    let mut output = Vec::new();
    if encoder.read_to_end(&mut output).is_err() {
        return AlphaBuffer::empty();
    }

    AlphaBuffer::from_vec(output)
}

#[no_mangle]
pub extern "C" fn alpha_level_dat_decode(
    input: *const c_uchar,
    input_len: size_t,
    out_level: *mut AlphaLevelDat,
) -> c_int {
    if out_level.is_null() {
        return 0;
    }

    let Some(bytes) = copy_input(input, input_len) else {
        return 0;
    };

    let mut decoder = GzDecoder::new(bytes.as_slice());
    let mut raw_nbt = Vec::new();
    if decoder.read_to_end(&mut raw_nbt).is_err() {
        return 0;
    }

    let Some(level) = decode_level_dat_nbt(&raw_nbt) else {
        return 0;
    };

    unsafe {
        *out_level = level;
    }
    1
}

#[no_mangle]
pub extern "C" fn alpha_level_dat_free(level: *mut AlphaLevelDat) {
    if level.is_null() {
        return;
    }

    let level = unsafe { &mut *level };
    if !level.level_name.is_null() {
        unsafe {
            drop(CString::from_raw(level.level_name as *mut c_char));
        }
        level.level_name = ptr::null();
    }
}
