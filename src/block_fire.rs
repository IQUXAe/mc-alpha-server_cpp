//! Fire behavior ported from C++ `BlockFire` (mirrors Java `BlockFire`).
//!
//! Burn-rate tables live here as constants (from the old constructor's
//! `setBurnRate` calls); C++ keeps only the `Block` shell. Spread, aging,
//! and catching run on a direct `&mut World` borrow; detonating TNT calls
//! straight into the world.

use crate::world::World;

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

fn q_id(w: &mut World, x: i32, y: i32, z: i32) -> u8 {
    w.get_block_id(x, y, z)
}
fn q_meta(w: &mut World, x: i32, y: i32, z: i32) -> u8 {
    w.get_block_meta(x, y, z)
}
fn q_attach(w: &mut World, x: i32, y: i32, z: i32) -> bool {
    w.block_allows_attachment(x, y, z)
}
fn rng_int(w: &mut World, bound: i32) -> i32 {
    w.rng_next_int(bound)
}
fn u_set_meta(w: &mut World, x: i32, y: i32, z: i32, meta: u8) {
    w.set_block_meta(x, y, z, meta);
}
fn u_set_notify(w: &mut World, x: i32, y: i32, z: i32, id: u8) {
    w.apply_set_notify(x, y, z, id);
}
fn u_schedule(w: &mut World, x: i32, y: i32, z: i32, id: u8, delay: i32) {
    w.schedule_block_update(x, y, z, id, delay);
}

fn encourage_at(w: &mut World, x: i32, y: i32, z: i32, current: i32) -> i32 {
    let v = encourage(q_id(w, x, y, z));
    if v > current {
        v
    } else {
        current
    }
}

fn has_burnable_neighbor(w: &mut World, x: i32, y: i32, z: i32) -> bool {
    encourage_at(w, x + 1, y, z, 0) > 0
        || encourage_at(w, x - 1, y, z, 0) > 0
        || encourage_at(w, x, y - 1, z, 0) > 0
        || encourage_at(w, x, y + 1, z, 0) > 0
        || encourage_at(w, x, y, z - 1, 0) > 0
        || encourage_at(w, x, y, z + 1, 0) > 0
}

fn neighbors_encourage(w: &mut World, x: i32, y: i32, z: i32) -> i32 {
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
    w: &mut World,
    fire_id: u8,
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
            w.ignite_tnt(x, y, z, 80);
        }
    }
}

pub fn block_fire_tick(
    w: &mut World,
    fire_id: u8,
    tick_rate: i32,
    x: i32,
    y: i32,
    z: i32,
) {
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
        try_catch_fire(w, fire_id, x + 1, y, z, 300);
        try_catch_fire(w, fire_id, x - 1, y, z, 300);
        try_catch_fire(w, fire_id, x, y - 1, z, 250);
        try_catch_fire(w, fire_id, x, y + 1, z, 250);
        try_catch_fire(w, fire_id, x, y, z - 1, 300);
        try_catch_fire(w, fire_id, x, y, z + 1, 300);

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
                    // Java BlockFire: nextInt(chance) <= encourage (inclusive).
                    if neighbor > 0 && rng_int(w, chance) <= neighbor {
                        u_set_notify(w, nx, ny, nz, fire_id);
                    }
                }
            }
        }
    }
}

pub fn block_fire_can_place(w: &mut World, x: i32, y: i32, z: i32) -> bool {
    q_attach(w, x, y - 1, z) || has_burnable_neighbor(w, x, y, z)
}

pub fn block_fire_neighbor(w: &mut World, x: i32, y: i32, z: i32) {
    if !q_attach(w, x, y - 1, z) && !has_burnable_neighbor(w, x, y, z) {
        u_set_notify(w, x, y, z, 0);
    }
}

pub fn block_fire_added(w: &mut World, fire_id: u8, tick_rate: i32, x: i32, y: i32, z: i32) {
    if !q_attach(w, x, y - 1, z) && !has_burnable_neighbor(w, x, y, z) {
        u_set_notify(w, x, y, z, 0);
    } else {
        u_schedule(w, x, y, z, fire_id, tick_rate);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::chunk::Chunk;
    use crate::world::World;

    fn harness(seed: i64) -> World {
        World::new(seed)
    }

    /// One chunk plus scripted cells (id + meta), with height and skylight
    /// maps. No neighbor routing: the drivers under test drive the real
    /// routing themselves.
    fn stage(w: &mut World, cells: &[(i32, i32, i32, u8, u8)]) {
        // 3x3 chunk neighborhood: scheduling requires loaded surroundings
        // (radius 8), so the center chunk never ticks alone.
        for cx in -1..=1 {
            for cz in -1..=1 {
                w.insert_chunk(Chunk::new(cx, cz));
            }
        }
        let c = w.chunk_ref_mut(0, 0).unwrap();
        for &(x, y, z, id, meta) in cells {
            c.set_block_id(x, y, z, id);
            c.set_block_metadata(x, y, z, meta);
        }
        c.generate_height_map();
        c.generate_skylight_map();
    }

    #[test]
    fn test_fire_scenarios() {
        // 1. Young fire on attached stone ages and reschedules.
        let mut w = harness(7);
        stage(&mut w, &[(0, 5, 0, 51, 2), (0, 4, 0, 1, 0)]);
        block_fire_tick(&mut w, 51, 10, 0, 5, 0);
        assert_eq!(w.get_block_meta(0, 5, 0), 3);
        assert!(w.scheduled.contains_key(&(10, 0, 5, 0, 51)));

        // 2. Old fire without fuel dies.
        let mut w = harness(7);
        stage(&mut w, &[(0, 5, 0, 51, 5), (0, 4, 0, 1, 0)]);
        block_fire_tick(&mut w, 51, 10, 0, 5, 0);
        assert_eq!(w.get_block_id(0, 5, 0), 0);

        // 3. Fire on netherrack never starves.
        let mut w = harness(7);
        stage(&mut w, &[(0, 5, 0, 51, 15), (0, 4, 0, 87, 0)]);
        block_fire_tick(&mut w, 51, 10, 0, 5, 0);
        assert_eq!(w.get_block_id(0, 5, 0), 51);

        // 4. Adjacent planks catch fire on a lucky roll (seed opens with
        // next_int(300) < 20, then next_int(2) == 0). Stone below keeps
        // the new fire attached (unsupported fire burns out on placement,
        // like vanilla).
        let mut w = harness(81);
        stage(&mut w, &[(0, 5, 0, 51, 4), (0, 4, 0, 87, 0), (1, 5, 0, 5, 0), (1, 4, 0, 1, 0)]);
        block_fire_tick(&mut w, 51, 10, 0, 5, 0);
        assert_eq!(w.get_block_id(1, 5, 0), 51);

        // 5. TNT catches, clears, and detonates (seed opens with
        // next_int(300) < 100, then next_int(2) == 1).
        let mut w = harness(0);
        stage(&mut w, &[(0, 5, 0, 51, 4), (0, 4, 0, 87, 0), (1, 5, 0, 46, 0)]);
        block_fire_tick(&mut w, 51, 10, 0, 5, 0);
        assert_eq!(w.get_block_id(1, 5, 0), 0);
        assert_eq!(w.pending_tnt, vec![(1, 5, 0, 80)]);

        // 6. Encouraged air next to the fire ignites through the spread loop
        // (air at +1 with planks at +2; seed opens with next_int(100) <= 5).
        let mut w = harness(300018);
        stage(&mut w, &[(0, 5, 0, 51, 4), (0, 4, 0, 87, 0), (2, 5, 0, 5, 0)]);
        block_fire_tick(&mut w, 51, 10, 0, 5, 0);
        assert_eq!(w.get_block_id(1, 5, 0), 51);

        // 7. Placement needs support or fuel; dead fire clears on touch.
        let mut w = harness(7);
        assert!(!block_fire_can_place(&mut w, 0, 5, 0));
        stage(&mut w, &[(0, 5, 0, 51, 0)]);
        block_fire_neighbor(&mut w, 0, 5, 0);
        assert_eq!(w.get_block_id(0, 5, 0), 0);
        let mut w = harness(7);
        stage(&mut w, &[(0, 4, 0, 1, 0)]);
        block_fire_added(&mut w, 51, 10, 0, 5, 0);
        assert!(w.scheduled.contains_key(&(10, 0, 5, 0, 51)));
    }
}
