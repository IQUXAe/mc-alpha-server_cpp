//! Item verbs ported from C++ `Item.cpp` (`ItemHoe/Seeds/FlintAndSteel/Sign/
//! Block/Boat::onItemUse`, mirrors Java `Item*`).
//!
//! Each verb runs its checks and world mutations through the shared
//! `ItemUseWorld` callback table; C++ keeps stack objects, the entity
//! table, packets, and TileEntities. Stack-count/damage bookkeeping stays
//! in C++ (it owns `ItemStack`); Rust reports outcomes the caller applies.
//!
//! Face-offset tables, soil rules, and growth constants are centralized
//! here so the six verbs share one tested source of truth.

use crate::item_use::{alpha_item_furnace_facing, alpha_item_sign_yaw_meta};
use crate::math_helper::{cos, sin};

/// World access for item verbs. Missing hooks fail the verb safely
/// (an all-None table refuses every verb).
#[repr(C)]
pub struct ItemUseWorld {
    pub next_int: Option<fn(bound: i32) -> i32>,
    pub next_f64_01: Option<fn() -> f64>,
    pub get_block_id: Option<fn(x: i32, y: i32, z: i32) -> u8>,
    pub set_block_notify: Option<fn(x: i32, y: i32, z: i32, id: u8) -> bool>,
    pub set_block_meta_notify: Option<fn(x: i32, y: i32, z: i32, id: u8, meta: u8) -> bool>,
    pub set_block_quiet: Option<fn(x: i32, y: i32, z: i32, id: u8) -> bool>,
    pub set_block_meta: Option<fn(x: i32, y: i32, z: i32, meta: u8)>,
    pub does_attach: Option<fn(x: i32, y: i32, z: i32) -> bool>,
    pub material_burning: Option<fn(x: i32, y: i32, z: i32) -> bool>,
    pub material_solid: Option<fn(x: i32, y: i32, z: i32) -> bool>,
    pub collidable_box: Option<fn(x: i32, y: i32, z: i32) -> bool>,
    pub block_can_stay: Option<fn(id: u8, x: i32, y: i32, z: i32) -> bool>,
    pub placement_clear: Option<fn(id: u8, x: i32, y: i32, z: i32) -> bool>,
    pub block_placed: Option<fn(id: u8, x: i32, y: i32, z: i32, side: i32)>,
    pub have_block: Option<fn(id: u8) -> bool>,
    pub spawn_item:
        Option<fn(item_id: i32, count: i32, damage: i32, fx: f64, fy: f64, fz: f64, mx: f64, my: f64, mz: f64)>,
    pub send_te_packet: Option<fn(x: i32, y: i32, z: i32)>,
    pub ray_trace:
        Option<fn(sx: f64, sy: f64, sz: f64, ex: f64, ey: f64, ez: f64, out_x: &mut i32, out_y: &mut i32, out_z: &mut i32) -> bool>,
}

const PLACE_DX: [i32; 6] = [0, 0, 0, 0, -1, 1];
const PLACE_DY: [i32; 6] = [-1, 1, 0, 0, 0, 0];
const PLACE_DZ: [i32; 6] = [0, 0, -1, 1, 0, 0];

fn place_offset(side: i32) -> Option<(i32, i32, i32)> {
    if side < 0 || side > 5 {
        return None;
    }
    let s = side as usize;
    Some((PLACE_DX[s], PLACE_DY[s], PLACE_DZ[s]))
}

fn q_id(w: &ItemUseWorld, x: i32, y: i32, z: i32) -> u8 {
    w.get_block_id.map(|f| f(x, y, z)).unwrap_or(0)
}
fn rng_int(w: &ItemUseWorld, bound: i32) -> i32 {
    w.next_int.map(|f| f(bound)).unwrap_or(0)
}
fn rng_f64(w: &ItemUseWorld) -> f64 {
    w.next_f64_01.map(|f| f()).unwrap_or(0.0)
}

/// Hoe tilling (mirrors `ItemHoe::onItemUse`): grass/dirt to soil, plus a
/// 1/8 seed drop on grass. Returns true when C++ must damage the stack
/// (and possibly destroy it when depleted).
pub fn item_hoe_use(
    world: &ItemUseWorld,
    seeds_id: i32,
    x: i32,
    y: i32,
    z: i32,
) -> bool {
    let w = world;
    let Some(set_notify) = w.set_block_notify else {
        return false;
    };
    let block_id = q_id(w, x, y, z);
    let cover = w.collidable_box.map(|f| f(x, y + 1, z)).unwrap_or(false);
    if (cover || block_id != 2) && block_id != 3 {
        return false;
    }
    if !set_notify(x, y, z, 60) {
        return false;
    }
    if block_id == 2 && seeds_id > 0 && rng_int(w, 8) == 0 {
        if let Some(spawn) = w.spawn_item {
            spawn(
                seeds_id,
                1,
                0,
                x as f64 + 0.5,
                y as f64 + 1.1,
                z as f64 + 0.5,
                rng_f64(w) * 0.1 - 0.05,
                0.12,
                rng_f64(w) * 0.1 - 0.05,
            );
        }
    }
    true
}

/// Seed planting (mirrors `ItemSeeds::onItemUse`). True means C++ must
/// decrement the stack.
pub fn item_seeds_use(world: &ItemUseWorld, x: i32, y: i32, z: i32, side: i32) -> bool {
    if side != 1 {
        return false;
    }
    let w = world;
    let Some(set_meta) = w.set_block_meta_notify else {
        return false;
    };
    if q_id(w, x, y, z) != 60 || q_id(w, x, y + 1, z) != 0 {
        return false;
    }
    set_meta(x, y + 1, z, 59, 0)
}

/// Flint and steel (mirrors `ItemFlintAndSteel::onItemUse`). On success
/// writes the new damage; `broke` tells C++ to zero the stack.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct FlintOut {
    pub placed: bool,
    pub new_damage: i32,
    pub broke: bool,
}

/// Flint and steel (mirrors Java `ItemFlintAndSteel.onItemUse`): side offset
/// 0=y-1,1=y+1,2=z-1,3=z+1,4=x-1,5=x+1; if the target cell is air, ignite it.
/// Always damages the stack by 1 and always consumes the event (returns true),
/// even when nothing ignited. `broke` follows `ItemStack.damageItem`: strict
/// `new_damage > max_damage` (65 uses at max 64).
pub fn item_flint_use(
    world: &ItemUseWorld,
    damage_in: i32,
    max_damage: i32,
    x: i32,
    y: i32,
    z: i32,
    side: i32,
    out: &mut FlintOut,
) -> bool {
    let w = world;
    let Some((dx, dy, dz)) = place_offset(side) else {
        // Invalid side: vanilla would still damage, but without a target cell
        // there is nothing to do — report no placement with damage applied.
        let new_damage = damage_in + 1;
        *out = FlintOut { placed: false, new_damage, broke: new_damage > max_damage };
        return true;
    };
    let (fx, fy, fz) = (x + dx, y + dy, z + dz);
    let mut placed = false;
    if q_id(w, fx, fy, fz) == 0 {
        placed = w.set_block_notify.map(|f| f(fx, fy, fz, 51)).unwrap_or(false);
    }
    let new_damage = damage_in + 1;
    *out = FlintOut { placed, new_damage, broke: new_damage > max_damage };
    true
}

/// Sign placement (mirrors `ItemSign::onItemUse`). True means C++ must
/// send the edit packet and decrement the stack.
pub fn item_sign_use(
    world: &ItemUseWorld,
    x: i32,
    y: i32,
    z: i32,
    side: i32,
    yaw: f32,
) -> bool {
    if side == 0 {
        return false;
    }
    let w = world;
    let solid = w.material_solid.map(|f| f(x, y, z)).unwrap_or(false);
    if !solid {
        return false;
    }
    let (mut tx, mut ty, mut tz) = (x, y, z);
    match side {
        1 => ty += 1,
        2 => tz -= 1,
        3 => tz += 1,
        4 => tx -= 1,
        5 => tx += 1,
        _ => return false,
    }
    if ty < 0 || ty >= 128 || q_id(w, tx, ty, tz) != 0 {
        return false;
    }
    if side == 1 {
        let meta = alpha_item_sign_yaw_meta(yaw);
        let ok = w.set_block_meta_notify.map(|f| f(tx, ty, tz, 63, meta)).unwrap_or(false);
        if !ok {
            return false;
        }
    } else if !(w.set_block_meta_notify.map(|f| f(tx, ty, tz, 68, side as u8)).unwrap_or(false)) {
        return false;
    }
    if let Some(send) = w.send_te_packet {
        send(tx, ty, tz);
    }
    true
}

/// Block placement (mirrors `ItemBlock::onItemUse`). True means C++ must
/// decrement the stack.
#[allow(clippy::too_many_arguments)]
pub fn item_block_use(
    world: &ItemUseWorld,
    block_id: u8,
    stack_count: i32,
    x: i32,
    y: i32,
    z: i32,
    side: i32,
    yaw: f32,
) -> bool {
    let w = world;
    let (mut tx, mut ty, mut tz) = (x, y, z);
    // Snow layers are replaced instead of offset.
    if q_id(w, x, y, z) != 78 {
        let Some((dx, dy, dz)) = place_offset(side) else {
            return false;
        };
        tx += dx;
        ty += dy;
        tz += dz;
    }
    if stack_count == 0 || ty < 0 || ty >= 128 {
        return false;
    }
    let target = q_id(w, tx, ty, tz);
    if target != 0 && !matches!(target, 8 | 9 | 10 | 11 | 51 | 78) {
        return false;
    }
    if !(w.have_block.map(|f| f(block_id)).unwrap_or(false)) {
        return false;
    }
    if !(w.block_can_stay.map(|f| f(block_id, tx, ty, tz)).unwrap_or(false)) {
        return false;
    }
    if !(w.placement_clear.map(|f| f(block_id, tx, ty, tz)).unwrap_or(false)) {
        return false;
    }
    if !(w.set_block_quiet.map(|f| f(tx, ty, tz, block_id)).unwrap_or(false)) {
        return false;
    }
    if block_id == 61 || block_id == 62 {
        let meta = alpha_item_furnace_facing(yaw);
        if let Some(f) = w.set_block_meta {
            f(tx, ty, tz, meta);
        }
    }
    if let Some(f) = w.block_placed {
        f(block_id, tx, ty, tz, side);
    }
    true
}

/// Boat look vector + eye start (mirrors `ItemBoat::onItemRightClick`
/// interpolation with `partialTick = 1.0`, then the 5-block ray).
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct BoatThrow {
    pub lx: f64,
    pub ly: f64,
    pub lz: f64,
    pub sx: f64,
    pub sy: f64,
    pub sz: f64,
    pub ex: f64,
    pub ey: f64,
    pub ez: f64,
}

pub fn item_boat_aim(
    prev_yaw: f32,
    yaw: f32,
    prev_pitch: f32,
    pitch: f32,
    prev_x: f64,
    x: f64,
    prev_y: f64,
    y: f64,
    prev_z: f64,
    z: f64,
    y_offset: f64,
    out: &mut BoatThrow,
) -> bool {
    // partialTick is constant 1.0: prev + (cur - prev) * 1.0, in f32/f64
    // exactly like C++.
    let iyaw = prev_yaw + (yaw - prev_yaw) * 1.0f32;
    let ipitch = prev_pitch + (pitch - prev_pitch) * 1.0f32;
    let sx = prev_x + (x - prev_x) * 1.0;
    let mut sy = prev_y + (y - prev_y) * 1.0;
    let sz = prev_z + (z - prev_z) * 1.0;
    sy += 1.62 - y_offset;
    let half_pi = std::f32::consts::PI / 180.0f32;
    let cos_yaw = cos(-iyaw * half_pi - std::f32::consts::PI);
    let sin_yaw = sin(-iyaw * half_pi - std::f32::consts::PI);
    let look_h = -cos(-ipitch * half_pi);
    let look_y = sin(-ipitch * half_pi);
    let (lx, lz) = (sin_yaw * look_h, cos_yaw * look_h);
    let (lx, ly, lz) = (lx as f64, look_y as f64, lz as f64);
    *out = BoatThrow {
        lx,
        ly,
        lz,
        sx,
        sy,
        sz,
        ex: sx + lx * 5.0,
        ey: sy + ly * 5.0,
        ez: sz + lz * 5.0,
    };
    true
}

/// Boat raycast resolution (mirrors the tail of `ItemBoat::onItemRightClick`).
/// Writes the hit cell and returns true when C++ must spawn the boat and
/// decrement the stack.
pub fn item_boat_throw(
    world: &ItemUseWorld,
    sx: f64,
    sy: f64,
    sz: f64,
    ex: f64,
    ey: f64,
    ez: f64,
    out_x: &mut i32,
    out_y: &mut i32,
    out_z: &mut i32,
) -> bool {
    let w = world;
    let Some(ray) = w.ray_trace else {
        return false;
    };
    let (mut hx, mut hy, mut hz) = (0, 0, 0);
    if !ray(sx, sy, sz, ex, ey, ez, &mut hx, &mut hy, &mut hz) {
        return false;
    }
    *out_x = hx;
    *out_y = hy;
    *out_z = hz;
    true
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashMap;
    use std::sync::{Mutex, MutexGuard};

    struct Fake {
        blocks: HashMap<(i32, i32, i32), (u8, u8)>,
        solid: HashMap<(i32, i32, i32), bool>,
        burning: HashMap<(i32, i32, i32), bool>,
        log: Vec<String>,
        int_script: Vec<i32>,
        int_pos: usize,
        hit: Option<(i32, i32, i32)>,
    }

    static FAKE: Mutex<Option<Fake>> = Mutex::new(None);

    fn fake() -> MutexGuard<'static, Option<Fake>> {
        match FAKE.lock() {
            Ok(g) => g,
            Err(poisoned) => poisoned.into_inner(),
        }
    }

    fn reset() {
        *fake() = Some(Fake {
            blocks: HashMap::new(),
            solid: HashMap::new(),
            burning: HashMap::new(),
            log: Vec::new(),
            int_script: Vec::new(),
            int_pos: 0,
            hit: None,
        });
    }

    fn s_next_int(bound: i32) -> i32 {
        let mut g = fake();
        let f = g.as_mut().unwrap_or_else(|| unreachable!());
        if f.int_pos < f.int_script.len() {
            let v = f.int_script[f.int_pos];
            f.int_pos += 1;
            v % bound
        } else {
            0
        }
    }
    fn s_next_f64() -> f64 {
        0.25
    }
    fn s_get_id(x: i32, y: i32, z: i32) -> u8 {
        fake().as_ref().and_then(|f| f.blocks.get(&(x, y, z)).map(|b| b.0)).unwrap_or(0)
    }
    fn s_set_notify(x: i32, y: i32, z: i32, id: u8) -> bool {
        if let Some(f) = fake().as_mut() {
            f.blocks.insert((x, y, z), (id, 0));
            f.log.push(format!("notify {x} {y} {z} {id}"));
        }
        true
    }
    fn s_set_meta_notify(x: i32, y: i32, z: i32, id: u8, meta: u8) -> bool {
        if let Some(f) = fake().as_mut() {
            f.blocks.insert((x, y, z), (id, meta));
            f.log.push(format!("metanotify {x} {y} {z} {id} {meta}"));
        }
        true
    }
    fn s_set_quiet(x: i32, y: i32, z: i32, id: u8) -> bool {
        if let Some(f) = fake().as_mut() {
            f.blocks.insert((x, y, z), (id, 0));
            f.log.push(format!("quiet {x} {y} {z} {id}"));
        }
        true
    }
    fn s_set_meta(x: i32, y: i32, z: i32, meta: u8) {
        if let Some(f) = fake().as_mut() {
            let id = f.blocks.get(&(x, y, z)).map(|b| b.0).unwrap_or(0);
            f.blocks.insert((x, y, z), (id, meta));
            f.log.push(format!("meta {x} {y} {z} {meta}"));
        }
    }
    fn s_attach(x: i32, y: i32, z: i32) -> bool {
        fake().as_ref().and_then(|f| f.solid.get(&(x, y, z)).copied()).unwrap_or(false)
    }
    fn s_burning(x: i32, y: i32, z: i32) -> bool {
        fake().as_ref().and_then(|f| f.burning.get(&(x, y, z)).copied()).unwrap_or(false)
    }
    fn s_solid(x: i32, y: i32, z: i32) -> bool {
        s_attach(x, y, z)
    }
    fn s_collidable(x: i32, y: i32, z: i32) -> bool {
        s_attach(x, y, z)
    }
    fn s_can_stay(_id: u8, _x: i32, _y: i32, _z: i32) -> bool {
        true
    }
    fn s_clear(_id: u8, _x: i32, _y: i32, _z: i32) -> bool {
        true
    }
    fn s_placed(_id: u8, _x: i32, _y: i32, _z: i32, _side: i32) {}
    fn s_have_block(_id: u8) -> bool {
        true
    }
    fn s_spawn(item: i32, count: i32, damage: i32, fx: f64, fy: f64, fz: f64, _mx: f64, _my: f64, _mz: f64) {
        if let Some(f) = fake().as_mut() {
            f.log.push(format!("spawn {item} {count} {damage} {fx:.1} {fy:.1} {fz:.1}"));
        }
    }
    fn s_te(x: i32, y: i32, z: i32) {
        if let Some(f) = fake().as_mut() {
            f.log.push(format!("te {x} {y} {z}"));
        }
    }
    fn s_ray(
        _sx: f64, _sy: f64, _sz: f64, _ex: f64, _ey: f64, _ez: f64,
        out_x: &mut i32, out_y: &mut i32, out_z: &mut i32,
    ) -> bool {
        let hit = fake().as_ref().and_then(|f| f.hit);
        match hit {
            Some((hx, hy, hz)) => {
                *out_x = hx;
                *out_y = hy;
                *out_z = hz;
                true
            }
            None => false,
        }
    }

    fn table() -> ItemUseWorld {
        ItemUseWorld {
            next_int: Some(s_next_int),
            next_f64_01: Some(s_next_f64),
            get_block_id: Some(s_get_id),
            set_block_notify: Some(s_set_notify),
            set_block_meta_notify: Some(s_set_meta_notify),
            set_block_quiet: Some(s_set_quiet),
            set_block_meta: Some(s_set_meta),
            does_attach: Some(s_attach),
            material_burning: Some(s_burning),
            material_solid: Some(s_solid),
            collidable_box: Some(s_collidable),
            block_can_stay: Some(s_can_stay),
            placement_clear: Some(s_clear),
            block_placed: Some(s_placed),
            have_block: Some(s_have_block),
            spawn_item: Some(s_spawn),
            send_te_packet: Some(s_te),
            ray_trace: Some(s_ray),
        }
    }

    fn logs() -> Vec<String> {
        fake().as_ref().map(|f| f.log.clone()).unwrap_or_default()
    }

    #[test]
    fn test_verb_scenarios() {
        let t = table();
        let tp = &t;

        // 1. Hoe tills grass to soil, drops a seed on a 1/8 roll.
        reset();
        let _ = fake().as_mut().map(|f| {
            let _ = f.blocks.insert((0, 64, 0), (2, 0));
        });
        assert!(item_hoe_use(tp, 295, 0, 64, 0));
        let l = logs();
        assert!(l.contains(&"notify 0 64 0 60".to_string()), "{l:?}");
        assert!(l.iter().any(|e| e.starts_with("spawn 295 1 0")), "{l:?}");

        // 2. Hoe refuses covered grass and stone.
        reset();
        let _ = fake().as_mut().map(|f| {
            let _ = f.blocks.insert((0, 64, 0), (2, 0));
            let _ = f.solid.insert((0, 65, 0), true);
        });
        assert!(!item_hoe_use(tp, 295, 0, 64, 0));
        reset();
        let _ = fake().as_mut().map(|f| {
            let _ = f.blocks.insert((0, 64, 0), (1, 0));
        });
        assert!(!item_hoe_use(tp, 295, 0, 64, 0));

        // 3. Seeds plant on soil with air above, only from the top face.
        reset();
        let _ = fake().as_mut().map(|f| {
            let _ = f.blocks.insert((0, 64, 0), (60, 0));
        });
        assert!(item_seeds_use(tp, 0, 64, 0, 1));
        assert!(logs().contains(&"metanotify 0 65 0 59 0".to_string()), "{:?}", logs());
        assert!(!item_seeds_use(tp, 0, 64, 0, 2));

        // 4. Flint: vanilla side map (3 => +z), always damages, always consumes.
        reset();
        let mut out = FlintOut { placed: false, new_damage: 0, broke: false };
        assert!(item_flint_use(tp, 3, 64, 0, 64, 0, 3, &mut out));
        assert!(out.placed && out.new_damage == 4 && !out.broke);
        assert!(logs().contains(&"notify 0 64 1 51".to_string()), "{:?}", logs());
        // Breaks strictly above max (65 uses at max 64).
        reset();
        let mut out = FlintOut { placed: false, new_damage: 0, broke: false };
        assert!(item_flint_use(tp, 64, 64, 0, 64, 0, 3, &mut out));
        assert!(out.broke && out.new_damage == 65);
        // Occupied target: no placement but still damages + consumes.
        reset();
        let _ = fake().as_mut().map(|f| {
            let _ = f.blocks.insert((0, 64, 1), (1, 0));
        });
        let mut out = FlintOut { placed: false, new_damage: 0, broke: false };
        assert!(item_flint_use(tp, 0, 64, 0, 64, 0, 3, &mut out));
        assert!(!out.placed && out.new_damage == 1);

        // 5. Sign post on solid ground takes yaw metadata; wall sign takes side.
        reset();
        let _ = fake().as_mut().map(|f| {
            let _ = f.solid.insert((0, 64, 0), true);
        });
        assert!(item_sign_use(tp, 0, 64, 0, 1, 0.0));
        assert!(logs().contains(&"metanotify 0 65 0 63 8".to_string()), "{:?}", logs());
        reset();
        let _ = fake().as_mut().map(|f| {
            let _ = f.solid.insert((0, 64, 0), true);
        });
        assert!(item_sign_use(tp, 0, 64, 0, 4, 0.0));
        assert!(logs().contains(&"metanotify -1 64 0 68 4".to_string()), "{:?}", logs());
        assert!(!item_sign_use(tp, 0, 64, 0, 0, 0.0));

        // 6. Block placement offsets by face, replaces snow, sets furnace facing.
        reset();
        let _ = fake().as_mut().map(|f| {
            let _ = f.blocks.insert((0, 64, 0), (1, 0));
        });
        assert!(item_block_use(tp, 5, 1, 0, 64, 0, 1, 0.0));
        assert!(logs().contains(&"quiet 0 65 0 5".to_string()), "{:?}", logs());
        // Occupied by stone: refused.
        reset();
        let _ = fake().as_mut().map(|f| {
            let _ = f.blocks.insert((0, 64, 0), (1, 0));
            let _ = f.blocks.insert((0, 65, 0), (1, 0));
        });
        assert!(!item_block_use(tp, 5, 1, 0, 64, 0, 1, 0.0));
        // Furnace gets yaw facing metadata.
        reset();
        let _ = fake().as_mut().map(|f| {
            let _ = f.blocks.insert((0, 64, 0), (1, 0));
        });
        assert!(item_block_use(tp, 61, 1, 0, 64, 0, 1, 90.0));
        assert!(logs().contains(&"meta 0 65 0 5".to_string()), "{:?}", logs());

        // 7. Boat aim + throw: miss keeps the stack, hit places.
        let mut aim = BoatThrow { lx: 0.0, ly: 0.0, lz: 0.0, sx: 0.0, sy: 0.0, sz: 0.0, ex: 0.0, ey: 0.0, ez: 0.0 };
        assert!(item_boat_aim(0.0, 0.0, 0.0, 0.0, 0.5, 0.5, 64.0, 64.0, 0.5, 0.5, 0.0, &mut aim));
        assert!((aim.sy - 65.62).abs() < 1e-9);
        reset();
        let (mut hx, mut hy, mut hz) = (0, 0, 0);
        assert!(!item_boat_throw(tp, aim.sx, aim.sy, aim.sz, aim.ex, aim.ey, aim.ez, &mut hx, &mut hy, &mut hz));
        let _ = fake().as_mut().map(|f| {
            f.hit = Some((3, 63, 7));
        });
        assert!(item_boat_throw(tp, aim.sx, aim.sy, aim.sz, aim.ex, aim.ey, aim.ez, &mut hx, &mut hy, &mut hz));
        assert_eq!((hx, hy, hz), (3, 63, 7));

        // 8. Missing table hooks fail everything safely (all-None table).
        let bare = ItemUseWorld {
            next_int: None,
            next_f64_01: None,
            get_block_id: None,
            set_block_notify: None,
            set_block_meta_notify: None,
            set_block_quiet: None,
            set_block_meta: None,
            does_attach: None,
            material_burning: None,
            material_solid: None,
            collidable_box: None,
            block_can_stay: None,
            placement_clear: None,
            block_placed: None,
            have_block: None,
            spawn_item: None,
            send_te_packet: None,
            ray_trace: None,
        };
        assert!(!item_hoe_use(&bare, 295, 0, 64, 0));
        assert!(!item_seeds_use(&bare, 0, 64, 0, 1));
        assert!(!item_sign_use(&bare, 0, 64, 0, 1, 0.0));
        assert!(!item_block_use(&bare, 5, 1, 0, 64, 0, 1, 0.0));
    }
}
