//! Pure mob-spawning math ported from C++ `World::spawnHostileMobs` /
//! `World::spawnPassiveMobs` (which mirror Java `SpawnerAnimals::func_4111_a`).
//!
//! Both C++ functions duplicated the same cap formula, pack-spread step,
//! and world-spawn exclusion check. This module is the single source of
//! truth. All functions are pure (no pointers, no allocation, no RNG
//! draws inside) so they can be unit-tested in Rust and called from C++
//! one at a time while RNG draws (`World::rand`), block access, and the
//! entity list stay in C++ for now.
//!
//! Deliberately NOT moved (control flow + world access stay in C++):
//! - eligible-chunk collection (needs live player positions) and its
//!   8-chunk radius, the 1/50 per-chunk roll, the 3-group x 4-pack loop,
//!   and the 1/100 spider-jockey roll (all interleave RNG draws with
//!   block/entity queries).
//! - the 24-block nearest-player check (`getClosestPlayer`, 3D scan over
//!   live players).
//!
//! Long-term FFI-reduction direction: once the draw sequence is owned by
//! Rust `JavaRandom` (exact Java parity), the whole per-chunk attempt can
//! move behind a single batch FFI call instead of fine-grained calls.

/// Spawn cap: `budget_per_256 * num_eligible_chunks / 256`.
///
/// Mirrors `World::spawnHostileMobs` (`100 * n / 256`) and
/// `World::spawnPassiveMobs` (`20 * n / 256`), which mirror Java
/// `var29.field_4220_d * field_4311_a.size() / 256` per creature type.
/// Multiplication first, then truncating division, exactly like C++ `int`.
#[no_mangle]
pub extern "C" fn alpha_spawn_max_count(num_eligible_chunks: i32, budget_per_256: i32) -> i32 {
    budget_per_256.wrapping_mul(num_eligible_chunks).wrapping_div(256)
}

/// One pack-spread step: `first - second`.
///
/// Mirrors `x += spread6(rand) - spread6(rand)` (Java
/// `var15 += rand.nextInt(6) - rand.nextInt(6)`). Each draw is uniform in
/// `[0, 6)`, so the step is triangular in `[-5, 5]`. The two draws are
/// passed in (C++ owns the RNG stream); this function only owns the shape.
#[no_mangle]
pub extern "C" fn alpha_spawn_pack_offset(first: i32, second: i32) -> i32 {
    first.wrapping_sub(second)
}

/// World-spawn exclusion: is the candidate inside the 24-block no-spawn
/// cube around the world spawn (squared distance `< 576.0`)?
///
/// Mirrors the `dsx*dsx + dsy*dsy + dsz*dsz < 576.0f` check in both spawn
/// functions (Java `var26 >= 576.0F` gate). `f32` arithmetic matches the
/// C++ `float` computation bit-for-bit for the same inputs.
#[no_mangle]
pub extern "C" fn alpha_spawn_too_close_to_spawn(
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
}
