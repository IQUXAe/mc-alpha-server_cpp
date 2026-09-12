//! Play-packet constructors, split out of `session.rs`.
//! Re-exported from `session` so `crate::session::pkt_*` keeps working.

use crate::inventory::FfiItemStack;
use crate::network::{put_f32, put_f64, put_i16, put_i32, put_i64, put_i8, put_str, put_u8};
use crate::world::TileData;

pub fn pkt_handshake(server_id: &str) -> Vec<u8> {
    let mut b = Vec::with_capacity(4 + server_id.len());
    put_u8(&mut b, 2);
    put_str(&mut b, server_id);
    b
}

pub fn pkt_kick(reason: &str) -> Vec<u8> {
    let mut b = Vec::with_capacity(4 + reason.len());
    put_u8(&mut b, 255);
    put_str(&mut b, reason);
    b
}

pub fn pkt_login_response(entity_id: i32, seed: i64, dimension: i8) -> Vec<u8> {
    let mut b = Vec::with_capacity(16);
    put_u8(&mut b, 1);
    put_i32(&mut b, entity_id);
    put_str(&mut b, "");
    put_str(&mut b, "");
    put_i64(&mut b, seed);
    put_i8(&mut b, dimension);
    b
}

pub fn pkt_chat(msg: &str) -> Vec<u8> {
    let mut b = Vec::with_capacity(4 + msg.len());
    put_u8(&mut b, 3);
    put_str(&mut b, msg);
    b
}

pub fn pkt_time(time: i64) -> Vec<u8> {
    let mut b = Vec::with_capacity(9);
    put_u8(&mut b, 4);
    put_i64(&mut b, time);
    b
}

pub fn pkt_spawn_pos(x: i32, y: i32, z: i32) -> Vec<u8> {
    let mut b = Vec::with_capacity(13);
    put_u8(&mut b, 6);
    put_i32(&mut b, x);
    put_i32(&mut b, y);
    put_i32(&mut b, z);
    b
}

pub fn pkt_health(health: i8) -> Vec<u8> {
    let mut b = Vec::with_capacity(2);
    put_u8(&mut b, 8);
    put_i8(&mut b, health);
    b
}

pub fn pkt_teleport(x: f64, y: f64, z: f64, yaw: f32, pitch: f32) -> Vec<u8> {
    let mut b = Vec::with_capacity(42);
    put_u8(&mut b, 13);
    put_f64(&mut b, x);
    put_f64(&mut b, y + 1.62f32 as f64);
    put_f64(&mut b, y);
    put_f64(&mut b, z);
    put_f32(&mut b, yaw);
    put_f32(&mut b, pitch);
    b.push(0);
    b
}

pub fn pkt_block_change(x: i32, y: i32, z: i32, block_type: u8, meta: u8) -> Vec<u8> {
    let mut b = Vec::with_capacity(12);
    put_u8(&mut b, 53);
    crate::network::put_i32(&mut b, x);
    put_i8(&mut b, y as i8);
    crate::network::put_i32(&mut b, z);
    put_u8(&mut b, block_type);
    put_u8(&mut b, meta);
    b
}

fn put_slot(buf: &mut Vec<u8>, s: Option<FfiItemStack>) {
    match s {
        Some(v) if v.stack_size > 0 => {
            put_i16(buf, v.item_id as i16);
            put_i8(buf, v.stack_size as i8);
            put_i16(buf, v.item_damage as i16);
        }
        // Vanilla writes empty slots as a bare -1 (2 bytes, no
        // count/damage tail). Anything longer desyncs the stream: the
        // client reads short-only for negative ids, plants phantom
        // Item(0)s into the following slots, and NPEs rendering the
        // hotbar (blocksList[0] is null). This crashed real clients
        // on every fresh login; synthetic tests never render.
        _ => {
            put_i16(buf, -1);
        }
    }
}

pub fn pkt_inventory_section(inv_type: i32, slots: &[Option<FfiItemStack>]) -> Vec<u8> {
    let mut b = Vec::with_capacity(7 + slots.len() * 5);
    put_u8(&mut b, 5);
    crate::network::put_i32(&mut b, inv_type);
    put_i16(&mut b, slots.len() as i16);
    for s in slots {
        put_slot(&mut b, *s);
    }
    b
}

pub fn pkt_tile_entity(x: i32, y: i32, z: i32, nbt_gz: &[u8]) -> Vec<u8> {
    let mut b = Vec::with_capacity(13 + nbt_gz.len());
    put_u8(&mut b, 59);
    crate::network::put_i32(&mut b, x);
    put_i16(&mut b, y as i16);
    crate::network::put_i32(&mut b, z);
    put_i16(&mut b, nbt_gz.len() as i16);
    b.extend_from_slice(nbt_gz);
    b
}

pub fn pkt_respawn() -> Vec<u8> {
    vec![9]
}

pub fn pkt_keepalive() -> Vec<u8> {
    vec![0]
}

pub fn pkt_arm(entity_id: i32, animate: i8) -> Vec<u8> {
    let mut b = Vec::with_capacity(6);
    put_u8(&mut b, 18);
    crate::network::put_i32(&mut b, entity_id);
    put_i8(&mut b, animate);
    b
}

pub fn pkt_pre_chunk(x: i32, z: i32, mode: bool) -> Vec<u8> {
    let mut b = Vec::with_capacity(10);
    put_u8(&mut b, 50);
    crate::network::put_i32(&mut b, x);
    crate::network::put_i32(&mut b, z);
    put_u8(&mut b, if mode { 1 } else { 0 });
    b
}

pub fn pkt_map_chunk(
    x: i32,
    y: i32,
    z: i32,
    size_x: i32,
    size_y: i32,
    size_z: i32,
    data: &[u8],
) -> Vec<u8> {
    let mut b = Vec::with_capacity(18 + data.len());
    put_u8(&mut b, 51);
    crate::network::put_i32(&mut b, x);
    put_i16(&mut b, y as i16);
    crate::network::put_i32(&mut b, z);
    put_u8(&mut b, (size_x - 1) as u8);
    put_u8(&mut b, (size_y - 1) as u8);
    put_u8(&mut b, (size_z - 1) as u8);
    crate::network::put_i32(&mut b, data.len() as i32);
    b.extend_from_slice(data);
    b
}

pub fn tile_packet(x: i32, y: i32, z: i32, tile: &TileData) -> Vec<u8> {
    use crate::nbt::write_root;
    use std::io::Write as _;
    // Root is the tile compound itself, like C++ writeRoot.
    let comp = crate::persist::tile_nbt(x, y, z, tile);
    let mut bytes = Vec::new();
    write_root(&mut bytes, "", &comp).ok();
    let mut enc = flate2::write::GzEncoder::new(Vec::new(), flate2::Compression::default());
    enc.write_all(&bytes).ok();
    let gz = enc.finish().ok().unwrap_or_default();
    pkt_tile_entity(x, y, z, &gz)
}
