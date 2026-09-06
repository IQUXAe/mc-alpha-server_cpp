use std::collections::BTreeMap;
use std::ffi::CStr;
use std::fs;
use std::io::{Cursor, Read, Write};
use std::path::Path;
use libc::{c_char, size_t};
use flate2::read::GzDecoder;
use flate2::write::GzEncoder;
use flate2::Compression;

use crate::nbt::{read_root, write_root, NbtCompound, NbtList, NbtTag};

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct FfiPlayerSlot {
    pub slot: u8,
    pub item_id: i16,
    pub count: i8,
    pub damage: i16,
}

#[repr(C)]
#[derive(Debug)]
pub struct AlphaPlayerData {
    pub pos_x: f64,
    pub pos_y: f64,
    pub pos_z: f64,
    pub motion_x: f64,
    pub motion_y: f64,
    pub motion_z: f64,
    pub rotation_yaw: f32,
    pub rotation_pitch: f32,
    pub fall_distance: f32,
    pub fire: i16,
    pub air: i16,
    pub on_ground: bool,
    pub health: i16,
    pub hurt_time: i16,
    pub death_time: i16,
    pub attack_time: i16,
    pub dimension: i32,
    pub score: i32,
    pub held_item_id: i32,
    pub slots: [FfiPlayerSlot; 64],
    pub slots_count: size_t,
}

impl Default for AlphaPlayerData {
    fn default() -> Self {
        Self {
            pos_x: 0.0,
            pos_y: 64.0,
            pos_z: 0.0,
            motion_x: 0.0,
            motion_y: 0.0,
            motion_z: 0.0,
            rotation_yaw: 0.0,
            rotation_pitch: 0.0,
            fall_distance: 0.0,
            fire: 0,
            air: 300,
            on_ground: false,
            health: 20,
            hurt_time: 0,
            death_time: 0,
            attack_time: 0,
            dimension: 0,
            score: 0,
            held_item_id: -1,
            slots: [FfiPlayerSlot::default(); 64],
            slots_count: 0,
        }
    }
}

pub fn save_player_data(filepath: &str, data: &AlphaPlayerData) -> bool {
    let mut root_map = BTreeMap::new();

    // Pos [Double; 3]
    let pos_list = NbtList {
        tag_type: 6, // TAG_Double
        elements: vec![
            NbtTag::Double(data.pos_x),
            NbtTag::Double(data.pos_y),
            NbtTag::Double(data.pos_z),
        ],
    };
    root_map.insert("Pos".to_string(), NbtTag::List(pos_list));

    // Also write legacy PosX/Y/Z for full backward compatibility
    root_map.insert("PosX".to_string(), NbtTag::Double(data.pos_x));
    root_map.insert("PosY".to_string(), NbtTag::Double(data.pos_y));
    root_map.insert("PosZ".to_string(), NbtTag::Double(data.pos_z));

    // Motion [Double; 3]
    let motion_list = NbtList {
        tag_type: 6, // TAG_Double
        elements: vec![
            NbtTag::Double(data.motion_x),
            NbtTag::Double(data.motion_y),
            NbtTag::Double(data.motion_z),
        ],
    };
    root_map.insert("Motion".to_string(), NbtTag::List(motion_list));

    // Rotation [Float; 2]
    let rot_list = NbtList {
        tag_type: 5, // TAG_Float
        elements: vec![
            NbtTag::Float(data.rotation_yaw),
            NbtTag::Float(data.rotation_pitch),
        ],
    };
    root_map.insert("Rotation".to_string(), NbtTag::List(rot_list));

    // Legacy RotationYaw/Pitch
    root_map.insert("RotationYaw".to_string(), NbtTag::Float(data.rotation_yaw));
    root_map.insert("RotationPitch".to_string(), NbtTag::Float(data.rotation_pitch));

    root_map.insert("FallDistance".to_string(), NbtTag::Float(data.fall_distance));
    root_map.insert("Fire".to_string(), NbtTag::Short(data.fire));
    root_map.insert("Air".to_string(), NbtTag::Short(data.air));
    root_map.insert("OnGround".to_string(), NbtTag::Byte(if data.on_ground { 1 } else { 0 }));

    root_map.insert("Health".to_string(), NbtTag::Short(data.health));
    root_map.insert("HurtTime".to_string(), NbtTag::Short(data.hurt_time));
    root_map.insert("DeathTime".to_string(), NbtTag::Short(data.death_time));
    root_map.insert("AttackTime".to_string(), NbtTag::Short(data.attack_time));

    root_map.insert("Dimension".to_string(), NbtTag::Int(data.dimension));
    root_map.insert("Score".to_string(), NbtTag::Int(data.score));
    root_map.insert("HeldItemId".to_string(), NbtTag::Int(data.held_item_id));

    // Inventory
    let count = data.slots_count.min(data.slots.len());
    let mut inv_elements = Vec::with_capacity(count);
    for slot_data in &data.slots[..count] {
        let mut item_map = BTreeMap::new();
        item_map.insert("Slot".to_string(), NbtTag::Byte(slot_data.slot as i8));
        item_map.insert("id".to_string(), NbtTag::Short(slot_data.item_id));
        item_map.insert("Count".to_string(), NbtTag::Byte(slot_data.count));
        item_map.insert("Damage".to_string(), NbtTag::Short(slot_data.damage));
        inv_elements.push(NbtTag::Compound(NbtCompound { map: item_map }));
    }
    let inv_list = NbtList {
        tag_type: 10, // TAG_Compound
        elements: inv_elements,
    };
    root_map.insert("Inventory".to_string(), NbtTag::List(inv_list));

    let root = NbtCompound { map: root_map };
    let mut raw_nbt = Vec::new();
    if write_root(&mut raw_nbt, "Player", &root).is_err() {
        return false;
    }

    // Compress with GZip
    let mut encoder = GzEncoder::new(Vec::new(), Compression::default());
    if encoder.write_all(&raw_nbt).is_err() {
        return false;
    }
    let Ok(compressed) = encoder.finish() else {
        return false;
    };

    let target_path = Path::new(filepath);
    if let Some(parent) = target_path.parent() {
        if !parent.exists() {
            let _ = fs::create_dir_all(parent);
        }
    }

    // Atomic write via _tmp_.dat (identical to Java PlayerNBTManager)
    let tmp_path = target_path.with_file_name(format!("_tmp_{}.dat", std::process::id()));
    if fs::write(&tmp_path, &compressed).is_err() {
        return false;
    }
    if fs::rename(&tmp_path, target_path).is_err() {
        let _ = fs::remove_file(&tmp_path);
        return fs::write(target_path, &compressed).is_ok();
    }
    true
}

pub fn load_player_data(filepath: &str, data: &mut AlphaPlayerData) -> bool {
    let path = Path::new(filepath);
    if !path.exists() {
        return false;
    }

    let Ok(file_bytes) = fs::read(path) else {
        return false;
    };
    if file_bytes.is_empty() {
        return false;
    }

    // Attempt GZip decompression; fallback to raw bytes if uncompressed
    let raw_bytes = {
        let mut gz = GzDecoder::new(&file_bytes[..]);
        let mut decompressed = Vec::new();
        if gz.read_to_end(&mut decompressed).is_ok() && !decompressed.is_empty() {
            decompressed
        } else {
            file_bytes
        }
    };

    let mut cursor = Cursor::new(raw_bytes);
    let Ok((_root_name, root)) = read_root(&mut cursor) else {
        return false;
    };

    // Position: check "Pos" list first, then legacy "PosX/Y/Z"
    if let Some(NbtTag::List(list)) = root.map.get("Pos") {
        if list.elements.len() >= 3 {
            if let NbtTag::Double(v) = list.elements[0] { data.pos_x = v; }
            if let NbtTag::Double(v) = list.elements[1] { data.pos_y = v; }
            if let NbtTag::Double(v) = list.elements[2] { data.pos_z = v; }
        }
    } else {
        if let Some(NbtTag::Double(v)) = root.map.get("PosX") { data.pos_x = *v; }
        if let Some(NbtTag::Double(v)) = root.map.get("PosY") { data.pos_y = *v; }
        if let Some(NbtTag::Double(v)) = root.map.get("PosZ") { data.pos_z = *v; }
    }

    // Motion: check "Motion" list
    if let Some(NbtTag::List(list)) = root.map.get("Motion") {
        if list.elements.len() >= 3 {
            if let NbtTag::Double(v) = list.elements[0] { data.motion_x = v; }
            if let NbtTag::Double(v) = list.elements[1] { data.motion_y = v; }
            if let NbtTag::Double(v) = list.elements[2] { data.motion_z = v; }
        }
    }

    // Rotation: check "Rotation" list, then legacy "RotationYaw/Pitch"
    if let Some(NbtTag::List(list)) = root.map.get("Rotation") {
        if list.elements.len() >= 2 {
            if let NbtTag::Float(v) = list.elements[0] { data.rotation_yaw = v; }
            if let NbtTag::Float(v) = list.elements[1] { data.rotation_pitch = v; }
        }
    } else {
        if let Some(NbtTag::Float(v)) = root.map.get("RotationYaw") { data.rotation_yaw = *v; }
        if let Some(NbtTag::Float(v)) = root.map.get("RotationPitch") { data.rotation_pitch = *v; }
    }

    if let Some(NbtTag::Float(v)) = root.map.get("FallDistance") { data.fall_distance = *v; }
    if let Some(NbtTag::Short(v)) = root.map.get("Fire") { data.fire = *v; }
    if let Some(NbtTag::Short(v)) = root.map.get("Air") { data.air = *v; }
    if let Some(NbtTag::Byte(v)) = root.map.get("OnGround") { data.on_ground = *v != 0; }

    if let Some(NbtTag::Short(v)) = root.map.get("Health") { data.health = *v; }
    if let Some(NbtTag::Short(v)) = root.map.get("HurtTime") { data.hurt_time = *v; }
    if let Some(NbtTag::Short(v)) = root.map.get("DeathTime") { data.death_time = *v; }
    if let Some(NbtTag::Short(v)) = root.map.get("AttackTime") { data.attack_time = *v; }

    if let Some(NbtTag::Int(v)) = root.map.get("Dimension") { data.dimension = *v; }
    if let Some(NbtTag::Int(v)) = root.map.get("Score") { data.score = *v; }
    if let Some(NbtTag::Int(v)) = root.map.get("HeldItemId") { data.held_item_id = *v; }

    // Inventory
    data.slots_count = 0;
    if let Some(NbtTag::List(list)) = root.map.get("Inventory") {
        for elem in &list.elements {
            if let NbtTag::Compound(item_comp) = elem {
                let slot = match item_comp.map.get("Slot") {
                    Some(NbtTag::Byte(b)) => *b as u8,
                    _ => continue,
                };
                let item_id = match item_comp.map.get("id") {
                    Some(NbtTag::Short(s)) => *s,
                    _ => 0,
                };
                let count = match item_comp.map.get("Count") {
                    Some(NbtTag::Byte(b)) => *b,
                    _ => 0,
                };
                let damage = match item_comp.map.get("Damage") {
                    Some(NbtTag::Short(s)) => *s,
                    _ => 0,
                };

                if data.slots_count < data.slots.len() {
                    data.slots[data.slots_count] = FfiPlayerSlot {
                        slot,
                        item_id,
                        count,
                        damage,
                    };
                    data.slots_count += 1;
                }
            }
        }
    }

    true
}

// C-ABI Exports
#[no_mangle]
pub unsafe extern "C" fn alpha_player_storage_save(
    filepath: *const c_char,
    data: *const AlphaPlayerData,
) -> bool {
    if filepath.is_null() || data.is_null() {
        return false;
    }
    let path = match CStr::from_ptr(filepath).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    save_player_data(path, &*data)
}

#[no_mangle]
pub unsafe extern "C" fn alpha_player_storage_load(
    filepath: *const c_char,
    out_data: *mut AlphaPlayerData,
) -> bool {
    if filepath.is_null() || out_data.is_null() {
        return false;
    }
    let path = match CStr::from_ptr(filepath).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    load_player_data(path, &mut *out_data)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_player_storage_roundtrip() {
        let mut player = AlphaPlayerData::default();
        player.pos_x = 12.5;
        player.pos_y = 65.0;
        player.pos_z = -44.25;
        player.rotation_yaw = 180.0;
        player.rotation_pitch = 45.0;
        player.health = 18;
        player.air = 250;
        player.held_item_id = 276; // Diamond sword

        player.slots[0] = FfiPlayerSlot { slot: 0, item_id: 276, count: 1, damage: 5 };
        player.slots[1] = FfiPlayerSlot { slot: 100, item_id: 310, count: 1, damage: 0 }; // Helmet
        player.slots_count = 2;

        let test_file = "/tmp/test_alpha_player.dat";
        assert!(save_player_data(test_file, &player));

        let mut loaded = AlphaPlayerData::default();
        assert!(load_player_data(test_file, &mut loaded));

        assert!((loaded.pos_x - 12.5).abs() < 1e-5);
        assert!((loaded.pos_y - 65.0).abs() < 1e-5);
        assert!((loaded.pos_z - (-44.25)).abs() < 1e-5);
        assert!((loaded.rotation_yaw - 180.0).abs() < 1e-5);
        assert!((loaded.rotation_pitch - 45.0).abs() < 1e-5);
        assert_eq!(loaded.health, 18);
        assert_eq!(loaded.air, 250);
        assert_eq!(loaded.held_item_id, 276);
        assert_eq!(loaded.slots_count, 2);
        assert_eq!(loaded.slots[0].slot, 0);
        assert_eq!(loaded.slots[0].item_id, 276);
        assert_eq!(loaded.slots[0].count, 1);
        assert_eq!(loaded.slots[0].damage, 5);
        assert_eq!(loaded.slots[1].slot, 100);
        assert_eq!(loaded.slots[1].item_id, 310);

        let _ = fs::remove_file(test_file);
    }
}
