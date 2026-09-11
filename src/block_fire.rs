//! Fire behavior ported from C++ `BlockFire` (mirrors Java `BlockFire`).
//!
//! Burn-rate tables live here as constants (from the old constructor's
//! `setBurnRate` calls); C++ keeps only the `Block` shell. Spread, aging,
//! and catching use the shared `BlockTickWorld` callback table from
//! `block_ticks` plus one TNT hook: detonating TNT calls back into C++
//! (`Block::blocksList[46]->onBlockDestroyedByPlayer`).

use crate::block_ticks::BlockTickWorld;

/// (block id, encourage chance, catch ability) from `BlockFire` ctor.
const BURN_RATES: [(u8, i32, i32); 6] = [
    (5, 5, 20),   // planks
    (17, 5, 5),   // wood (log)
    (18, 30, 60), // leaves
    (47, 30, 20), // bookshelf
    (46, 15, 100), // TNT
    (35, 30, 60), // cloth (wool)
];

fn encourage(id: u8) -> i32 {
    BURN_RATES.iter().find(|(b, _, _)| *b == id).map(|(_, c, _)| *c).unwrap_or(0)
}

fn ability(id: u8) -> i32 {
    BURN_RATES.iter().find(|(b, _, _)| *b == id).map(|(_, _, a)| *a).unwrap_or(0)
}

fn q_id(w: &BlockTickWorld, x: i32, y: i32, z: i32) -> u8 {
    w.get_block_id.map(|f| f(x, y, z)).unwrap_or(0)
}
fn q_meta(w: &BlockTickWorld, x: i32, y: i32, z: i32) -> u8 {
    w.get_block_meta.map(|f| f(x, y, z)).unwrap_or(0)
}
fn q_attach(w: &BlockTickWorld, x: i32, y: i32, z: i32) -> bool {
    w.attach_world.map(|f| f(x, y, z)).unwrap_or(false)
}
fn rng_int(w: &BlockTickWorld, bound: i32) -> i32 {
    w.next_int.map(|f| f(bound)).unwrap_or(0)
}
fn u_set_meta(w: &BlockTickWorld, x: i32, y: i32, z: i32, meta: u8) {
    if let Some(f) = w.set_block_meta {
        f(x, y, z, meta);
    }
}
fn u_set_notify(w: &BlockTickWorld, x: i32, y: i32, z: i32, id: u8) {
    if let Some(f) = w.set_block_notify {
        f(x, y, z, id);
    }
}
fn u_schedule(w: &BlockTickWorld, x: i32, y: i32, z: i32, id: u8, delay: i32) {
    if let Some(f) = w.schedule_update {
        f(x, y, z, id, delay);
    }
}

fn encourage_at(w: &BlockTickWorld, x: i32, y: i32, z: i32, current: i32) -> i32 {
    let v = encourage(q_id(w, x, y, z));
    if v > current {
        v
    } else {
        current
    }
}

fn has_burnable_neighbor(w: &BlockTickWorld, x: i32, y: i32, z: i32) -> bool {
    encourage_at(w, x + 1, y, z, 0) > 0
        || encourage_at(w, x - 1, y, z, 0) > 0
        || encourage_at(w, x, y - 1, z, 0) > 0
        || encourage_at(w, x, y + 1, z, 0) > 0
        || encourage_at(w, x, y, z - 1, 0) > 0
        || encourage_at(w, x, y, z + 1, 0) > 0
}

fn neighbors_encourage(w: &BlockTickWorld, x: i32, y: i32, z: i32) -> i32 {
    if q_id(w, x, y, z) != 0 {
        return 0;
    }
    let mut result = 0;
    result = encourage_at(w, x + 1, y, z, result);
    result = encourage_at(w, x - 1, y, z, result);
    result = encourage_at(w, x, y - 1, z, result);
    result = encourage_at(w, x, y + 1, z, result);
    result = encourage_at(w, x, y, z - 1, result);
    result = encourage_at(w, x, y, z + 1, result);
    result
}

fn try_catch_fire(
    w: &BlockTickWorld,
    fire_id: u8,
    detonate_tnt: Option<extern "C" fn(x: i32, y: i32, z: i32)>,
    x: i32,
    y: i32,
    z: i32,
    chance: i32,
) {
    let id = q_id(w, x, y, z);
    if ability(id) > 0 && rng_int(w, chance) < ability(id) {
        let is_tnt = id == 46;
        if rng_int(w, 2) == 0 {
            u_set_notify(w, x, y, z, fire_id);
        } else {
            u_set_notify(w, x, y, z, 0);
        }
        if is_tnt {
            if let Some(f) = detonate_tnt {
                f(x, y, z);
            }
        }
    }
}

/// Extra C++ hook for fire: TNT detonation (virtual dispatch on another
/// block, stays in C++).
#[repr(C)]
pub struct FireWorld {
    pub base: *const BlockTickWorld,
    pub detonate_tnt: Option<extern "C" fn(x: i32, y: i32, z: i32)>,
}

fn base(world: *const FireWorld) -> Option<&'static BlockTickWorld> {
    if world.is_null() {
        return None;
    }
    let fw = unsafe { &*world };
    if fw.base.is_null() {
        return None;
    }
    Some(unsafe { &*fw.base })
}

#[no_mangle]
pub unsafe extern "C" fn block_fire_tick(
    world: *const FireWorld,
    fire_id: u8,
    tick_rate: i32,
    x: i32,
    y: i32,
    z: i32,
) {
    let Some(w) = base(world) else {
        return;
    };
    let fw = unsafe { &*world };
    let on_netherrack = q_id(w, x, y - 1, z) == 87;
    let meta = q_meta(w, x, y, z);

    if meta < 15 {
        u_set_meta(w, x, y, z, meta.saturating_add(1));
        u_schedule(w, x, y, z, fire_id, tick_rate);
    }

    if !on_netherrack && !has_burnable_neighbor(w, x, y, z) {
        if !q_attach(w, x, y - 1, z) || meta > 3 {
            u_set_notify(w, x, y, z, 0);
        }
    } else if !on_netherrack
        && encourage(q_id(w, x, y - 1, z)) == 0
        && meta == 15
        && rng_int(w, 4) == 0
    {
        u_set_notify(w, x, y, z, 0);
    } else if meta % 2 == 0 && meta > 2 {
        try_catch_fire(w, fire_id, fw.detonate_tnt, x + 1, y, z, 300);
        try_catch_fire(w, fire_id, fw.detonate_tnt, x - 1, y, z, 300);
        try_catch_fire(w, fire_id, fw.detonate_tnt, x, y - 1, z, 250);
        try_catch_fire(w, fire_id, fw.detonate_tnt, x, y + 1, z, 250);
        try_catch_fire(w, fire_id, fw.detonate_tnt, x, y, z - 1, 300);
        try_catch_fire(w, fire_id, fw.detonate_tnt, x, y, z + 1, 300);

        for nx in x - 1..=x + 1 {
            for nz in z - 1..=z + 1 {
                for ny in y - 1..=y + 4 {
                    if nx == x && ny == y && nz == z {
                        continue;
                    }
                    let mut chance = 100;
                    if ny > y + 1 {
                        chance += (ny - (y + 1)) * 100;
                    }
                    let neighbor = neighbors_encourage(w, nx, ny, nz);
                    if neighbor > 0 && rng_int(w, chance) < neighbor {
                        u_set_notify(w, nx, ny, nz, fire_id);
                    }
                }
            }
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn block_fire_can_place(world: *const FireWorld, x: i32, y: i32, z: i32) -> bool {
    let Some(w) = base(world) else {
        return false;
    };
    q_attach(w, x, y - 1, z) || has_burnable_neighbor(w, x, y, z)
}

#[no_mangle]
pub unsafe extern "C" fn block_fire_neighbor(world: *const FireWorld, x: i32, y: i32, z: i32) {
    let Some(w) = base(world) else {
        return;
    };
    if !q_attach(w, x, y - 1, z) && !has_burnable_neighbor(w, x, y, z) {
        u_set_notify(w, x, y, z, 0);
    }
}

#[no_mangle]
pub unsafe extern "C" fn block_fire_added(world: *const FireWorld, fire_id: u8, tick_rate: i32, x: i32, y: i32, z: i32) {
    let Some(w) = base(world) else {
        return;
    };
    if !q_attach(w, x, y - 1, z) && !has_burnable_neighbor(w, x, y, z) {
        u_set_notify(w, x, y, z, 0);
    } else {
        u_schedule(w, x, y, z, fire_id, tick_rate);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::block_ticks::BlockTickWorld;
    use std::collections::HashMap;
    use std::sync::{Mutex, MutexGuard};

    struct Fake {
        blocks: HashMap<(i32, i32, i32), (u8, u8)>,
        attach: HashMap<(i32, i32, i32), bool>,
        log: Vec<String>,
        int_script: Vec<i32>,
        int_pos: usize,
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
            attach: HashMap::new(),
            log: Vec::new(),
            int_script: Vec::new(),
            int_pos: 0,
        });
    }

    extern "C" fn s_next_int(bound: i32) -> i32 {
        let mut g = fake();
        let f = g.as_mut().unwrap_or_else(|| unreachable!());
        if f.int_pos < f.int_script.len() {
            let v = f.int_script[f.int_pos];
            f.int_pos += 1;
            v % bound
        } else {
            bound - 1 // default: rolls fail
        }
    }
    extern "C" fn s_get_id(x: i32, y: i32, z: i32) -> u8 {
        fake().as_ref().and_then(|f| f.blocks.get(&(x, y, z)).map(|b| b.0)).unwrap_or(0)
    }
    extern "C" fn s_get_meta(x: i32, y: i32, z: i32) -> u8 {
        fake().as_ref().and_then(|f| f.blocks.get(&(x, y, z)).map(|b| b.1)).unwrap_or(0)
    }
    extern "C" fn s_set_meta(x: i32, y: i32, z: i32, meta: u8) {
        if let Some(f) = fake().as_mut() {
            let id = f.blocks.get(&(x, y, z)).map(|b| b.0).unwrap_or(0);
            f.blocks.insert((x, y, z), (id, meta));
            f.log.push(format!("meta {x} {y} {z} {meta}"));
        }
    }
    extern "C" fn s_set_notify(x: i32, y: i32, z: i32, id: u8) {
        if let Some(f) = fake().as_mut() {
            f.blocks.insert((x, y, z), (id, 0));
            f.log.push(format!("notify {x} {y} {z} {id}"));
        }
    }
    extern "C" fn s_attach(x: i32, y: i32, z: i32) -> bool {
        fake().as_ref().and_then(|f| f.attach.get(&(x, y, z)).copied()).unwrap_or(false)
    }
    extern "C" fn s_schedule(x: i32, y: i32, z: i32, id: u8, delay: i32) {
        if let Some(f) = fake().as_mut() {
            f.log.push(format!("sched {x} {y} {z} {id} {delay}"));
        }
    }
    extern "C" fn s_detonate(x: i32, y: i32, z: i32) {
        if let Some(f) = fake().as_mut() {
            f.log.push(format!("boom {x} {y} {z}"));
        }
    }
    // Unused table slots: null (treated as missing).
    extern "C" fn s_f01() -> f32 {
        0.0
    }
    extern "C" fn s_u64() -> u64 {
        0
    }

    fn base_table() -> BlockTickWorld {
        BlockTickWorld {
            next_int: Some(s_next_int),
            next_float01: Some(s_f01),
            next_u64: Some(s_u64),
            get_block_id: Some(s_get_id),
            get_block_id_nc: Some(s_get_id),
            get_block_meta: Some(s_get_meta),
            set_block: None,
            set_block_meta: Some(s_set_meta),
            set_block_notify: Some(s_set_notify),
            set_block_update: None,
            set_block_meta_notify: None,
            set_block_and_meta: None,
            get_block_light: None,
            can_see_sky: None,
            attach_world: Some(s_attach),
            attach_torch: None,
            is_solid: None,
            is_solid_nc: None,
            is_water_or_lava: None,
            is_water: None,
            block_registered: None,
            collidable_box: None,
            schedule_update: Some(s_schedule),
            mark_update: None,
            notify_neighbors: None,
            spawn_drop: None,
            spawn_falling: None,
            drop_occupant: None,
        }
    }

    fn logs() -> Vec<String> {
        fake().as_ref().map(|f| f.log.clone()).unwrap_or_default()
    }

    #[test]
    fn test_fire_scenarios() {
        // FireWorld borrows the table; keep both alive together.
        let base = base_table();
        let fw = FireWorld { base: &base, detonate_tnt: Some(s_detonate) };
        let fp = &fw as *const FireWorld;

        // 1. Young fire on attached stone ages and reschedules.
        reset();
        {
            let mut g = fake();
            let f = g.as_mut().unwrap_or_else(|| unreachable!());
            f.blocks.insert((0, 5, 0), (51, 2));
            f.blocks.insert((0, 4, 0), (1, 0));
            f.attach.insert((0, 4, 0), true);
        }
        unsafe { block_fire_tick(fp, 51, 10, 0, 5, 0) };
        let l = logs();
        assert!(l.contains(&"meta 0 5 0 3".to_string()), "{l:?}");
        assert!(l.iter().any(|e| e.starts_with("sched 0 5 0 51 10")), "{l:?}");

        // 2. Old fire without fuel dies.
        reset();
        {
            let mut g = fake();
            let f = g.as_mut().unwrap_or_else(|| unreachable!());
            f.blocks.insert((0, 5, 0), (51, 5));
            f.blocks.insert((0, 4, 0), (1, 0));
            f.attach.insert((0, 4, 0), true);
        }
        unsafe { block_fire_tick(fp, 51, 10, 0, 5, 0) };
        assert!(logs().contains(&"notify 0 5 0 0".to_string()), "{:?}", logs());

        // 3. Fire on netherrack never starves.
        reset();
        {
            let mut g = fake();
            let f = g.as_mut().unwrap_or_else(|| unreachable!());
            f.blocks.insert((0, 5, 0), (51, 15));
            f.blocks.insert((0, 4, 0), (87, 0));
        }
        unsafe { block_fire_tick(fp, 51, 10, 0, 5, 0) };
        assert!(!logs().iter().any(|e| e == "notify 0 5 0 0"), "{:?}", logs());

        // 4. Adjacent planks catch fire on a lucky roll.
        reset();
        {
            let mut g = fake();
            let f = g.as_mut().unwrap_or_else(|| unreachable!());
            f.blocks.insert((0, 5, 0), (51, 4));
            f.blocks.insert((0, 4, 0), (87, 0));
            f.blocks.insert((1, 5, 0), (5, 0));
            f.int_script = vec![0, 0];
        }
        unsafe { block_fire_tick(fp, 51, 10, 0, 5, 0) };
        assert!(logs().contains(&"notify 1 5 0 51".to_string()), "{:?}", logs());

        // 5. TNT catches, clears, and detonates.
        reset();
        {
            let mut g = fake();
            let f = g.as_mut().unwrap_or_else(|| unreachable!());
            f.blocks.insert((0, 5, 0), (51, 4));
            f.blocks.insert((0, 4, 0), (87, 0));
            f.blocks.insert((1, 5, 0), (46, 0));
            f.int_script = vec![0, 1];
        }
        unsafe { block_fire_tick(fp, 51, 10, 0, 5, 0) };
        let l = logs();
        assert!(l.contains(&"notify 1 5 0 0".to_string()), "{l:?}");
        assert!(l.contains(&"boom 1 5 0".to_string()), "{l:?}");

        // 6. Encouraged air next to the fire ignites through the spread loop
        // (air at +1 with planks at +2).
        reset();
        {
            let mut g = fake();
            let f = g.as_mut().unwrap_or_else(|| unreachable!());
            f.blocks.insert((0, 5, 0), (51, 4));
            f.blocks.insert((0, 4, 0), (87, 0));
            f.blocks.insert((2, 5, 0), (5, 0));
            f.int_script = vec![0];
        }
        unsafe { block_fire_tick(fp, 51, 10, 0, 5, 0) };
        assert!(logs().contains(&"notify 1 5 0 51".to_string()), "{:?}", logs());

        // 7. Placement needs support or fuel; dead fire clears on touch.
        reset();
        assert!(!unsafe { block_fire_can_place(fp, 0, 5, 0) });
        {
            let mut g = fake();
            let f = g.as_mut().unwrap_or_else(|| unreachable!());
            f.blocks.insert((0, 5, 0), (51, 0));
        }
        unsafe { block_fire_neighbor(fp, 0, 5, 0) };
        assert!(logs().contains(&"notify 0 5 0 0".to_string()), "{:?}", logs());
        reset();
        {
            let mut g = fake();
            let f = g.as_mut().unwrap_or_else(|| unreachable!());
            f.attach.insert((0, 4, 0), true);
        }
        unsafe { block_fire_added(fp, 51, 10, 0, 5, 0) };
        assert!(logs().iter().any(|e| e.starts_with("sched 0 5 0 51")), "{:?}", logs());

        // 8. Null table is a safe no-op.
        unsafe {
            block_fire_tick(std::ptr::null(), 51, 10, 0, 5, 0);
            assert!(!block_fire_can_place(std::ptr::null(), 0, 5, 0));
        }
    }
}
