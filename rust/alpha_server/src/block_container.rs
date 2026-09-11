//! Container-block helpers ported from C++ `BlockChest` / `BlockFurnace`
//! (mirrors Java `BlockChest` / `BlockFurnace`).
//!
//! Only the world-query half moves: chest placement rules and the
//! inventory-scatter loops. TileEntity lookup, GUI packets
//! (`blockActivated`), and TileEntity lifetime stay in C++ until the
//! TileEntity table moves to Rust.
//!
//! RNG notes: the old chest scatter drew from a per-call
//! `mt19937(random_device)` (offsets, chunk sizes, gaussian motion) and the
//! furnace from `World::rand`. Both now draw from the shared global Rust
//! RNG via callbacks. Scatter motion is cosmetic; exact historical streams
//! are intentionally not replicated (the chest stream was re-seeded per
//! call anyway).

/// RNG + spawn hooks for scatter. Gaussians are derived inside Rust
/// (Box-Muller over `next_f64_01`); C++ only constructs the item entity.
#[repr(C)]
pub struct ScatterWorld {
    pub next_int: Option<extern "C" fn(bound: i32) -> i32>,
    pub next_f32_01: Option<extern "C" fn() -> f32>,
    pub next_f64_01: Option<extern "C" fn() -> f64>,
    pub spawn_item: Option<
        extern "C" fn(item_id: i32, count: i32, damage: i32, fx: f64, fy: f64, fz: f64, mx: f64, my: f64, mz: f64),
    >,
}

fn rng_int(w: &ScatterWorld, bound: i32) -> i32 {
    w.next_int.map(|f| f(bound)).unwrap_or(0)
}
fn rng_f32(w: &ScatterWorld) -> f32 {
    w.next_f32_01.map(|f| f()).unwrap_or(0.0)
}
fn rng_f64(w: &ScatterWorld) -> f64 {
    w.next_f64_01.map(|f| f()).unwrap_or(0.0)
}

/// Standard normal via Box-Muller (two uniforms per sample, no caching).
fn gaussian(w: &ScatterWorld) -> f64 {
    let (mut u1, mut u2) = (rng_f64(w), rng_f64(w));
    if u1 <= 0.0 {
        u1 = f64::MIN_POSITIVE;
    }
    if u2 <= 0.0 {
        u2 = f64::MIN_POSITIVE;
    }
    (-2.0 * u1.ln()).sqrt() * (2.0 * std::f64::consts::PI * u2).cos()
}

fn emit(
    w: &ScatterWorld,
    item_id: i32,
    count: i32,
    damage: i32,
    fx: f64,
    fy: f64,
    fz: f64,
    mx: f64,
    my: f64,
    mz: f64,
) {
    if count <= 0 {
        return;
    }
    if let Some(f) = w.spawn_item {
        f(item_id, count, damage, fx, fy, fz, mx, my, mz);
    }
}

/// One chest slot scattered in 10..30-sized chunks (mirrors
/// `BlockChest::onBlockRemoval`). Returns the leftover (always 0 unless the
/// table is missing).
#[no_mangle]
pub unsafe extern "C" fn block_chest_scatter_stack(
    world: *const ScatterWorld,
    item_id: i32,
    count: i32,
    damage: i32,
    x: i32,
    y: i32,
    z: i32,
) -> i32 {
    if world.is_null() || item_id <= 0 || count <= 0 {
        return count.max(0);
    }
    let w = unsafe { &*world };
    // Offsets stay in f32 like the C++ float distribution, then promote.
    // Positions add in f32 too (C++ int+float), widening only at the call.
    let (ox, oy, oz) = (rng_f32(w) * 0.8 + 0.1, rng_f32(w) * 0.8 + 0.1, rng_f32(w) * 0.8 + 0.1);
    let mut left = count;
    while left > 0 {
        let n = left.min(rng_int(w, 21) + 10);
        left -= n;
        emit(
            w,
            item_id,
            n,
            damage,
            (x as f32 + ox) as f64,
            (y as f32 + oy) as f64,
            (z as f32 + oz) as f64,
            gaussian(w) * 0.05,
            gaussian(w) * 0.05 + 0.2,
            gaussian(w) * 0.05,
        );
    }
    left
}

/// One furnace slot scattered whole (mirrors `BlockFurnace::onBlockRemoval`).
#[no_mangle]
pub unsafe extern "C" fn block_furnace_scatter_stack(
    world: *const ScatterWorld,
    item_id: i32,
    count: i32,
    damage: i32,
    x: i32,
    y: i32,
    z: i32,
) {
    if world.is_null() || item_id <= 0 || count <= 0 {
        return;
    }
    let w = unsafe { &*world };
    emit(
        w,
        item_id,
        count,
        damage,
        x as f64 + 0.5,
        y as f64 + 0.7,
        z as f64 + 0.5,
        rng_f64(w) * 0.2 - 0.1,
        0.2,
        rng_f64(w) * 0.2 - 0.1,
    );
}

/// Chest placement (mirrors `BlockChest::canPlaceBlockAt`): no more than
/// one adjacent chest, and no adjacent double-chest.
#[no_mangle]
pub unsafe extern "C" fn block_chest_can_place(
    get_block_id: Option<extern "C" fn(x: i32, y: i32, z: i32) -> u8>,
    chest_id: u8,
    x: i32,
    y: i32,
    z: i32,
) -> bool {
    let Some(get) = get_block_id else {
        return false;
    };
    let at = |dx: i32, dz: i32| get(x + dx, y, z + dz) == chest_id;
    let mut adjacent = 0;
    for (dx, dz) in [(-1, 0), (1, 0), (0, -1), (0, 1)] {
        if at(dx, dz) {
            adjacent += 1;
        }
    }
    if adjacent > 1 {
        return false;
    }
    // No neighbor that already has its own neighbor (would make a triple).
    for (dx, dz) in [(-1, 0), (1, 0), (0, -1), (0, 1)] {
        if !at(dx, dz) {
            continue;
        }
        let (nx, nz) = (x + dx, z + dz);
        for (ex, ez) in [(-1, 0), (1, 0), (0, -1), (0, 1)] {
            if get(nx + ex, y, nz + ez) == chest_id {
                return false;
            }
        }
    }
    true
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashMap;
    use std::sync::{Mutex, MutexGuard};

    struct Fake {
        blocks: HashMap<(i32, i32, i32), u8>,
        log: Vec<String>,
        int_script: Vec<i32>,
        int_pos: usize,
        f32_val: f32,
        f64_val: f64,
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
            log: Vec::new(),
            int_script: Vec::new(),
            int_pos: 0,
            f32_val: 0.0,
            f64_val: 0.0,
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
            0
        }
    }
    extern "C" fn s_f32() -> f32 {
        fake().as_ref().map(|f| f.f32_val).unwrap_or(0.0)
    }
    extern "C" fn s_f64() -> f64 {
        fake().as_ref().map(|f| f.f64_val).unwrap_or(0.0)
    }
    extern "C" fn s_spawn(item: i32, count: i32, damage: i32, fx: f64, fy: f64, fz: f64, mx: f64, my: f64, mz: f64) {
        if let Some(f) = fake().as_mut() {
            f.log.push(format!("spawn {item} {count} {damage} {fx:.1} {fy:.1} {fz:.1} {mx:.2} {my:.2} {mz:.2}"));
        }
    }
    extern "C" fn s_get_id(x: i32, y: i32, z: i32) -> u8 {
        fake().as_ref().and_then(|f| f.blocks.get(&(x, y, z)).copied()).unwrap_or(0)
    }

    fn table() -> ScatterWorld {
        ScatterWorld {
            next_int: Some(s_next_int),
            next_f32_01: Some(s_f32),
            next_f64_01: Some(s_f64),
            spawn_item: Some(s_spawn),
        }
    }

    fn logs() -> Vec<String> {
        fake().as_ref().map(|f| f.log.clone()).unwrap_or_default()
    }

    #[test]
    fn test_container_scenarios() {
        let t = table();
        let tp = &t as *const ScatterWorld;

        // 1. Chest stack of 25 chunks into 10/10/5 at the +.1 corner.
        reset();
        let left = unsafe { block_chest_scatter_stack(tp, 35, 25, 0, 10, 64, 10) };
        assert_eq!(left, 0);
        let l = logs();
        assert_eq!(l.len(), 3);
        assert!(l[0].starts_with("spawn 35 10 0 10.1 64.1 10.1"), "{l:?}");
        assert!(l[2].starts_with("spawn 35 5 0"), "{l:?}");

        // 2. Empty/degenerate inputs scatter nothing.
        reset();
        assert_eq!(unsafe { block_chest_scatter_stack(tp, 35, 0, 0, 0, 0, 0) }, 0);
        assert_eq!(unsafe { block_chest_scatter_stack(tp, 0, 5, 0, 0, 0, 0) }, 5);
        assert!(logs().is_empty());
        unsafe { block_furnace_scatter_stack(tp, 0, 5, 0, 0, 0, 0) };
        assert!(logs().is_empty());

        // 3. Furnace slot drops whole at block center-top.
        reset();
        unsafe { block_furnace_scatter_stack(tp, 265, 3, 0, 1, 2, 3) };
        let l = logs();
        assert_eq!(l.len(), 1);
        assert!(l[0].starts_with("spawn 265 3 0 1.5 2.7 3.5 -0.10 0.20 -0.10"), "{l:?}");

        // 4. Chest placement: solo ok, double ok, triple rejected.
        reset();
        assert!(unsafe { block_chest_can_place(Some(s_get_id), 54, 0, 64, 0) });
        let _ = fake().as_mut().map(|f| {
            let _ = f.blocks.insert((1, 64, 0), 54);
        });
        assert!(unsafe { block_chest_can_place(Some(s_get_id), 54, 0, 64, 0) });
        let _ = fake().as_mut().map(|f| {
            let _ = f.blocks.insert((2, 64, 0), 54);
        });
        assert!(!unsafe { block_chest_can_place(Some(s_get_id), 54, 0, 64, 0) });
        // Two adjacent directly: also rejected.
        reset();
        let _ = fake().as_mut().map(|f| {
            let _ = f.blocks.insert((1, 64, 0), 54);
            let _ = f.blocks.insert((-1, 64, 0), 54);
        });
        assert!(!unsafe { block_chest_can_place(Some(s_get_id), 54, 0, 64, 0) });

        // 5. Null inputs are safe.
        assert_eq!(unsafe { block_chest_scatter_stack(std::ptr::null(), 35, 5, 0, 0, 0, 0) }, 5);
        assert!(!unsafe { block_chest_can_place(None, 54, 0, 64, 0) });
    }
}
