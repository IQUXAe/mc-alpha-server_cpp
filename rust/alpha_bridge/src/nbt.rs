use std::collections::BTreeMap;
use std::ffi::{CStr, CString};
use std::io::{Cursor, Read, Write};
use libc::{c_char, c_int, c_uchar, size_t};
use crate::AlphaBuffer;

#[derive(Debug, Clone, PartialEq)]
pub enum NbtTag {
    End,
    Byte(i8),
    Short(i16),
    Int(i32),
    Long(i64),
    Float(f32),
    Double(f64),
    ByteArray(Vec<u8>),
    String(String),
    List(NbtList),
    Compound(NbtCompound),
}

#[derive(Debug, Clone, PartialEq)]
pub struct NbtCompound {
    pub map: BTreeMap<String, NbtTag>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct NbtList {
    pub tag_type: u8,
    pub elements: Vec<NbtTag>,
}

unsafe fn c_to_str<'a>(ptr: *const c_char) -> &'a str {
    if ptr.is_null() {
        return "";
    }
    CStr::from_ptr(ptr).to_str().unwrap_or("")
}

pub fn read_tag_type<R: Read>(reader: &mut R) -> std::io::Result<u8> {
    let mut buf = [0u8; 1];
    reader.read_exact(&mut buf)?;
    Ok(buf[0])
}

pub fn write_tag_type<W: Write>(writer: &mut W, tag_type: u8) -> std::io::Result<()> {
    writer.write_all(&[tag_type])
}

pub fn read_string<R: Read>(reader: &mut R) -> std::io::Result<String> {
    let mut len_buf = [0u8; 2];
    reader.read_exact(&mut len_buf)?;
    let len = u16::from_be_bytes(len_buf) as usize;
    let mut bytes = vec![0u8; len];
    reader.read_exact(&mut bytes)?;
    String::from_utf8(bytes).map_err(|e| std::io::Error::new(std::io::ErrorKind::InvalidData, e))
}

pub fn write_string<W: Write>(writer: &mut W, s: &str) -> std::io::Result<()> {
    let bytes = s.as_bytes();
    let len = bytes.len() as u16;
    writer.write_all(&len.to_be_bytes())?;
    writer.write_all(bytes)
}

pub fn read_payload<R: Read>(reader: &mut R, tag_type: u8) -> std::io::Result<NbtTag> {
    match tag_type {
        0 => Ok(NbtTag::End),
        1 => {
            let mut buf = [0u8; 1];
            reader.read_exact(&mut buf)?;
            Ok(NbtTag::Byte(buf[0] as i8))
        }
        2 => {
            let mut buf = [0u8; 2];
            reader.read_exact(&mut buf)?;
            Ok(NbtTag::Short(i16::from_be_bytes(buf)))
        }
        3 => {
            let mut buf = [0u8; 4];
            reader.read_exact(&mut buf)?;
            Ok(NbtTag::Int(i32::from_be_bytes(buf)))
        }
        4 => {
            let mut buf = [0u8; 8];
            reader.read_exact(&mut buf)?;
            Ok(NbtTag::Long(i64::from_be_bytes(buf)))
        }
        5 => {
            let mut buf = [0u8; 4];
            reader.read_exact(&mut buf)?;
            Ok(NbtTag::Float(f32::from_be_bytes(buf)))
        }
        6 => {
            let mut buf = [0u8; 8];
            reader.read_exact(&mut buf)?;
            Ok(NbtTag::Double(f64::from_be_bytes(buf)))
        }
        7 => {
            let mut len_buf = [0u8; 4];
            reader.read_exact(&mut len_buf)?;
            let len = i32::from_be_bytes(len_buf);
            if len < 0 {
                return Err(std::io::Error::new(std::io::ErrorKind::InvalidData, "negative byte array length"));
            }
            let mut bytes = vec![0u8; len as usize];
            reader.read_exact(&mut bytes)?;
            Ok(NbtTag::ByteArray(bytes))
        }
        8 => {
            let s = read_string(reader)?;
            Ok(NbtTag::String(s))
        }
        9 => {
            let mut type_buf = [0u8; 1];
            reader.read_exact(&mut type_buf)?;
            let tag_type = type_buf[0];
            let mut len_buf = [0u8; 4];
            reader.read_exact(&mut len_buf)?;
            let len = i32::from_be_bytes(len_buf);
            if len < 0 {
                return Err(std::io::Error::new(std::io::ErrorKind::InvalidData, "negative list length"));
            }
            let mut elements = Vec::with_capacity(len as usize);
            for _ in 0..len {
                elements.push(read_payload(reader, tag_type)?);
            }
            Ok(NbtTag::List(NbtList { tag_type, elements }))
        }
        10 => {
            let mut map = BTreeMap::new();
            loop {
                let mut type_buf = [0u8; 1];
                reader.read_exact(&mut type_buf)?;
                let tag_type = type_buf[0];
                if tag_type == 0 {
                    break;
                }
                let name = read_string(reader)?;
                let val = read_payload(reader, tag_type)?;
                map.insert(name, val);
            }
            Ok(NbtTag::Compound(NbtCompound { map }))
        }
        _ => Err(std::io::Error::new(std::io::ErrorKind::InvalidData, format!("unknown tag type {}", tag_type))),
    }
}

pub fn write_payload<W: Write>(writer: &mut W, tag: &NbtTag) -> std::io::Result<()> {
    match tag {
        NbtTag::End => Ok(()),
        NbtTag::Byte(val) => writer.write_all(&[*val as u8]),
        NbtTag::Short(val) => writer.write_all(&val.to_be_bytes()),
        NbtTag::Int(val) => writer.write_all(&val.to_be_bytes()),
        NbtTag::Long(val) => writer.write_all(&val.to_be_bytes()),
        NbtTag::Float(val) => writer.write_all(&val.to_be_bytes()),
        NbtTag::Double(val) => writer.write_all(&val.to_be_bytes()),
        NbtTag::ByteArray(bytes) => {
            let len = bytes.len() as i32;
            writer.write_all(&len.to_be_bytes())?;
            writer.write_all(bytes)
        }
        NbtTag::String(s) => write_string(writer, s),
        NbtTag::List(list) => {
            writer.write_all(&[list.tag_type])?;
            let len = list.elements.len() as i32;
            writer.write_all(&len.to_be_bytes())?;
            for elem in &list.elements {
                write_payload(writer, elem)?;
            }
            Ok(())
        }
        NbtTag::Compound(comp) => {
            for (name, tag) in &comp.map {
                let tag_type = match tag {
                    NbtTag::End => 0,
                    NbtTag::Byte(_) => 1,
                    NbtTag::Short(_) => 2,
                    NbtTag::Int(_) => 3,
                    NbtTag::Long(_) => 4,
                    NbtTag::Float(_) => 5,
                    NbtTag::Double(_) => 6,
                    NbtTag::ByteArray(_) => 7,
                    NbtTag::String(_) => 8,
                    NbtTag::List(_) => 9,
                    NbtTag::Compound(_) => 10,
                };
                writer.write_all(&[tag_type])?;
                write_string(writer, name)?;
                write_payload(writer, tag)?;
            }
            writer.write_all(&[0]) // TAG_End
        }
    }
}

pub fn read_root<R: Read>(reader: &mut R) -> std::io::Result<(String, NbtCompound)> {
    let mut type_buf = [0u8; 1];
    reader.read_exact(&mut type_buf)?;
    if type_buf[0] != 10 {
        return Err(std::io::Error::new(std::io::ErrorKind::InvalidData, "root must be TAG_Compound"));
    }
    let name = read_string(reader)?;
    let tag = read_payload(reader, 10)?;
    if let NbtTag::Compound(comp) = tag {
        Ok((name, comp))
    } else {
        Err(std::io::Error::new(std::io::ErrorKind::InvalidData, "expected NbtCompound"))
    }
}

pub fn write_root<W: Write>(writer: &mut W, name: &str, comp: &NbtCompound) -> std::io::Result<()> {
    writer.write_all(&[10])?;
    write_string(writer, name)?;
    write_payload(writer, &NbtTag::Compound(comp.clone()))
}

// FFI Implementation

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_compound_create() -> *mut NbtCompound {
    Box::into_raw(Box::new(NbtCompound { map: BTreeMap::new() }))
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_compound_free(ptr: *mut NbtCompound) {
    if !ptr.is_null() {
        let _ = Box::from_raw(ptr);
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_compound_set_byte(comp: *mut NbtCompound, name: *const c_char, val: i8) {
    if !comp.is_null() {
        let comp = &mut *comp;
        comp.map.insert(c_to_str(name).to_string(), NbtTag::Byte(val));
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_compound_set_short(comp: *mut NbtCompound, name: *const c_char, val: i16) {
    if !comp.is_null() {
        let comp = &mut *comp;
        comp.map.insert(c_to_str(name).to_string(), NbtTag::Short(val));
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_compound_set_int(comp: *mut NbtCompound, name: *const c_char, val: i32) {
    if !comp.is_null() {
        let comp = &mut *comp;
        comp.map.insert(c_to_str(name).to_string(), NbtTag::Int(val));
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_compound_set_long(comp: *mut NbtCompound, name: *const c_char, val: i64) {
    if !comp.is_null() {
        let comp = &mut *comp;
        comp.map.insert(c_to_str(name).to_string(), NbtTag::Long(val));
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_compound_set_float(comp: *mut NbtCompound, name: *const c_char, val: f32) {
    if !comp.is_null() {
        let comp = &mut *comp;
        comp.map.insert(c_to_str(name).to_string(), NbtTag::Float(val));
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_compound_set_double(comp: *mut NbtCompound, name: *const c_char, val: f64) {
    if !comp.is_null() {
        let comp = &mut *comp;
        comp.map.insert(c_to_str(name).to_string(), NbtTag::Double(val));
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_compound_set_string(comp: *mut NbtCompound, name: *const c_char, val: *const c_char) {
    if !comp.is_null() {
        let comp = &mut *comp;
        comp.map.insert(c_to_str(name).to_string(), NbtTag::String(c_to_str(val).to_string()));
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_compound_set_byte_array(comp: *mut NbtCompound, name: *const c_char, val_ptr: *const u8, val_len: size_t) {
    if !comp.is_null() {
        let comp = &mut *comp;
        let vec = if val_ptr.is_null() || val_len == 0 {
            Vec::new()
        } else {
            std::slice::from_raw_parts(val_ptr, val_len).to_vec()
        };
        comp.map.insert(c_to_str(name).to_string(), NbtTag::ByteArray(vec));
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_compound_set_compound(comp: *mut NbtCompound, name: *const c_char, child: *mut NbtCompound) {
    if !comp.is_null() && !child.is_null() {
        let comp = &mut *comp;
        let child = *Box::from_raw(child); // take ownership
        comp.map.insert(c_to_str(name).to_string(), NbtTag::Compound(child));
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_compound_set_list(comp: *mut NbtCompound, name: *const c_char, child: *mut NbtList) {
    if !comp.is_null() && !child.is_null() {
        let comp = &mut *comp;
        let child = *Box::from_raw(child); // take ownership
        comp.map.insert(c_to_str(name).to_string(), NbtTag::List(child));
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_compound_get_tag_type(comp: *const NbtCompound, name: *const c_char) -> u8 {
    if comp.is_null() {
        return 0;
    }
    let comp = &*comp;
    match comp.map.get(c_to_str(name)) {
        None => 0,
        Some(NbtTag::End) => 0,
        Some(NbtTag::Byte(_)) => 1,
        Some(NbtTag::Short(_)) => 2,
        Some(NbtTag::Int(_)) => 3,
        Some(NbtTag::Long(_)) => 4,
        Some(NbtTag::Float(_)) => 5,
        Some(NbtTag::Double(_)) => 6,
        Some(NbtTag::ByteArray(_)) => 7,
        Some(NbtTag::String(_)) => 8,
        Some(NbtTag::List(_)) => 9,
        Some(NbtTag::Compound(_)) => 10,
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_compound_get_byte(comp: *const NbtCompound, name: *const c_char) -> i8 {
    if comp.is_null() {
        return 0;
    }
    let comp = &*comp;
    match comp.map.get(c_to_str(name)) {
        Some(NbtTag::Byte(val)) => *val,
        _ => 0,
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_compound_get_short(comp: *const NbtCompound, name: *const c_char) -> i16 {
    if comp.is_null() {
        return 0;
    }
    let comp = &*comp;
    match comp.map.get(c_to_str(name)) {
        Some(NbtTag::Short(val)) => *val,
        _ => 0,
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_compound_get_int(comp: *const NbtCompound, name: *const c_char) -> i32 {
    if comp.is_null() {
        return 0;
    }
    let comp = &*comp;
    match comp.map.get(c_to_str(name)) {
        Some(NbtTag::Int(val)) => *val,
        _ => 0,
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_compound_get_long(comp: *const NbtCompound, name: *const c_char) -> i64 {
    if comp.is_null() {
        return 0;
    }
    let comp = &*comp;
    match comp.map.get(c_to_str(name)) {
        Some(NbtTag::Long(val)) => *val,
        _ => 0,
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_compound_get_float(comp: *const NbtCompound, name: *const c_char) -> f32 {
    if comp.is_null() {
        return 0.0;
    }
    let comp = &*comp;
    match comp.map.get(c_to_str(name)) {
        Some(NbtTag::Float(val)) => *val,
        _ => 0.0,
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_compound_get_double(comp: *const NbtCompound, name: *const c_char) -> f64 {
    if comp.is_null() {
        return 0.0;
    }
    let comp = &*comp;
    match comp.map.get(c_to_str(name)) {
        Some(NbtTag::Double(val)) => *val,
        _ => 0.0,
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_compound_get_string(comp: *const NbtCompound, name: *const c_char) -> AlphaBuffer {
    if comp.is_null() {
        return AlphaBuffer::empty();
    }
    let comp = &*comp;
    match comp.map.get(c_to_str(name)) {
        Some(NbtTag::String(s)) => {
            let c_str = CString::new(s.as_str()).unwrap_or_default();
            AlphaBuffer::from_vec(c_str.into_bytes_with_nul())
        }
        _ => AlphaBuffer::empty(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_compound_get_byte_array(comp: *const NbtCompound, name: *const c_char) -> AlphaBuffer {
    if comp.is_null() {
        return AlphaBuffer::empty();
    }
    let comp = &*comp;
    match comp.map.get(c_to_str(name)) {
        Some(NbtTag::ByteArray(vec)) => AlphaBuffer::from_vec(vec.clone()),
        _ => AlphaBuffer::empty(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_compound_get_compound(comp: *const NbtCompound, name: *const c_char) -> *mut NbtCompound {
    if comp.is_null() {
        return std::ptr::null_mut();
    }
    let comp = &*comp;
    match comp.map.get(c_to_str(name)) {
        Some(NbtTag::Compound(child)) => Box::into_raw(Box::new(child.clone())),
        _ => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_compound_get_list(comp: *const NbtCompound, name: *const c_char) -> *mut NbtList {
    if comp.is_null() {
        return std::ptr::null_mut();
    }
    let comp = &*comp;
    match comp.map.get(c_to_str(name)) {
        Some(NbtTag::List(child)) => Box::into_raw(Box::new(child.clone())),
        _ => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_compound_get_keys(comp: *const NbtCompound) -> AlphaBuffer {
    if comp.is_null() {
        return AlphaBuffer::empty();
    }
    let comp = &*comp;
    let mut out = Vec::new();
    for key in comp.map.keys() {
        let c_str = CString::new(key.as_str()).unwrap_or_default();
        out.extend_from_slice(c_str.as_bytes_with_nul());
    }
    AlphaBuffer::from_vec(out)
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_list_create(tag_type: u8) -> *mut NbtList {
    Box::into_raw(Box::new(NbtList { tag_type, elements: Vec::new() }))
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_list_free(ptr: *mut NbtList) {
    if !ptr.is_null() {
        let _ = Box::from_raw(ptr);
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_list_get_type(list: *const NbtList) -> u8 {
    if list.is_null() {
        return 0;
    }
    (*list).tag_type
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_list_get_size(list: *const NbtList) -> size_t {
    if list.is_null() {
        return 0;
    }
    (*list).elements.len()
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_list_add_byte(list: *mut NbtList, val: i8) {
    if !list.is_null() {
        let list = &mut *list;
        list.elements.push(NbtTag::Byte(val));
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_list_add_short(list: *mut NbtList, val: i16) {
    if !list.is_null() {
        let list = &mut *list;
        list.elements.push(NbtTag::Short(val));
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_list_add_int(list: *mut NbtList, val: i32) {
    if !list.is_null() {
        let list = &mut *list;
        list.elements.push(NbtTag::Int(val));
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_list_add_long(list: *mut NbtList, val: i64) {
    if !list.is_null() {
        let list = &mut *list;
        list.elements.push(NbtTag::Long(val));
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_list_add_float(list: *mut NbtList, val: f32) {
    if !list.is_null() {
        let list = &mut *list;
        list.elements.push(NbtTag::Float(val));
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_list_add_double(list: *mut NbtList, val: f64) {
    if !list.is_null() {
        let list = &mut *list;
        list.elements.push(NbtTag::Double(val));
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_list_add_string(list: *mut NbtList, val: *const c_char) {
    if !list.is_null() {
        let list = &mut *list;
        list.elements.push(NbtTag::String(c_to_str(val).to_string()));
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_list_add_byte_array(list: *mut NbtList, val_ptr: *const u8, val_len: size_t) {
    if !list.is_null() {
        let list = &mut *list;
        let vec = if val_ptr.is_null() || val_len == 0 {
            Vec::new()
        } else {
            std::slice::from_raw_parts(val_ptr, val_len).to_vec()
        };
        list.elements.push(NbtTag::ByteArray(vec));
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_list_add_compound(list: *mut NbtList, child: *mut NbtCompound) {
    if !list.is_null() && !child.is_null() {
        let list = &mut *list;
        let child = *Box::from_raw(child); // take ownership
        list.elements.push(NbtTag::Compound(child));
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_list_add_list(list: *mut NbtList, child: *mut NbtList) {
    if !list.is_null() && !child.is_null() {
        let list = &mut *list;
        let child = *Box::from_raw(child); // take ownership
        list.elements.push(NbtTag::List(child));
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_list_get_byte(list: *const NbtList, idx: size_t) -> i8 {
    if list.is_null() {
        return 0;
    }
    let list = &*list;
    match list.elements.get(idx) {
        Some(NbtTag::Byte(val)) => *val,
        _ => 0,
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_list_get_short(list: *const NbtList, idx: size_t) -> i16 {
    if list.is_null() {
        return 0;
    }
    let list = &*list;
    match list.elements.get(idx) {
        Some(NbtTag::Short(val)) => *val,
        _ => 0,
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_list_get_int(list: *const NbtList, idx: size_t) -> i32 {
    if list.is_null() {
        return 0;
    }
    let list = &*list;
    match list.elements.get(idx) {
        Some(NbtTag::Int(val)) => *val,
        _ => 0,
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_list_get_long(list: *const NbtList, idx: size_t) -> i64 {
    if list.is_null() {
        return 0;
    }
    let list = &*list;
    match list.elements.get(idx) {
        Some(NbtTag::Long(val)) => *val,
        _ => 0,
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_list_get_float(list: *const NbtList, idx: size_t) -> f32 {
    if list.is_null() {
        return 0.0;
    }
    let list = &*list;
    match list.elements.get(idx) {
        Some(NbtTag::Float(val)) => *val,
        _ => 0.0,
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_list_get_double(list: *const NbtList, idx: size_t) -> f64 {
    if list.is_null() {
        return 0.0;
    }
    let list = &*list;
    match list.elements.get(idx) {
        Some(NbtTag::Double(val)) => *val,
        _ => 0.0,
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_list_get_string(list: *const NbtList, idx: size_t) -> AlphaBuffer {
    if list.is_null() {
        return AlphaBuffer::empty();
    }
    let list = &*list;
    match list.elements.get(idx) {
        Some(NbtTag::String(s)) => {
            let c_str = CString::new(s.as_str()).unwrap_or_default();
            AlphaBuffer::from_vec(c_str.into_bytes_with_nul())
        }
        _ => AlphaBuffer::empty(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_list_get_byte_array(list: *const NbtList, idx: size_t) -> AlphaBuffer {
    if list.is_null() {
        return AlphaBuffer::empty();
    }
    let list = &*list;
    match list.elements.get(idx) {
        Some(NbtTag::ByteArray(vec)) => AlphaBuffer::from_vec(vec.clone()),
        _ => AlphaBuffer::empty(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_list_get_compound(list: *const NbtList, idx: size_t) -> *mut NbtCompound {
    if list.is_null() {
        return std::ptr::null_mut();
    }
    let list = &*list;
    match list.elements.get(idx) {
        Some(NbtTag::Compound(child)) => Box::into_raw(Box::new(child.clone())),
        _ => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_list_get_list(list: *const NbtList, idx: size_t) -> *mut NbtList {
    if list.is_null() {
        return std::ptr::null_mut();
    }
    let list = &*list;
    match list.elements.get(idx) {
        Some(NbtTag::List(child)) => Box::into_raw(Box::new(child.clone())),
        _ => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_compound_serialize_root(comp: *const NbtCompound, name: *const c_char) -> AlphaBuffer {
    if comp.is_null() {
        return AlphaBuffer::empty();
    }
    let comp = &*comp;
    let name = c_to_str(name);
    let mut out = Vec::new();
    if write_root(&mut out, name, comp).is_err() {
        return AlphaBuffer::empty();
    }
    AlphaBuffer::from_vec(out)
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_compound_deserialize_root(
    data: *const u8,
    len: size_t,
    out_name: *mut *mut c_char,
    out_bytes_read: *mut size_t,
) -> *mut NbtCompound {
    if data.is_null() || len == 0 {
        return std::ptr::null_mut();
    }
    let bytes = std::slice::from_raw_parts(data, len);
    let mut cursor = Cursor::new(bytes);
    match read_root(&mut cursor) {
        Ok((name, comp)) => {
            if !out_name.is_null() {
                let name_c = CString::new(name).unwrap_or_default();
                *out_name = name_c.into_raw();
            }
            if !out_bytes_read.is_null() {
                *out_bytes_read = cursor.position() as size_t;
            }
            Box::into_raw(Box::new(comp))
        }
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_nbt_free_name(name: *mut c_char) {
    if !name.is_null() {
        let _ = CString::from_raw(name);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_compound_roundtrip() {
        let mut comp = NbtCompound { map: BTreeMap::new() };
        comp.map.insert("byteVal".to_string(), NbtTag::Byte(127));
        comp.map.insert("shortVal".to_string(), NbtTag::Short(-30000));
        comp.map.insert("intVal".to_string(), NbtTag::Int(999999));
        comp.map.insert("strVal".to_string(), NbtTag::String("hello".to_string()));
        comp.map.insert("floatVal".to_string(), NbtTag::Float(1.5));
        comp.map.insert("doubleVal".to_string(), NbtTag::Double(2.5));

        let mut out = Vec::new();
        write_root(&mut out, "Root", &comp).unwrap();

        let mut cursor = Cursor::new(out);
        let (name, read_back) = read_root(&mut cursor).unwrap();

        assert_eq!(name, "Root");
        assert_eq!(read_back, comp);
    }
}
