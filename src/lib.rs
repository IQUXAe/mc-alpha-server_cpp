//! Alpha 1.2.6 server in native Rust.
//!
//! `repr(C)` structs are legacy FFI shapes kept for the packet/slot layout
//! (no C++ bridge remains); the `unsafe` blocks left sit behind documented
//! guards around the world tree bridge and the generator canvas
//! (see `world/shims.rs`), and `unsafe_code` stays visible in review
//! until those are dissolved too.
//!
//! Layout: `world/` (storage in `mod`, one responsibility per submodule:
//! physics, living, AI, combat, spawning, blocks, gen, shims) +
//! `session` (transport+play) + `server` (tick fan-out); packet builders
//! live in `session_packets`, tile rows in `world::tiles`, and each
//! god-object's tests in adjacent `tests` submodules.
pub mod block;
pub mod block_container;
pub mod block_fire;
pub mod block_ticks;
pub mod entity_ai;
pub mod entity_living;
pub mod entity_misc;
pub mod entity_physics;
pub mod entity_player;
pub mod entity_table;
pub mod byte_buffer;
pub mod inventory;
pub mod tile_entity_furnace;
pub mod tile_entity_chest;
pub mod tile_entity_sign;
pub mod random;
pub mod noise;
pub mod biome;
pub mod density;
pub mod caves;
pub mod decorators;
pub mod nbt;
pub mod chunk;
pub mod network;
pub mod generator;
pub mod pathfinder;
pub mod persist;
pub mod commands;
pub mod player_inventory;
pub mod player_combat;
pub mod player_movement;
pub mod player_mining;
pub mod player_digging;
pub mod tracker_math;
pub mod mob_spawning;
pub mod server;
pub mod server_admin;
pub mod tracker;
pub mod session;
pub mod session_packets;
pub mod world;
pub mod material;
pub mod nibble;
pub mod server_config;
pub mod server_log;
pub mod aabb;
pub mod math_helper;
pub mod server_constants;
pub mod vec3d;
pub mod item_data;
pub mod item_use;
pub mod item_verbs;

use crate::random::JavaRandom;

/// Grow a small tree through a block accessor (sapling path).
pub fn generate_tree(
    accessor: &mut dyn crate::decorators::BlockAccess,
    seed: i64,
    x: i32,
    y: i32,
    z: i32,
) -> bool {
    let mut rand = JavaRandom::new(seed);
    crate::decorators::trees::WorldGenTrees::new().generate(accessor, &mut rand, x, y, z)
}

/// Grow a big tree through a block accessor (1/10 sapling roll).
pub fn generate_big_tree(
    accessor: &mut dyn crate::decorators::BlockAccess,
    seed: i64,
    x: i32,
    y: i32,
    z: i32,
) -> bool {
    let mut rand = JavaRandom::new(seed);
    let mut big_tree = crate::decorators::trees::WorldGenBigTree::new();
    big_tree.func_420_a(1.0, 1.0, 1.0);
    big_tree.generate(accessor, &mut rand, x, y, z)
}
