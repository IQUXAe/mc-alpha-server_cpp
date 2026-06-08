use std::collections::BTreeMap;
use std::ffi::{CStr, CString};
use std::io::{Cursor, Read, Write};
use std::path::PathBuf;
use libc::{c_char, c_int, size_t};
use crate::nbt::{NbtCompound, NbtList, NbtTag, read_root, write_root};

#[repr(C)]
pub struct AlphaChunkData {
    pub x_pos: i32,
    pub z_pos: i32,
    pub last_update: i64,
    pub blocks: *mut u8,
    pub blocks_len: size_t,
    pub data: *mut u8,
    pub data_len: size_t,
    pub sky_light: *mut u8,
    pub sky_light_len: size_t,
    pub block_light: *mut u8,
    pub block_light_len: size_t,
    pub height_map: *mut u8,
    pub height_map_len: size_t,
    pub terrain_populated: bool,
    pub tile_entities: *mut *mut NbtCompound,
    pub tile_entities_count: size_t,
    pub items: *mut *mut NbtCompound,
    pub items_count: size_t,
    pub animals: *mut *mut NbtCompound,
    pub animals_count: size_t,
    pub monsters: *mut *mut NbtCompound,
    pub monsters_count: size_t,
    pub boats: *mut *mut NbtCompound,
    pub boats_count: size_t,
}

unsafe fn c_to_str<'a>(ptr: *const c_char) -> &'a str {
    if ptr.is_null() {
        return "";
    }
    CStr::from_ptr(ptr).to_str().unwrap_or("")
}

pub fn to_base_36(num: i32) -> String {
    let mut n = num;
    if n == 0 {
        return "0".to_string();
    }
    let mut result = String::new();
    let negative = n < 0;
    if negative {
        n = -n;
    }
    while n > 0 {
        let rem = (n % 36) as u8;
        let char_code = if rem < 10 {
            b'0' + rem
        } else {
            b'a' + (rem - 10)
        };
        result.push(char_code as char);
        n /= 36;
    }
    let mut reversed: String = result.chars().rev().collect();
    if negative {
        reversed.insert(0, '-');
    }
    reversed
}

pub fn get_chunk_file_path(world_dir: &str, chunk_x: i32, chunk_z: i32) -> PathBuf {
    let file_name = format!("c.{}.{}.dat", to_base_36(chunk_x), to_base_36(chunk_z));
    let dir1 = to_base_36(chunk_x & 63);
    let dir2 = to_base_36(chunk_z & 63);
    PathBuf::from(world_dir).join(dir1).join(dir2).join(file_name)
}

unsafe fn build_nbt_list(ptrs: *mut *mut NbtCompound, count: size_t) -> NbtTag {
    if count > 0 && !ptrs.is_null() {
        let slice = std::slice::from_raw_parts(ptrs, count);
        let mut list = Vec::with_capacity(count);
        for &ptr in slice {
            if !ptr.is_null() {
                list.push(NbtTag::Compound((*ptr).clone()));
            }
        }
        NbtTag::List(NbtList {
            tag_type: 10,
            elements: list,
        })
    } else {
        NbtTag::List(NbtList {
            tag_type: 10,
            elements: Vec::new(),
        })
    }
}

fn load_nbt_list(level: &NbtCompound, key: &str) -> (*mut *mut NbtCompound, size_t) {
    let mut vec = Vec::new();
    if let Some(NbtTag::List(list)) = level.map.get(key) {
        if list.tag_type == 10 {
            for elem in &list.elements {
                if let NbtTag::Compound(comp) = elem {
                    let ptr = Box::into_raw(Box::new(comp.clone()));
                    vec.push(ptr);
                }
            }
        }
    }
    let count = vec.len();
    let mut vec = vec;
    vec.shrink_to_fit();
    let ptr = vec.as_mut_ptr();
    std::mem::forget(vec);
    (ptr, count)
}

#[no_mangle]
pub unsafe extern "C" fn alpha_chunk_data_free(data: *mut AlphaChunkData) {
    if data.is_null() {
        return;
    }
    let d = Box::from_raw(data);
    if !d.blocks.is_null() && d.blocks_len > 0 {
        let _ = Vec::from_raw_parts(d.blocks, d.blocks_len, d.blocks_len);
    }
    if !d.data.is_null() && d.data_len > 0 {
        let _ = Vec::from_raw_parts(d.data, d.data_len, d.data_len);
    }
    if !d.sky_light.is_null() && d.sky_light_len > 0 {
        let _ = Vec::from_raw_parts(d.sky_light, d.sky_light_len, d.sky_light_len);
    }
    if !d.block_light.is_null() && d.block_light_len > 0 {
        let _ = Vec::from_raw_parts(d.block_light, d.block_light_len, d.block_light_len);
    }
    if !d.height_map.is_null() && d.height_map_len > 0 {
        let _ = Vec::from_raw_parts(d.height_map, d.height_map_len, d.height_map_len);
    }
    
    let free_nbt_list = |ptrs: *mut *mut NbtCompound, count: size_t| {
        if !ptrs.is_null() && count > 0 {
            let list = Vec::from_raw_parts(ptrs, count, count);
            for &comp in &list {
                if !comp.is_null() {
                    let _ = Box::from_raw(comp);
                }
            }
        }
    };
    
    free_nbt_list(d.tile_entities, d.tile_entities_count);
    free_nbt_list(d.items, d.items_count);
    free_nbt_list(d.animals, d.animals_count);
    free_nbt_list(d.monsters, d.monsters_count);
    free_nbt_list(d.boats, d.boats_count);
}

#[no_mangle]
pub unsafe extern "C" fn alpha_chunk_loader_save(
    world_dir: *const c_char,
    create_dirs: bool,
    chunk_data: *const AlphaChunkData,
) -> bool {
    if world_dir.is_null() || chunk_data.is_null() {
        return false;
    }
    let world_dir = c_to_str(world_dir);
    let chunk_data = &*chunk_data;

    let mut level_map = BTreeMap::new();
    level_map.insert("xPos".to_string(), NbtTag::Int(chunk_data.x_pos));
    level_map.insert("zPos".to_string(), NbtTag::Int(chunk_data.z_pos));
    level_map.insert("LastUpdate".to_string(), NbtTag::Long(chunk_data.last_update));

    let blocks_vec = std::slice::from_raw_parts(chunk_data.blocks, chunk_data.blocks_len).to_vec();
    level_map.insert("Blocks".to_string(), NbtTag::ByteArray(blocks_vec));

    let data_vec = std::slice::from_raw_parts(chunk_data.data, chunk_data.data_len).to_vec();
    level_map.insert("Data".to_string(), NbtTag::ByteArray(data_vec));

    let sky_vec = std::slice::from_raw_parts(chunk_data.sky_light, chunk_data.sky_light_len).to_vec();
    level_map.insert("SkyLight".to_string(), NbtTag::ByteArray(sky_vec));

    let bl_vec = std::slice::from_raw_parts(chunk_data.block_light, chunk_data.block_light_len).to_vec();
    level_map.insert("BlockLight".to_string(), NbtTag::ByteArray(bl_vec));

    let hm_vec = std::slice::from_raw_parts(chunk_data.height_map, chunk_data.height_map_len).to_vec();
    level_map.insert("HeightMap".to_string(), NbtTag::ByteArray(hm_vec));

    level_map.insert(
        "TerrainPopulated".to_string(),
        NbtTag::Byte(if chunk_data.terrain_populated { 1 } else { 0 }),
    );

    level_map.insert("TileEntities".to_string(), build_nbt_list(chunk_data.tile_entities, chunk_data.tile_entities_count));
    level_map.insert("Items".to_string(), build_nbt_list(chunk_data.items, chunk_data.items_count));
    level_map.insert("Animals".to_string(), build_nbt_list(chunk_data.animals, chunk_data.animals_count));
    level_map.insert("Monsters".to_string(), build_nbt_list(chunk_data.monsters, chunk_data.monsters_count));
    level_map.insert("Boats".to_string(), build_nbt_list(chunk_data.boats, chunk_data.boats_count));

    let level_compound = NbtCompound { map: level_map };
    let mut root_map = BTreeMap::new();
    root_map.insert("Level".to_string(), NbtTag::Compound(level_compound));
    let root_compound = NbtCompound { map: root_map };

    let mut raw_bytes = Vec::new();
    if write_root(&mut raw_bytes, "", &root_compound).is_err() {
        return false;
    }

    let mut encoder = flate2::read::GzEncoder::new(raw_bytes.as_slice(), flate2::Compression::default());
    let mut compressed = Vec::new();
    if encoder.read_to_end(&mut compressed).is_err() {
        return false;
    }

    let chunk_file = get_chunk_file_path(world_dir, chunk_data.x_pos, chunk_data.z_pos);

    if create_dirs {
        if let Some(parent) = chunk_file.parent() {
            if std::fs::create_dir_all(parent).is_err() {
                return false;
            }
        }
    }

    let temp_file = format!("{}/tmp_chunk.dat", world_dir);
    if std::fs::write(&temp_file, compressed).is_err() {
        return false;
    }

    if std::fs::rename(temp_file, chunk_file).is_err() {
        return false;
    }

    true
}

#[no_mangle]
pub unsafe extern "C" fn alpha_chunk_loader_load(
    world_dir: *const c_char,
    chunk_x: c_int,
    chunk_z: c_int,
) -> *mut AlphaChunkData {
    if world_dir.is_null() {
        return std::ptr::null_mut();
    }
    let world_dir = c_to_str(world_dir);
    let path = get_chunk_file_path(world_dir, chunk_x, chunk_z);
    if !path.exists() {
        return std::ptr::null_mut();
    }

    let Ok(compressed) = std::fs::read(&path) else {
        return std::ptr::null_mut();
    };

    let mut decoder = flate2::read::GzDecoder::new(compressed.as_slice());
    let mut decompressed = Vec::new();
    if decoder.read_to_end(&mut decompressed).is_err() {
        return std::ptr::null_mut();
    }

    let mut cursor = std::io::Cursor::new(decompressed);
    let Ok((_root_name, root)) = read_root(&mut cursor) else {
        return std::ptr::null_mut();
    };

    let Some(NbtTag::Compound(level)) = root.map.get("Level") else {
        return std::ptr::null_mut();
    };

    let x_pos = match level.map.get("xPos") {
        Some(NbtTag::Int(val)) => *val,
        _ => chunk_x,
    };
    let z_pos = match level.map.get("zPos") {
        Some(NbtTag::Int(val)) => *val,
        _ => chunk_z,
    };
    let last_update = match level.map.get("LastUpdate") {
        Some(NbtTag::Long(val)) => *val,
        _ => 0,
    };

    let Some(NbtTag::ByteArray(blocks_vec)) = level.map.get("Blocks") else {
        return std::ptr::null_mut();
    };
    let Some(NbtTag::ByteArray(data_vec)) = level.map.get("Data") else {
        return std::ptr::null_mut();
    };
    let Some(NbtTag::ByteArray(sky_vec)) = level.map.get("SkyLight") else {
        return std::ptr::null_mut();
    };
    let Some(NbtTag::ByteArray(bl_vec)) = level.map.get("BlockLight") else {
        return std::ptr::null_mut();
    };
    let Some(NbtTag::ByteArray(hm_vec)) = level.map.get("HeightMap") else {
        return std::ptr::null_mut();
    };

    let terrain_populated = match level.map.get("TerrainPopulated") {
        Some(NbtTag::Byte(val)) => *val != 0,
        _ => false,
    };

    let (tile_entities_ptr, tile_entities_count) = load_nbt_list(level, "TileEntities");
    let (items_ptr, items_count) = load_nbt_list(level, "Items");
    let (animals_ptr, animals_count) = load_nbt_list(level, "Animals");
    let (monsters_ptr, monsters_count) = load_nbt_list(level, "Monsters");
    let (boats_ptr, boats_count) = load_nbt_list(level, "Boats");

    let mut blocks = blocks_vec.clone();
    blocks.shrink_to_fit();
    let blocks_ptr = blocks.as_mut_ptr();
    let blocks_len = blocks.len();
    std::mem::forget(blocks);

    let mut data = data_vec.clone();
    data.shrink_to_fit();
    let data_ptr = data.as_mut_ptr();
    let data_len = data.len();
    std::mem::forget(data);

    let mut sky = sky_vec.clone();
    sky.shrink_to_fit();
    let sky_ptr = sky.as_mut_ptr();
    let sky_len = sky.len();
    std::mem::forget(sky);

    let mut bl = bl_vec.clone();
    bl.shrink_to_fit();
    let bl_ptr = bl.as_mut_ptr();
    let bl_len = bl.len();
    std::mem::forget(bl);

    let mut hm = hm_vec.clone();
    hm.shrink_to_fit();
    let hm_ptr = hm.as_mut_ptr();
    let hm_len = hm.len();
    std::mem::forget(hm);

    let out = Box::new(AlphaChunkData {
        x_pos,
        z_pos,
        last_update,
        blocks: blocks_ptr,
        blocks_len,
        data: data_ptr,
        data_len,
        sky_light: sky_ptr,
        sky_light_len: sky_len,
        block_light: bl_ptr,
        block_light_len: bl_len,
        height_map: hm_ptr,
        height_map_len: hm_len,
        terrain_populated,
        tile_entities: tile_entities_ptr,
        tile_entities_count,
        items: items_ptr,
        items_count,
        animals: animals_ptr,
        animals_count,
        monsters: monsters_ptr,
        monsters_count,
        boats: boats_ptr,
        boats_count,
    });

    Box::into_raw(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_base36_conversions() {
        assert_eq!(to_base_36(0), "0");
        assert_eq!(to_base_36(5), "5");
        assert_eq!(to_base_36(10), "a");
        assert_eq!(to_base_36(-10), "-a");
        assert_eq!(to_base_36(35), "z");
        assert_eq!(to_base_36(36), "10");
        assert_eq!(to_base_36(-36), "-10");
    }

    #[test]
    fn test_chunk_paths() {
        let path = get_chunk_file_path("world", 10, -10);
        let path_str = path.to_str().unwrap().replace('\\', "/");
        assert!(path_str.ends_with("world/a/1i/c.a.-a.dat"), "Path was: {}", path_str);
    }
}
