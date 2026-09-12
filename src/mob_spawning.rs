//! Pure mob-spawning math ported from C++ `World::spawnHostileMobs` /
//! `World::spawnPassiveMobs` (which mirror Java `SpawnerAnimals::func_4111_a`).
//!
//! Two layers:
//! - `alpha_spawn_*` helpers: the cap formula, pack-spread step, and
//!   world-spawn exclusion check. Pure (no pointers, no allocation), the
//!   single source of truth, unit-tested on both sides of the FFI.
//! - `rust_world_spawn_hostile` / `rust_world_spawn_passive`: batch drivers
//!   that run the whole per-tick spawn pass. C++ keeps ownership of the
//!   RNG stream (`World::rand`, via the `next_int` / `next_uniform_float`
//!   callbacks, so the exact draw sequence is preserved), block storage,
//!   and the entity list; Rust owns the control flow and all constants.
//!
//! NOTE on draw order: the old C++ loop drew the four pack-spread values as
//! `x += spread6(rand) - spread6(rand)`, whose evaluation order was
//! unspecified. The driver below fixes the order (x-pair, then z-pair);
//! the draw *count* per attempt is unchanged.
//!
//! World access stays in C++ behind the `SpawnerWorld` callback table
//! (block storage, entity list, `getCanSpawnHere`), and RNG draws stay on
//! the C++ `World::rand` stream via callbacks, so check order
//! and draw sequences are preserved.

/// Spawn cap: `budget_per_256 * num_eligible_chunks / 256`.
///
/// Mirrors `World::spawnHostileMobs` (`100 * n / 256`) and
/// `World::spawnPassiveMobs` (`20 * n / 256`), which mirror Java
/// `var29.field_4220_d * field_4311_a.size() / 256` per creature type.
/// Multiplication first, then truncating division, exactly like C++ `int`.
pub fn alpha_spawn_max_count(num_eligible_chunks: i32, budget_per_256: i32) -> i32 {
    budget_per_256.wrapping_mul(num_eligible_chunks).wrapping_div(256)
}

/// One pack-spread step: `first - second`.
///
/// Mirrors `x += spread6(rand) - spread6(rand)` (Java
/// `var15 += rand.nextInt(6) - rand.nextInt(6)`). Each draw is uniform in
/// `[0, 6)`, so the step is triangular in `[-5, 5]`. The two draws are
/// passed in (C++ owns the RNG stream); this function only owns the shape.
pub fn alpha_spawn_pack_offset(first: i32, second: i32) -> i32 {
    first.wrapping_sub(second)
}

/// World-spawn exclusion: is the candidate inside the 24-block no-spawn
/// cube around the world spawn (squared distance `< 576.0`)?
///
/// Mirrors the `dsx*dsx + dsy*dsy + dsz*dsz < 576.0f` check in both spawn
/// functions (Java `var26 >= 576.0F` gate). `f32` arithmetic matches the
/// C++ `float` computation bit-for-bit for the same inputs.
pub fn alpha_spawn_too_close_to_spawn(
    fx: f32,
    fy: f32,
    fz: f32,
    spawn_x: i32,
    spawn_y: i32,
    spawn_z: i32,
) -> bool {
    let dsx = fx - spawn_x as f32;
    let dsy = fy - spawn_y as f32;
    let dsz = fz - spawn_z as f32;
    dsx * dsx + dsy * dsy + dsz * dsz < 576.0
}

/// World access for the spawn drivers. All function pointers must be
/// non-null; a null table (or null entry) makes the driver return 0.
///
/// RNG draws go through `next_int` / `next_uniform_float` into C++
/// `World::rand`, preserving the exact historical draw sequence.
/// `try_spawn` must construct `kind`, position it, run `getCanSpawnHere`,
/// join the world on success, write `getMaxSpawnedInChunk` to
/// `out_max_in_chunk`, and return the entity id (or negative on failure).
/// Kinds are 0=spider,1=zombie,2=skeleton,3=creeper for the hostile driver
/// and 0=sheep,1=pig,2=chicken,3=cow for the passive driver.
pub struct SpawnerWorld {
    pub next_int: Option<fn(bound: i32) -> i32>,
    pub next_uniform_float: Option<fn(lo: f32, hi: f32) -> f32>,
    pub chunk_exists: Option<fn(x: i32, z: i32) -> bool>,
    pub is_solid: Option<fn(x: i32, y: i32, z: i32) -> bool>,
    pub is_air: Option<fn(x: i32, y: i32, z: i32) -> bool>,
    pub is_liquid: Option<fn(x: i32, y: i32, z: i32) -> bool>,
    pub try_spawn:
        Option<fn(kind: u8, fx: f32, fy: f32, fz: f32, yaw: f32, out_max_in_chunk: &mut i32) -> i32>,
    pub spawn_jockey:
        Option<fn(fx: f32, fy: f32, fz: f32, yaw: f32, host_id: i32) -> bool>,
}

const CHUNK_RADIUS: i32 = 8;
const CHUNK_ROLL: i32 = 50;
const GROUP_ATTEMPTS: i32 = 3;
const PACK_ATTEMPTS: i32 = 4;
const PACK_SPREAD: i32 = 6;
const PLAYER_RANGE: f64 = 24.0;
const JOCKEY_ROLL: i32 = 100;
const HOSTILE_BUDGET: i32 = 100;
const PASSIVE_BUDGET: i32 = 20;

fn chunk_key(chunk_x: i32, chunk_z: i32) -> u64 {
    ((chunk_x as u32 as u64) << 32) | (chunk_z as u32 as u64)
}

/// One full spawn pass. Returns the number of primary spawns (jockey
/// skeletons excluded). Preserves the old C++ check order and draw sequence.
#[allow(clippy::too_many_arguments)]
fn spawn_pass(
    world: &SpawnerWorld,
    players_x: &[f64],
    players_y: &[f64],
    players_z: &[f64],
    hostile: bool,
    current_count: i32,
    spawn_x: i32,
    spawn_y: i32,
    spawn_z: i32,
    world_height: i32,
) -> i32 {
    // Eligible chunks: 8-chunk square around every player (Java HashSet,
    // C++ vector+sort+unique — same set either way).
    let mut eligible: Vec<u64> = Vec::new();
    for i in 0..players_x.len() {
        let cx = (players_x[i] / 16.0).floor() as i32;
        let cz = (players_z[i] / 16.0).floor() as i32;
        for dx in -CHUNK_RADIUS..=CHUNK_RADIUS {
            for dz in -CHUNK_RADIUS..=CHUNK_RADIUS {
                eligible.push(chunk_key(cx + dx, cz + dz));
            }
        }
    }
    if eligible.is_empty() {
        return 0;
    }
    eligible.sort_unstable();
    eligible.dedup();

    let budget = if hostile { HOSTILE_BUDGET } else { PASSIVE_BUDGET };
    if current_count > alpha_spawn_max_count(eligible.len() as i32, budget) {
        return 0;
    }

    let (Some(next_int), Some(next_float), Some(chunk_exists), Some(is_solid), Some(is_air), Some(is_liquid), Some(try_spawn), Some(spawn_jockey)) =
        (world.next_int, world.next_uniform_float, world.chunk_exists, world.is_solid, world.is_air, world.is_liquid, world.try_spawn, world.spawn_jockey)
    else {
        return 0;
    };

    let mut spawned = 0;
    for key in &eligible {
        if next_int(CHUNK_ROLL) != 0 {
            continue;
        }
        let chunk_x = (key >> 32) as i32;
        let chunk_z = (key & 0xFFFF_FFFF) as i32;
        if !chunk_exists(chunk_x, chunk_z) {
            continue;
        }

        let base_x = chunk_x.wrapping_mul(16);
        let base_z = chunk_z.wrapping_mul(16);
        let kind = next_int(4) as u8;
        let origin_x = base_x + next_int(16);
        let origin_y = next_int(world_height);
        let origin_z = base_z + next_int(16);

        if is_solid(origin_x, origin_y, origin_z) || !is_air(origin_x, origin_y, origin_z) {
            continue;
        }

        let mut move_to_next_chunk = false;
        for _ in 0..GROUP_ATTEMPTS {
            if move_to_next_chunk {
                break;
            }
            let mut group_count = 0;
            let (mut x, mut y, mut z) = (origin_x, origin_y, origin_z);
            for _ in 0..PACK_ATTEMPTS {
                // Java order per attempt: x-pair, y-pair, z-pair. The y draws
                // are nextInt(1) (always 0) but still consume RNG state.
                x += alpha_spawn_pack_offset(next_int(PACK_SPREAD), next_int(PACK_SPREAD));
                y += alpha_spawn_pack_offset(next_int(1), next_int(1));
                z += alpha_spawn_pack_offset(next_int(PACK_SPREAD), next_int(PACK_SPREAD));

                if !is_solid(x, y - 1, z)
                    || is_solid(x, y, z)
                    || is_liquid(x, y, z)
                    || is_solid(x, y + 1, z)
                {
                    continue;
                }

                let fx = x as f32 + 0.5;
                let fy = y as f32;
                let fz = z as f32 + 0.5;
                // 24-block nearest-player check (mirrors getClosestPlayer 3D scan).
                let mut too_close = false;
                for i in 0..players_x.len() {
                    let dx = players_x[i] - fx as f64;
                    let dy = players_y[i] - fy as f64;
                    let dz = players_z[i] - fz as f64;
                    if dx * dx + dy * dy + dz * dz < PLAYER_RANGE * PLAYER_RANGE {
                        too_close = true;
                        break;
                    }
                }
                if too_close {
                    continue;
                }
                if alpha_spawn_too_close_to_spawn(fx, fy, fz, spawn_x, spawn_y, spawn_z) {
                    continue;
                }

                let yaw = next_float(0.0, 360.0);
                let mut max_in_chunk = 4;
                let id = try_spawn(kind, fx, fy, fz, yaw, &mut max_in_chunk);
                if id < 0 {
                    continue;
                }
                spawned += 1;

                // Alpha spider jockey chance (hostile only).
                if hostile && kind == 0 && next_int(JOCKEY_ROLL) == 0 {
                    spawn_jockey(fx, fy, fz, yaw, id);
                }

                group_count += 1;
                if group_count >= max_in_chunk {
                    move_to_next_chunk = true; // Java: continue label110
                    break;
                }
            }
        }
    }
    spawned
}

/// Batch driver for `World::spawnHostileMobs`: parallel player-position
/// slices. Returns the primary spawn count.
pub fn rust_world_spawn_hostile(
    world: &SpawnerWorld,
    players_x: &[f64],
    players_y: &[f64],
    players_z: &[f64],
    current_count: i32,
    spawn_x: i32,
    spawn_y: i32,
    spawn_z: i32,
    world_height: i32,
) -> i32 {
    spawn_pass(world, players_x, players_y, players_z, true, current_count, spawn_x, spawn_y, spawn_z, world_height)
}

/// Batch driver for `World::spawnPassiveMobs`. Same contract as hostile;
/// spider-jockey logic is skipped.
pub fn rust_world_spawn_passive(
    world: &SpawnerWorld,
    players_x: &[f64],
    players_y: &[f64],
    players_z: &[f64],
    current_count: i32,
    spawn_x: i32,
    spawn_y: i32,
    spawn_z: i32,
    world_height: i32,
) -> i32 {
    spawn_pass(world, players_x, players_y, players_z, false, current_count, spawn_x, spawn_y, spawn_z, world_height)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_max_count_hostile_budget() {
        // 100 per 256 chunks (World::spawnHostileMobs).
        assert_eq!(alpha_spawn_max_count(256, 100), 100);
        assert_eq!(alpha_spawn_max_count(128, 100), 50);
        assert_eq!(alpha_spawn_max_count(0, 100), 0);
        // Truncation like C++ int division: 100*1/256 == 0.
        assert_eq!(alpha_spawn_max_count(1, 100), 0);
        assert_eq!(alpha_spawn_max_count(3, 100), 1);
    }

    #[test]
    fn test_max_count_passive_budget() {
        // 20 per 256 chunks (World::spawnPassiveMobs).
        assert_eq!(alpha_spawn_max_count(256, 20), 20);
        assert_eq!(alpha_spawn_max_count(128, 20), 10);
        assert_eq!(alpha_spawn_max_count(0, 20), 0);
    }

    #[test]
    fn test_pack_offset_shape() {
        assert_eq!(alpha_spawn_pack_offset(5, 0), 5);
        assert_eq!(alpha_spawn_pack_offset(0, 5), -5);
        assert_eq!(alpha_spawn_pack_offset(3, 3), 0);
        // Triangular step always fits in [-5, 5] for spread6 draws.
        for a in 0..6 {
            for b in 0..6 {
                let d = alpha_spawn_pack_offset(a, b);
                assert!((-5..=5).contains(&d));
            }
        }
    }

    #[test]
    fn test_too_close_to_spawn() {
        // On the spawn point: excluded.
        assert!(alpha_spawn_too_close_to_spawn(0.5, 64.0, 0.5, 0, 64, 0));
        // Clearly outside: allowed.
        assert!(!alpha_spawn_too_close_to_spawn(100.5, 64.0, 100.5, 0, 64, 0));
        // Boundary is exclusive: exactly 24 blocks away (576.0) is allowed.
        assert!(!alpha_spawn_too_close_to_spawn(24.0, 64.0, 0.0, 0, 64, 0));
        // Just inside: excluded.
        assert!(alpha_spawn_too_close_to_spawn(23.5, 64.0, 0.0, 0, 64, 0));
    }

    // Batch-driver tests with a scripted stub world. All scenarios run
    // sequentially inside one test because they share stub counters.
    mod driver {
        use super::*;
        use std::sync::atomic::{AtomicI32, Ordering};

        static TRY_SPAWN_CALLS: AtomicI32 = AtomicI32::new(0);
        static JOCKEY_CALLS: AtomicI32 = AtomicI32::new(0);
        static NEXT_ID: AtomicI32 = AtomicI32::new(1);

        fn reset() {
            TRY_SPAWN_CALLS.store(0, Ordering::SeqCst);
            JOCKEY_CALLS.store(0, Ordering::SeqCst);
            NEXT_ID.store(1, Ordering::SeqCst);
        }

        // All RNG draws return 0: every 1/N roll succeeds, kinds/origins are 0.
        fn stub_next_int(bound: i32) -> i32 {
            assert!(bound > 0);
            0
        }
        fn stub_next_float(_lo: f32, _hi: f32) -> f32 {
            0.0
        }
        fn stub_chunk_exists(_x: i32, _z: i32) -> bool {
            true
        }
        // Solid ground only at y == -1, air everywhere, no liquid.
        fn stub_is_solid(_x: i32, y: i32, _z: i32) -> bool {
            y == -1
        }
        fn stub_is_air(_x: i32, _y: i32, _z: i32) -> bool {
            true
        }
        fn stub_is_liquid(_x: i32, _y: i32, _z: i32) -> bool {
            false
        }
        fn stub_try_spawn(
            _kind: u8,
            _fx: f32,
            _fy: f32,
            _fz: f32,
            _yaw: f32,
            out_max: &mut i32,
        ) -> i32 {
            TRY_SPAWN_CALLS.fetch_add(1, Ordering::SeqCst);
            *out_max = 4;
            NEXT_ID.fetch_add(1, Ordering::SeqCst)
        }
        fn stub_spawn_jockey(_fx: f32, _fy: f32, _fz: f32, _yaw: f32, _host: i32) -> bool {
            JOCKEY_CALLS.fetch_add(1, Ordering::SeqCst);
            true
        }

        fn stub_world() -> SpawnerWorld {
            SpawnerWorld {
                next_int: Some(stub_next_int),
                next_uniform_float: Some(stub_next_float),
                chunk_exists: Some(stub_chunk_exists),
                is_solid: Some(stub_is_solid),
                is_air: Some(stub_is_air),
                is_liquid: Some(stub_is_liquid),
                try_spawn: Some(stub_try_spawn),
                spawn_jockey: Some(stub_spawn_jockey),
            }
        }

        #[test]
        fn test_driver_scenarios() {
            let world = stub_world();
            // Player at origin; world spawn far away so nothing is excluded.
            let px = [0.5f64];
            let py = [64.0f64];
            let pz = [0.5f64];

            // No players, no chunks, no spawns.
            reset();
            assert_eq!(
                rust_world_spawn_hostile(&world, &[], &[], &[], 0, 1000, 64, 1000, 128),
                0
            );
            assert_eq!(TRY_SPAWN_CALLS.load(Ordering::SeqCst), 0);

            // Cap gate: 1 player -> 289 chunks -> max 112 hostile; 113 blocks everything.
            reset();
            assert_eq!(
                rust_world_spawn_hostile(&world, &px, &py, &pz, 113, 1000, 64, 1000, 128),
                0
            );
            assert_eq!(TRY_SPAWN_CALLS.load(Ordering::SeqCst), 0);

            // Happy hostile path: every chunk attempts (roll always 0), each
            // fills its group of 4 (max_in_chunk), kind 0 (spider) always
            // triggers the per-spawn jockey roll (always 0).
            // 17x17 = 289 chunks -> 289*4 primary + 289*4 jockeys.
            reset();
            let n = rust_world_spawn_hostile(&world, &px, &py, &pz, 0, 1000, 64, 1000, 128);
            assert_eq!(n, 289 * 4);
            assert_eq!(TRY_SPAWN_CALLS.load(Ordering::SeqCst), 289 * 4);
            assert_eq!(JOCKEY_CALLS.load(Ordering::SeqCst), 289 * 4);

            // Happy passive path: same totals, no jockeys.
            reset();
            let n = rust_world_spawn_passive(&world, &px, &py, &pz, 0, 1000, 64, 1000, 128);
            assert_eq!(n, 289 * 4);
            assert_eq!(TRY_SPAWN_CALLS.load(Ordering::SeqCst), 289 * 4);
            assert_eq!(JOCKEY_CALLS.load(Ordering::SeqCst), 0);

            // World-spawn exclusion: spawn at y=0 inside the eligible area
            // removes candidates (strictly fewer spawns than the happy path).
            reset();
            let n_excl = rust_world_spawn_hostile(&world, &px, &py, &pz, 0, 8, 0, 8, 128);
            assert!(n_excl < 289 * 4);
            assert_eq!(TRY_SPAWN_CALLS.load(Ordering::SeqCst), n_excl);
        }
    }
}
