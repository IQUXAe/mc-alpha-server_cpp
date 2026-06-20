use std::collections::BTreeMap;
use std::ffi::{CStr, CString};
use std::io::Read;
use std::path::PathBuf;
use libc::{c_char, c_int, size_t};
use crate::AlphaBuffer;
use crate::nbt::{NbtCompound, NbtList, NbtTag, read_root, write_root};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct FfiItemStack {
    pub stack_size: i32,
    pub animations_to_go: i32,
    pub item_id: i32,
    pub item_damage: i32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct FfiFurnaceState {
    pub slots: [FfiItemStack; 3],
    pub burn_time: i16,
    pub cook_time: i16,
    pub current_item_burn_time: i16,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct FfiChestState {
    pub slots: [FfiItemStack; 27],
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct FfiSignState {
    pub lines: [[u8; 16]; 4],
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct FfiTileEntityChestData {
    pub x: i32,
    pub y: i32,
    pub z: i32,
    pub state: FfiChestState,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct FfiTileEntityFurnaceData {
    pub x: i32,
    pub y: i32,
    pub z: i32,
    pub state: FfiFurnaceState,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct FfiTileEntitySignData {
    pub x: i32,
    pub y: i32,
    pub z: i32,
    pub state: FfiSignState,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct FfiEntityItemData {
    pub item_id: i32,
    pub count: i32,
    pub meta: i32,
    pub age: i32,
    pub delay: i32,
    pub x: f64,
    pub y: f64,
    pub z: f64,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct FfiEntityAnimalData {
    pub id: *const c_char,
    pub x: f64,
    pub y: f64,
    pub z: f64,
    pub motion_x: f64,
    pub motion_y: f64,
    pub motion_z: f64,
    pub rotation_yaw: f32,
    pub rotation_pitch: f32,
    pub health: i16,
    pub max_health: i16,
    pub saddled: bool,
    pub sheared: bool,
    pub egg_lay_time: i32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct FfiEntityMonsterData {
    pub id: *const c_char,
    pub x: f64,
    pub y: f64,
    pub z: f64,
    pub motion_x: f64,
    pub motion_y: f64,
    pub motion_z: f64,
    pub rotation_yaw: f32,
    pub rotation_pitch: f32,
    pub health: i16,
    pub max_health: i16,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct FfiEntityBoatData {
    pub x: f64,
    pub y: f64,
    pub z: f64,
    pub motion_x: f64,
    pub motion_y: f64,
    pub motion_z: f64,
    pub rotation_yaw: f32,
    pub rotation_pitch: f32,
    pub time_since_hit: i32,
    pub damage_taken: i32,
    pub forward_direction: i32,
}

#[repr(C)]
pub struct AlphaChunkData {
    pub x_pos: i32,
    pub z_pos: i32,
    pub last_update: i64,
    pub blocks: *mut u8,
    pub blocks_len: size_t,
    pub blocks_capacity: size_t,
    pub data: *mut u8,
    pub data_len: size_t,
    pub data_capacity: size_t,
    pub sky_light: *mut u8,
    pub sky_light_len: size_t,
    pub sky_light_capacity: size_t,
    pub block_light: *mut u8,
    pub block_light_len: size_t,
    pub block_light_capacity: size_t,
    pub height_map: *mut u8,
    pub height_map_len: size_t,
    pub height_map_capacity: size_t,
    pub terrain_populated: bool,
    
    pub furnaces: *mut FfiTileEntityFurnaceData,
    pub furnaces_count: size_t,
    pub chests: *mut FfiTileEntityChestData,
    pub chests_count: size_t,
    pub signs: *mut FfiTileEntitySignData,
    pub signs_count: size_t,

    pub items: *mut FfiEntityItemData,
    pub items_count: size_t,
    
    pub animals: *mut FfiEntityAnimalData,
    pub animals_count: size_t,

    pub monsters: *mut FfiEntityMonsterData,
    pub monsters_count: size_t,

    pub boats: *mut FfiEntityBoatData,
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

#[no_mangle]
pub unsafe extern "C" fn alpha_chunk_data_free(data: *mut AlphaChunkData) {
    if data.is_null() { return; }
    let d = Box::from_raw(data);
    if !d.blocks.is_null() && d.blocks_capacity > 0 { let _ = Vec::from_raw_parts(d.blocks, d.blocks_len, d.blocks_capacity); }
    if !d.data.is_null() && d.data_capacity > 0 { let _ = Vec::from_raw_parts(d.data, d.data_len, d.data_capacity); }
    if !d.sky_light.is_null() && d.sky_light_capacity > 0 { let _ = Vec::from_raw_parts(d.sky_light, d.sky_light_len, d.sky_light_capacity); }
    if !d.block_light.is_null() && d.block_light_capacity > 0 { let _ = Vec::from_raw_parts(d.block_light, d.block_light_len, d.block_light_capacity); }
    if !d.height_map.is_null() && d.height_map_capacity > 0 { let _ = Vec::from_raw_parts(d.height_map, d.height_map_len, d.height_map_capacity); }
    
    alpha_chunk_data_free_except_arrays(Box::into_raw(d));
}

#[no_mangle]
pub unsafe extern "C" fn alpha_chunk_data_free_except_arrays(data: *mut AlphaChunkData) {
    if data.is_null() { return; }
    let d = Box::from_raw(data);
    
    if !d.furnaces.is_null() && d.furnaces_count > 0 { let _ = Vec::from_raw_parts(d.furnaces, d.furnaces_count, d.furnaces_count); }
    if !d.chests.is_null() && d.chests_count > 0 { let _ = Vec::from_raw_parts(d.chests, d.chests_count, d.chests_count); }
    if !d.signs.is_null() && d.signs_count > 0 { let _ = Vec::from_raw_parts(d.signs, d.signs_count, d.signs_count); }
    if !d.items.is_null() && d.items_count > 0 { let _ = Vec::from_raw_parts(d.items, d.items_count, d.items_count); }
    
    if !d.animals.is_null() && d.animals_count > 0 { 
        let list = Vec::from_raw_parts(d.animals, d.animals_count, d.animals_count); 
        for a in list {
            if !a.id.is_null() { let _ = CString::from_raw(a.id as *mut c_char); }
        }
    }
    
    if !d.monsters.is_null() && d.monsters_count > 0 { 
        let list = Vec::from_raw_parts(d.monsters, d.monsters_count, d.monsters_count); 
        for m in list {
            if !m.id.is_null() { let _ = CString::from_raw(m.id as *mut c_char); }
        }
    }
    
    if !d.boats.is_null() && d.boats_count > 0 { let _ = Vec::from_raw_parts(d.boats, d.boats_count, d.boats_count); }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_chunk_alloc_arrays(
    blocks: *mut *mut u8,
    data: *mut *mut u8,
    skylight: *mut *mut u8,
    blocklight: *mut *mut u8,
    height_map: *mut *mut u8,
) {
    if !blocks.is_null() {
        let mut v = vec![0u8; 32768];
        *blocks = v.as_mut_ptr(); std::mem::forget(v);
    }
    if !data.is_null() {
        let mut v = vec![0u8; 16384];
        *data = v.as_mut_ptr(); std::mem::forget(v);
    }
    if !skylight.is_null() {
        let mut v = vec![0u8; 16384];
        *skylight = v.as_mut_ptr(); std::mem::forget(v);
    }
    if !blocklight.is_null() {
        let mut v = vec![0u8; 16384];
        *blocklight = v.as_mut_ptr(); std::mem::forget(v);
    }
    if !height_map.is_null() {
        let mut v = vec![0u8; 256];
        *height_map = v.as_mut_ptr(); std::mem::forget(v);
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_chunk_free_arrays(
    blocks: *mut u8,
    data: *mut u8,
    skylight: *mut u8,
    blocklight: *mut u8,
    height_map: *mut u8,
) {
    if !blocks.is_null() { let _ = Vec::from_raw_parts(blocks, 32768, 32768); }
    if !data.is_null() { let _ = Vec::from_raw_parts(data, 16384, 16384); }
    if !skylight.is_null() { let _ = Vec::from_raw_parts(skylight, 16384, 16384); }
    if !blocklight.is_null() { let _ = Vec::from_raw_parts(blocklight, 16384, 16384); }
    if !height_map.is_null() { let _ = Vec::from_raw_parts(height_map, 256, 256); }
}

fn empty_item() -> FfiItemStack {
    FfiItemStack { stack_size: 0, animations_to_go: 0, item_id: 0, item_damage: 0 }
}

fn write_item_stack(stack: &FfiItemStack) -> NbtCompound {
    let mut map = BTreeMap::new();
    map.insert("id".to_string(), NbtTag::Short(stack.item_id as i16));
    map.insert("Count".to_string(), NbtTag::Byte(stack.stack_size as i8));
    map.insert("Damage".to_string(), NbtTag::Short(stack.item_damage as i16));
    NbtCompound { map }
}

fn read_item_stack(comp: &NbtCompound) -> FfiItemStack {
    let item_id = match comp.map.get("id") { Some(NbtTag::Short(v)) => *v as i32, _ => 0 };
    let stack_size = match comp.map.get("Count") { Some(NbtTag::Byte(v)) => *v as i32, _ => 0 };
    let item_damage = match comp.map.get("Damage") { Some(NbtTag::Short(v)) => *v as i32, _ => 0 };
    FfiItemStack { item_id, stack_size, item_damage, animations_to_go: 0 }
}

fn build_chunk_nbt_bytes(chunk_data: &AlphaChunkData) -> Option<Vec<u8>> {
    let mut level_map = BTreeMap::new();
    level_map.insert("xPos".to_string(), NbtTag::Int(chunk_data.x_pos));
    level_map.insert("zPos".to_string(), NbtTag::Int(chunk_data.z_pos));
    level_map.insert("LastUpdate".to_string(), NbtTag::Long(chunk_data.last_update));
    level_map.insert("Blocks".to_string(), NbtTag::ByteArray(unsafe { std::slice::from_raw_parts(chunk_data.blocks, chunk_data.blocks_len) }.to_vec()));
    level_map.insert("Data".to_string(), NbtTag::ByteArray(unsafe { std::slice::from_raw_parts(chunk_data.data, chunk_data.data_len) }.to_vec()));
    level_map.insert("SkyLight".to_string(), NbtTag::ByteArray(unsafe { std::slice::from_raw_parts(chunk_data.sky_light, chunk_data.sky_light_len) }.to_vec()));
    level_map.insert("BlockLight".to_string(), NbtTag::ByteArray(unsafe { std::slice::from_raw_parts(chunk_data.block_light, chunk_data.block_light_len) }.to_vec()));
    level_map.insert("HeightMap".to_string(), NbtTag::ByteArray(unsafe { std::slice::from_raw_parts(chunk_data.height_map, chunk_data.height_map_len) }.to_vec()));
    level_map.insert("TerrainPopulated".to_string(), NbtTag::Byte(if chunk_data.terrain_populated { 1 } else { 0 }));

    let mut te_list = Vec::new();
    if chunk_data.furnaces_count > 0 && !chunk_data.furnaces.is_null() {
        for f in unsafe { std::slice::from_raw_parts(chunk_data.furnaces, chunk_data.furnaces_count) } {
            let mut map = BTreeMap::new();
            map.insert("id".to_string(), NbtTag::String("Furnace".to_string()));
            map.insert("x".to_string(), NbtTag::Int(f.x));
            map.insert("y".to_string(), NbtTag::Int(f.y));
            map.insert("z".to_string(), NbtTag::Int(f.z));
            map.insert("BurnTime".to_string(), NbtTag::Short(f.state.burn_time));
            map.insert("CookTime".to_string(), NbtTag::Short(f.state.cook_time));
            map.insert("ItemBurnTime".to_string(), NbtTag::Short(f.state.current_item_burn_time));
            let mut items = Vec::new();
            for (i, slot) in f.state.slots.iter().enumerate() {
                if slot.stack_size > 0 {
                    let mut item_comp = write_item_stack(slot);
                    item_comp.map.insert("Slot".to_string(), NbtTag::Byte(i as i8));
                    items.push(NbtTag::Compound(item_comp));
                }
            }
            map.insert("Items".to_string(), NbtTag::List(NbtList { tag_type: 10, elements: items }));
            te_list.push(NbtTag::Compound(NbtCompound { map }));
        }
    }
    if chunk_data.chests_count > 0 && !chunk_data.chests.is_null() {
        for c in unsafe { std::slice::from_raw_parts(chunk_data.chests, chunk_data.chests_count) } {
            let mut map = BTreeMap::new();
            map.insert("id".to_string(), NbtTag::String("Chest".to_string()));
            map.insert("x".to_string(), NbtTag::Int(c.x));
            map.insert("y".to_string(), NbtTag::Int(c.y));
            map.insert("z".to_string(), NbtTag::Int(c.z));
            let mut items = Vec::new();
            for (i, slot) in c.state.slots.iter().enumerate() {
                if slot.stack_size > 0 {
                    let mut item_comp = write_item_stack(slot);
                    item_comp.map.insert("Slot".to_string(), NbtTag::Byte(i as i8));
                    items.push(NbtTag::Compound(item_comp));
                }
            }
            map.insert("Items".to_string(), NbtTag::List(NbtList { tag_type: 10, elements: items }));
            te_list.push(NbtTag::Compound(NbtCompound { map }));
        }
    }
    if chunk_data.signs_count > 0 && !chunk_data.signs.is_null() {
        for s in unsafe { std::slice::from_raw_parts(chunk_data.signs, chunk_data.signs_count) } {
            let mut map = BTreeMap::new();
            map.insert("id".to_string(), NbtTag::String("Sign".to_string()));
            map.insert("x".to_string(), NbtTag::Int(s.x));
            map.insert("y".to_string(), NbtTag::Int(s.y));
            map.insert("z".to_string(), NbtTag::Int(s.z));
            for (i, line) in s.state.lines.iter().enumerate() {
                let len = line.iter().position(|&c| c == 0).unwrap_or(16);
                let text = String::from_utf8_lossy(&line[..len]).to_string();
                map.insert(format!("Text{}", i + 1), NbtTag::String(text));
            }
            te_list.push(NbtTag::Compound(NbtCompound { map }));
        }
    }
    level_map.insert("TileEntities".to_string(), NbtTag::List(NbtList { tag_type: 10, elements: te_list }));

    let mut entity_list = Vec::new();
    if chunk_data.items_count > 0 && !chunk_data.items.is_null() {
        for item in unsafe { std::slice::from_raw_parts(chunk_data.items, chunk_data.items_count) } {
            let mut map = BTreeMap::new();
            map.insert("id".to_string(), NbtTag::String("Item".to_string()));
            map.insert("Pos".to_string(), NbtTag::List(NbtList { tag_type: 6, elements: vec![NbtTag::Double(item.x), NbtTag::Double(item.y), NbtTag::Double(item.z)] }));
            let mut it_map = BTreeMap::new();
            it_map.insert("id".to_string(), NbtTag::Short(item.item_id as i16));
            it_map.insert("Count".to_string(), NbtTag::Byte(item.count as i8));
            it_map.insert("Damage".to_string(), NbtTag::Short(item.meta as i16));
            map.insert("Item".to_string(), NbtTag::Compound(NbtCompound { map: it_map }));
            map.insert("Age".to_string(), NbtTag::Short(item.age as i16));
            map.insert("PickupDelay".to_string(), NbtTag::Short(item.delay as i16));
            entity_list.push(NbtTag::Compound(NbtCompound { map }));
        }
    }
    if chunk_data.animals_count > 0 && !chunk_data.animals.is_null() {
        for a in unsafe { std::slice::from_raw_parts(chunk_data.animals, chunk_data.animals_count) } {
            let mut map = BTreeMap::new();
            map.insert("id".to_string(), NbtTag::String(unsafe { c_to_str(a.id) }.to_string()));
            map.insert("Pos".to_string(), NbtTag::List(NbtList { tag_type: 6, elements: vec![NbtTag::Double(a.x), NbtTag::Double(a.y), NbtTag::Double(a.z)] }));
            map.insert("Motion".to_string(), NbtTag::List(NbtList { tag_type: 6, elements: vec![NbtTag::Double(a.motion_x), NbtTag::Double(a.motion_y), NbtTag::Double(a.motion_z)] }));
            map.insert("Rotation".to_string(), NbtTag::List(NbtList { tag_type: 5, elements: vec![NbtTag::Float(a.rotation_yaw), NbtTag::Float(a.rotation_pitch)] }));
            map.insert("Health".to_string(), NbtTag::Short(a.health));
            map.insert("MaxHealth".to_string(), NbtTag::Short(a.max_health));
            map.insert("Saddle".to_string(), NbtTag::Byte(if a.saddled { 1 } else { 0 }));
            map.insert("Sheared".to_string(), NbtTag::Byte(if a.sheared { 1 } else { 0 }));
            map.insert("EggLayTime".to_string(), NbtTag::Int(a.egg_lay_time));
            entity_list.push(NbtTag::Compound(NbtCompound { map }));
        }
    }
    if chunk_data.monsters_count > 0 && !chunk_data.monsters.is_null() {
        for m in unsafe { std::slice::from_raw_parts(chunk_data.monsters, chunk_data.monsters_count) } {
            let mut map = BTreeMap::new();
            map.insert("id".to_string(), NbtTag::String(unsafe { c_to_str(m.id) }.to_string()));
            map.insert("Pos".to_string(), NbtTag::List(NbtList { tag_type: 6, elements: vec![NbtTag::Double(m.x), NbtTag::Double(m.y), NbtTag::Double(m.z)] }));
            map.insert("Motion".to_string(), NbtTag::List(NbtList { tag_type: 6, elements: vec![NbtTag::Double(m.motion_x), NbtTag::Double(m.motion_y), NbtTag::Double(m.motion_z)] }));
            map.insert("Rotation".to_string(), NbtTag::List(NbtList { tag_type: 5, elements: vec![NbtTag::Float(m.rotation_yaw), NbtTag::Float(m.rotation_pitch)] }));
            map.insert("Health".to_string(), NbtTag::Short(m.health));
            map.insert("MaxHealth".to_string(), NbtTag::Short(m.max_health));
            entity_list.push(NbtTag::Compound(NbtCompound { map }));
        }
    }
    if chunk_data.boats_count > 0 && !chunk_data.boats.is_null() {
        for b in unsafe { std::slice::from_raw_parts(chunk_data.boats, chunk_data.boats_count) } {
            let mut map = BTreeMap::new();
            map.insert("id".to_string(), NbtTag::String("Boat".to_string()));
            map.insert("Pos".to_string(), NbtTag::List(NbtList { tag_type: 6, elements: vec![NbtTag::Double(b.x), NbtTag::Double(b.y), NbtTag::Double(b.z)] }));
            map.insert("Motion".to_string(), NbtTag::List(NbtList { tag_type: 6, elements: vec![NbtTag::Double(b.motion_x), NbtTag::Double(b.motion_y), NbtTag::Double(b.motion_z)] }));
            map.insert("Rotation".to_string(), NbtTag::List(NbtList { tag_type: 5, elements: vec![NbtTag::Float(b.rotation_yaw), NbtTag::Float(b.rotation_pitch)] }));
            map.insert("TimeSinceHit".to_string(), NbtTag::Int(b.time_since_hit));
            map.insert("DamageTaken".to_string(), NbtTag::Int(b.damage_taken));
            map.insert("ForwardDirection".to_string(), NbtTag::Int(b.forward_direction));
            entity_list.push(NbtTag::Compound(NbtCompound { map }));
        }
    }
    level_map.insert("Entities".to_string(), NbtTag::List(NbtList { tag_type: 10, elements: entity_list }));

    let level_compound = NbtCompound { map: level_map };
    let mut root_map = BTreeMap::new();
    root_map.insert("Level".to_string(), NbtTag::Compound(level_compound));
    let root_compound = NbtCompound { map: root_map };

    let mut raw_bytes = Vec::new();
    write_root(&mut raw_bytes, "", &root_compound).ok()?;
    Some(raw_bytes)
}

#[no_mangle]
pub unsafe extern "C" fn alpha_chunk_loader_save(
    world_dir: *const c_char,
    create_dirs: bool,
    chunk_data: *const AlphaChunkData,
) -> bool {
    if world_dir.is_null() || chunk_data.is_null() { return false; }
    let world_dir = c_to_str(world_dir);
    let chunk_data = &*chunk_data;
    let Some(raw_bytes) = build_chunk_nbt_bytes(chunk_data) else { return false; };

    let mut encoder = flate2::read::GzEncoder::new(raw_bytes.as_slice(), flate2::Compression::default());
    let mut compressed = Vec::new();
    if encoder.read_to_end(&mut compressed).is_err() { return false; }
    let chunk_file = get_chunk_file_path(world_dir, chunk_data.x_pos, chunk_data.z_pos);
    if create_dirs {
        if let Some(parent) = chunk_file.parent() {
            if std::fs::create_dir_all(parent).is_err() { return false; }
        }
    }
    let temp_file = format!("{}/tmp_chunk_{}_{}.dat", world_dir, chunk_data.x_pos, chunk_data.z_pos);
    if std::fs::write(&temp_file, compressed).is_err() { return false; }
    if std::fs::rename(temp_file, chunk_file).is_err() { return false; }
    true
}

#[no_mangle]
pub unsafe extern "C" fn alpha_chunk_nbt_serialize(
    chunk_data: *const AlphaChunkData,
    use_zstd: bool,
) -> AlphaBuffer {
    if chunk_data.is_null() { return AlphaBuffer::empty(); }
    let Some(raw_nbt) = build_chunk_nbt_bytes(&*chunk_data) else { return AlphaBuffer::empty(); };

    if use_zstd {
        match zstd::stream::encode_all(raw_nbt.as_slice(), 1) {
            Ok(out) => AlphaBuffer::from_vec(out),
            Err(_) => AlphaBuffer::empty(),
        }
    } else {
        let mut encoder = flate2::read::GzEncoder::new(raw_nbt.as_slice(), flate2::Compression::default());
        let mut out = Vec::new();
        if encoder.read_to_end(&mut out).is_err() { return AlphaBuffer::empty(); }
        AlphaBuffer::from_vec(out)
    }
}

unsafe fn parse_chunk_nbt(nbt_data: &[u8], chunk_x: i32, chunk_z: i32) -> Option<Box<AlphaChunkData>> {
    let mut cursor = std::io::Cursor::new(nbt_data);
    let Ok((_root_name, root)) = read_root(&mut cursor) else { return None; };
    let NbtTag::Compound(level) = root.map.get("Level")? else { return None; };

    let x_pos = match level.map.get("xPos") { Some(NbtTag::Int(val)) => *val, _ => chunk_x };
    let z_pos = match level.map.get("zPos") { Some(NbtTag::Int(val)) => *val, _ => chunk_z };
    let last_update = match level.map.get("LastUpdate") { Some(NbtTag::Long(val)) => *val, _ => 0 };

    let blocks_vec = match level.map.get("Blocks") { Some(NbtTag::ByteArray(v)) => v.clone(), _ => return None };
    let data_vec = match level.map.get("Data") { Some(NbtTag::ByteArray(v)) => v.clone(), _ => return None };
    let sky_vec = match level.map.get("SkyLight") { Some(NbtTag::ByteArray(v)) => v.clone(), _ => return None };
    let bl_vec = match level.map.get("BlockLight") { Some(NbtTag::ByteArray(v)) => v.clone(), _ => return None };
    let hm_vec = match level.map.get("HeightMap") { Some(NbtTag::ByteArray(v)) => v.clone(), _ => vec![0; 256] };

    let terrain_populated = match level.map.get("TerrainPopulated") { Some(NbtTag::Byte(val)) => *val != 0, _ => false };

    let mut furnaces = Vec::new();
    let mut chests = Vec::new();
    let mut signs = Vec::new();

    if let Some(NbtTag::List(list)) = level.map.get("TileEntities") {
        for elem in &list.elements {
            if let NbtTag::Compound(comp) = elem {
                let id = match comp.map.get("id") { Some(NbtTag::String(s)) => s.as_str(), _ => "" };
                let x = match comp.map.get("x") { Some(NbtTag::Int(v)) => *v, _ => 0 };
                let y = match comp.map.get("y") { Some(NbtTag::Int(v)) => *v, _ => 0 };
                let z = match comp.map.get("z") { Some(NbtTag::Int(v)) => *v, _ => 0 };
                match id {
                    "Furnace" => {
                        let mut state = FfiFurnaceState {
                            slots: [empty_item(), empty_item(), empty_item()],
                            burn_time: match comp.map.get("BurnTime") { Some(NbtTag::Short(v)) => *v, _ => 0 },
                            cook_time: match comp.map.get("CookTime") { Some(NbtTag::Short(v)) => *v, _ => 0 },
                            current_item_burn_time: match comp.map.get("ItemBurnTime") { Some(NbtTag::Short(v)) => *v, _ => 0 },
                        };
                        if let Some(NbtTag::List(items)) = comp.map.get("Items") {
                            for it in &items.elements {
                                if let NbtTag::Compound(item_comp) = it {
                                    let slot = match item_comp.map.get("Slot") { Some(NbtTag::Byte(v)) => *v as usize, _ => 0 };
                                    if slot < 3 { state.slots[slot] = read_item_stack(item_comp); }
                                }
                            }
                        }
                        furnaces.push(FfiTileEntityFurnaceData { x, y, z, state });
                    }
                    "Chest" => {
                        let mut state = FfiChestState { slots: [empty_item(); 27] };
                        if let Some(NbtTag::List(items)) = comp.map.get("Items") {
                            for it in &items.elements {
                                if let NbtTag::Compound(item_comp) = it {
                                    let slot = match item_comp.map.get("Slot") { Some(NbtTag::Byte(v)) => *v as usize, _ => 0 };
                                    if slot < 27 { state.slots[slot] = read_item_stack(item_comp); }
                                }
                            }
                        }
                        chests.push(FfiTileEntityChestData { x, y, z, state });
                    }
                    "Sign" => {
                        let mut state = FfiSignState { lines: [[0; 16]; 4] };
                        for i in 0..4 {
                            if let Some(NbtTag::String(text)) = comp.map.get(&format!("Text{}", i + 1)) {
                                let bytes = text.as_bytes();
                                let len = bytes.len().min(16);
                                state.lines[i][..len].copy_from_slice(&bytes[..len]);
                            }
                        }
                        signs.push(FfiTileEntitySignData { x, y, z, state });
                    }
                    _ => {}
                }
            }
        }
    }

    let mut items = Vec::new();
    let mut animals = Vec::new();
    let mut monsters = Vec::new();
    let mut boats = Vec::new();

    if let Some(NbtTag::List(list)) = level.map.get("Entities") {
        for elem in &list.elements {
            if let NbtTag::Compound(comp) = elem {
                let id = match comp.map.get("id") { Some(NbtTag::String(s)) => s.as_str(), _ => continue };

                let mut pos = [0.0; 3];
                if let Some(NbtTag::List(pos_list)) = comp.map.get("Pos") {
                    for (i, p) in pos_list.elements.iter().enumerate().take(3) {
                        if let NbtTag::Double(v) = p { pos[i] = *v; }
                    }
                }

                let mut motion = [0.0; 3];
                if let Some(NbtTag::List(motion_list)) = comp.map.get("Motion") {
                    for (i, m) in motion_list.elements.iter().enumerate().take(3) {
                        if let NbtTag::Double(v) = m { motion[i] = *v; }
                    }
                }

                let mut rotation = [0.0f32; 2];
                if let Some(NbtTag::List(rot_list)) = comp.map.get("Rotation") {
                    for (i, r) in rot_list.elements.iter().enumerate().take(2) {
                        if let NbtTag::Float(v) = r { rotation[i] = *v; }
                    }
                }

                let health = match comp.map.get("Health") { Some(NbtTag::Short(v)) => *v, _ => 20 };
                let max_health = match comp.map.get("MaxHealth") { Some(NbtTag::Short(v)) => *v, _ => 20 };

                match id {
                    "Item" => {
                        let mut item_id = 0; let mut count = 0; let mut meta = 0;
                        if let Some(NbtTag::Compound(item_comp)) = comp.map.get("Item") {
                            let stack = read_item_stack(item_comp);
                            item_id = stack.item_id; count = stack.stack_size; meta = stack.item_damage;
                        }
                        items.push(FfiEntityItemData {
                            item_id, count, meta,
                            age: match comp.map.get("Age") { Some(NbtTag::Short(v)) => *v as i32, _ => 0 },
                            delay: match comp.map.get("PickupDelay") { Some(NbtTag::Short(v)) => *v as i32, _ => 0 },
                            x: pos[0], y: pos[1], z: pos[2],
                        });
                    }
                    "Boat" => {
                        boats.push(FfiEntityBoatData {
                            x: pos[0], y: pos[1], z: pos[2],
                            motion_x: motion[0], motion_y: motion[1], motion_z: motion[2],
                            rotation_yaw: rotation[0], rotation_pitch: rotation[1],
                            time_since_hit: match comp.map.get("TimeSinceHit") { Some(NbtTag::Int(v)) => *v, _ => 0 },
                            damage_taken: match comp.map.get("DamageTaken") { Some(NbtTag::Int(v)) => *v, _ => 0 },
                            forward_direction: match comp.map.get("ForwardDirection") { Some(NbtTag::Int(v)) => *v, _ => 1 },
                        });
                    }
                    "Pig" | "Sheep" | "Cow" | "Chicken" => {
                        animals.push(FfiEntityAnimalData {
                            id: CString::new(id).unwrap().into_raw() as *const c_char,
                            x: pos[0], y: pos[1], z: pos[2],
                            motion_x: motion[0], motion_y: motion[1], motion_z: motion[2],
                            rotation_yaw: rotation[0], rotation_pitch: rotation[1],
                            health, max_health,
                            saddled: match comp.map.get("Saddle") { Some(NbtTag::Byte(v)) => *v != 0, _ => false },
                            sheared: match comp.map.get("Sheared") { Some(NbtTag::Byte(v)) => *v != 0, _ => false },
                            egg_lay_time: match comp.map.get("EggLayTime") { Some(NbtTag::Int(v)) => *v, _ => 0 },
                        });
                    }
                    "Zombie" | "Skeleton" | "Spider" | "Creeper" => {
                        monsters.push(FfiEntityMonsterData {
                            id: CString::new(id).unwrap().into_raw() as *const c_char,
                            x: pos[0], y: pos[1], z: pos[2],
                            motion_x: motion[0], motion_y: motion[1], motion_z: motion[2],
                            rotation_yaw: rotation[0], rotation_pitch: rotation[1],
                            health, max_health,
                        });
                    }
                    _ => {}
                }
            }
        }
    }

    let mut blocks_v = blocks_vec; let blocks_ptr = blocks_v.as_mut_ptr(); let blocks_len = blocks_v.len(); let blocks_cap = blocks_v.capacity(); std::mem::forget(blocks_v);
    let mut data_v = data_vec; let data_ptr = data_v.as_mut_ptr(); let data_len = data_v.len(); let data_cap = data_v.capacity(); std::mem::forget(data_v);
    let mut sky_v = sky_vec; let sky_ptr = sky_v.as_mut_ptr(); let sky_len = sky_v.len(); let sky_cap = sky_v.capacity(); std::mem::forget(sky_v);
    let mut bl_v = bl_vec; let bl_ptr = bl_v.as_mut_ptr(); let bl_len = bl_v.len(); let bl_cap = bl_v.capacity(); std::mem::forget(bl_v);
    let mut hm_v = hm_vec; let hm_ptr = hm_v.as_mut_ptr(); let hm_len = hm_v.len(); let hm_cap = hm_v.capacity(); std::mem::forget(hm_v);

    furnaces.shrink_to_fit(); let furnaces_count = furnaces.len(); let furnaces_ptr = furnaces.as_mut_ptr(); std::mem::forget(furnaces);
    chests.shrink_to_fit(); let chests_count = chests.len(); let chests_ptr = chests.as_mut_ptr(); std::mem::forget(chests);
    signs.shrink_to_fit(); let signs_count = signs.len(); let signs_ptr = signs.as_mut_ptr(); std::mem::forget(signs);
    items.shrink_to_fit(); let items_count = items.len(); let items_ptr = items.as_mut_ptr(); std::mem::forget(items);
    animals.shrink_to_fit(); let animals_count = animals.len(); let animals_ptr = animals.as_mut_ptr(); std::mem::forget(animals);
    monsters.shrink_to_fit(); let monsters_count = monsters.len(); let monsters_ptr = monsters.as_mut_ptr(); std::mem::forget(monsters);
    boats.shrink_to_fit(); let boats_count = boats.len(); let boats_ptr = boats.as_mut_ptr(); std::mem::forget(boats);

    Some(Box::new(AlphaChunkData {
        x_pos, z_pos, last_update,
        blocks: blocks_ptr, blocks_len, blocks_capacity: blocks_cap,
        data: data_ptr, data_len, data_capacity: data_cap,
        sky_light: sky_ptr, sky_light_len: sky_len, sky_light_capacity: sky_cap,
        block_light: bl_ptr, block_light_len: bl_len, block_light_capacity: bl_cap,
        height_map: hm_ptr, height_map_len: hm_len, height_map_capacity: hm_cap,
        terrain_populated,
        furnaces: furnaces_ptr, furnaces_count,
        chests: chests_ptr, chests_count,
        signs: signs_ptr, signs_count,
        items: items_ptr, items_count,
        animals: animals_ptr, animals_count,
        monsters: monsters_ptr, monsters_count,
        boats: boats_ptr, boats_count,
    }))
}

#[no_mangle]
pub unsafe extern "C" fn alpha_chunk_loader_load(
    world_dir: *const c_char,
    chunk_x: c_int,
    chunk_z: c_int,
) -> *mut AlphaChunkData {
    if world_dir.is_null() { return std::ptr::null_mut(); }
    let world_dir = c_to_str(world_dir);
    let path = get_chunk_file_path(world_dir, chunk_x, chunk_z);
    if !path.exists() { return std::ptr::null_mut(); }
    let Ok(compressed) = std::fs::read(&path) else { return std::ptr::null_mut(); };
    let mut decoder = flate2::read::GzDecoder::new(compressed.as_slice());
    let mut decompressed = Vec::new();
    if decoder.read_to_end(&mut decompressed).is_err() { return std::ptr::null_mut(); }
    match parse_chunk_nbt(&decompressed, chunk_x, chunk_z) {
        Some(out) => Box::into_raw(out),
        None => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_chunk_nbt_deserialize(
    data: *const u8,
    len: size_t,
    is_zstd: bool,
    chunk_x: i32,
    chunk_z: i32,
) -> *mut AlphaChunkData {
    if data.is_null() || len == 0 { return std::ptr::null_mut(); }
    let compressed = std::slice::from_raw_parts(data, len);

    let decompressed = if is_zstd {
        match zstd::stream::decode_all(compressed) {
            Ok(v) => v,
            Err(_) => return std::ptr::null_mut(),
        }
    } else {
        let mut decoder = flate2::read::GzDecoder::new(compressed);
        let mut out = Vec::new();
        if decoder.read_to_end(&mut out).is_err() { return std::ptr::null_mut(); }
        out
    };

    match parse_chunk_nbt(&decompressed, chunk_x, chunk_z) {
        Some(out) => Box::into_raw(out),
        None => std::ptr::null_mut(),
    }
}
