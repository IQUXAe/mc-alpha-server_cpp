pub mod ores;
pub mod trees;
pub mod misc;

use crate::random::JavaRandom;
use crate::noise::NoiseGeneratorOctaves;
use crate::biome::BiomeType;

use ores::{WorldGenMinable, WorldGenClay};
use trees::{WorldGenTrees, WorldGenBigTree};
use misc::{WorldGenLakes, WorldGenFlowers, WorldGenReed, WorldGenCactus, WorldGenPumpkin, WorldGenLiquids, WorldGenDungeons};

#[repr(C)]
pub struct WorldAccessor {
    pub get_block_id: extern "C" fn(x: i32, y: i32, z: i32) -> u8,
    pub set_block_id: extern "C" fn(x: i32, y: i32, z: i32, id: u8),
    pub get_block_meta: extern "C" fn(x: i32, y: i32, z: i32) -> u8,
    pub set_block_meta: extern "C" fn(x: i32, y: i32, z: i32, meta: u8),
    pub allows_attachment: extern "C" fn(x: i32, y: i32, z: i32) -> bool,
    pub is_block_solid: extern "C" fn(x: i32, y: i32, z: i32) -> bool,
    pub get_height_value: extern "C" fn(x: i32, z: i32) -> i32,
}

#[no_mangle]
pub unsafe extern "C" fn alpha_decorate_chunk(
    accessor: WorldAccessor,
    seed: i64,
    chunk_x: i32,
    chunk_z: i32,
    biome_type_raw: i32,
    noise_gen_713: *mut NoiseGeneratorOctaves,
    temperatures: *const f64,
) {
    let var4 = chunk_x * 16;
    let var5 = chunk_z * 16;

    let biome_type = match biome_type_raw {
        0 => BiomeType::Rainforest,
        1 => BiomeType::Swampland,
        2 => BiomeType::SeasonalForest,
        3 => BiomeType::Forest,
        4 => BiomeType::Savanna,
        5 => BiomeType::Shrubland,
        6 => BiomeType::Taiga,
        7 => BiomeType::Desert,
        8 => BiomeType::Plains,
        9 => BiomeType::IceDesert,
        10 => BiomeType::Tundra,
        11 => BiomeType::Hell,
        _ => BiomeType::Plains,
    };

    let mut rand = JavaRandom::new(seed);
    let var7 = rand.next_long() / 2 * 2 + 1;
    let var9 = rand.next_long() / 2 * 2 + 1;
    rand.set_seed((chunk_x as i64).wrapping_mul(var7).wrapping_add((chunk_z as i64).wrapping_mul(var9)) ^ seed);

    // --- Water lakes ---
    if rand.next_int_bound(4) == 0 {
        let var13 = var4 + rand.next_int_bound(16) + 8;
        let var14 = rand.next_int_bound(128);
        let var15 = var5 + rand.next_int_bound(16) + 8;
        WorldGenLakes::new(8).generate(&accessor, &mut rand, var13, var14, var15);
    }

    // --- Lava lakes ---
    if rand.next_int_bound(8) == 0 {
        let var13 = var4 + rand.next_int_bound(16) + 8;
        let step1 = rand.next_int_bound(120) + 8;
        let var14 = rand.next_int_bound(step1);
        let var15 = var5 + rand.next_int_bound(16) + 8;
        if var14 < 64 || rand.next_int_bound(10) == 0 {
            WorldGenLakes::new(10).generate(&accessor, &mut rand, var13, var14, var15);
        }
    }

    // --- Dungeons ---
    for _ in 0..8 {
        let dx = var4 + rand.next_int_bound(16) + 8;
        let dy = rand.next_int_bound(128);
        let dz = var5 + rand.next_int_bound(16) + 8;
        WorldGenDungeons::new().generate(&accessor, &mut rand, dx, dy, dz);
    }

    // --- Clay ---
    for _ in 0..10 {
        let cx = var4 + rand.next_int_bound(16);
        let cy = rand.next_int_bound(128);
        let cz = var5 + rand.next_int_bound(16);
        WorldGenClay::new(32).generate(&accessor, &mut rand, cx, cy, cz);
    }

    // --- Dirt veins ---
    for _ in 0..20 {
        let mx = var4 + rand.next_int_bound(16);
        let my = rand.next_int_bound(128);
        let mz = var5 + rand.next_int_bound(16);
        WorldGenMinable::new(3, 32).generate(&accessor, &mut rand, mx, my, mz);
    }

    // --- Gravel veins ---
    for _ in 0..10 {
        let mx = var4 + rand.next_int_bound(16);
        let my = rand.next_int_bound(128);
        let mz = var5 + rand.next_int_bound(16);
        WorldGenMinable::new(13, 32).generate(&accessor, &mut rand, mx, my, mz);
    }

    // --- Coal ore ---
    for _ in 0..20 {
        let mx = var4 + rand.next_int_bound(16);
        let my = rand.next_int_bound(128);
        let mz = var5 + rand.next_int_bound(16);
        WorldGenMinable::new(16, 16).generate(&accessor, &mut rand, mx, my, mz);
    }

    // --- Iron ore ---
    for _ in 0..20 {
        let mx = var4 + rand.next_int_bound(16);
        let my = rand.next_int_bound(64);
        let mz = var5 + rand.next_int_bound(16);
        WorldGenMinable::new(15, 8).generate(&accessor, &mut rand, mx, my, mz);
    }

    // --- Gold ore ---
    for _ in 0..2 {
        let mx = var4 + rand.next_int_bound(16);
        let my = rand.next_int_bound(32);
        let mz = var5 + rand.next_int_bound(16);
        WorldGenMinable::new(14, 8).generate(&accessor, &mut rand, mx, my, mz);
    }

    // --- Redstone ore ---
    for _ in 0..8 {
        let mx = var4 + rand.next_int_bound(16);
        let my = rand.next_int_bound(16);
        let mz = var5 + rand.next_int_bound(16);
        WorldGenMinable::new(73, 7).generate(&accessor, &mut rand, mx, my, mz);
    }

    // --- Diamond ore ---
    for _ in 0..1 {
        let mx = var4 + rand.next_int_bound(16);
        let my = rand.next_int_bound(16);
        let mz = var5 + rand.next_int_bound(16);
        WorldGenMinable::new(56, 7).generate(&accessor, &mut rand, mx, my, mz);
    }

    // --- Trees ---
    let var11d = 0.25;
    let noise_val = if !noise_gen_713.is_null() {
        (*noise_gen_713).func_647_a((var4 as f64) * var11d, (var5 as f64) * var11d)
    } else {
        0.0
    };

    let var13t = ((noise_val / 8.0 + rand.next_double() * 4.0 + 4.0) / 3.0) as i32;
    let mut var14t = 0;
    if rand.next_int_bound(10) == 0 {
        var14t += 1;
    }

    match biome_type {
        BiomeType::Forest | BiomeType::Rainforest | BiomeType::Taiga => {
            var14t += var13t + 5;
        }
        BiomeType::SeasonalForest => {
            var14t += var13t + 2;
        }
        BiomeType::Desert | BiomeType::Tundra | BiomeType::Plains => {
            var14t -= 20;
        }
        _ => {}
    }

    // Tree type selection
    let mut use_big_tree = false;
    if rand.next_int_bound(10) == 0 {
        use_big_tree = true;
    }
    if biome_type == BiomeType::Rainforest && rand.next_int_bound(3) == 0 {
        use_big_tree = true;
    }

    for _ in 0..var14t {
        let tx = var4 + rand.next_int_bound(16) + 8;
        let tz = var5 + rand.next_int_bound(16) + 8;
        let ty = (accessor.get_height_value)(tx, tz);
        if use_big_tree {
            let mut big_tree = WorldGenBigTree::new();
            big_tree.func_420_a(1.0, 1.0, 1.0);
            big_tree.generate(&accessor, &mut rand, tx, ty, tz);
        } else {
            WorldGenTrees::new().generate(&accessor, &mut rand, tx, ty, tz);
        }
    }

    // --- Yellow flowers ---
    for _ in 0..2 {
        let fx = var4 + rand.next_int_bound(16) + 8;
        let fy = rand.next_int_bound(128);
        let fz = var5 + rand.next_int_bound(16) + 8;
        WorldGenFlowers::new(37).generate(&accessor, &mut rand, fx, fy, fz);
    }

    // --- Red flower ---
    if rand.next_int_bound(2) == 0 {
        let fx = var4 + rand.next_int_bound(16) + 8;
        let fy = rand.next_int_bound(128);
        let fz = var5 + rand.next_int_bound(16) + 8;
        WorldGenFlowers::new(38).generate(&accessor, &mut rand, fx, fy, fz);
    }

    // --- Brown mushroom ---
    if rand.next_int_bound(4) == 0 {
        let fx = var4 + rand.next_int_bound(16) + 8;
        let fy = rand.next_int_bound(128);
        let fz = var5 + rand.next_int_bound(16) + 8;
        WorldGenFlowers::new(39).generate(&accessor, &mut rand, fx, fy, fz);
    }

    // --- Red mushroom ---
    if rand.next_int_bound(8) == 0 {
        let fx = var4 + rand.next_int_bound(16) + 8;
        let fy = rand.next_int_bound(128);
        let fz = var5 + rand.next_int_bound(16) + 8;
        WorldGenFlowers::new(40).generate(&accessor, &mut rand, fx, fy, fz);
    }

    // --- Reed ---
    for _ in 0..10 {
        let rx = var4 + rand.next_int_bound(16) + 8;
        let ry = rand.next_int_bound(128);
        let rz = var5 + rand.next_int_bound(16) + 8;
        WorldGenReed::new().generate(&accessor, &mut rand, rx, ry, rz);
    }

    // --- Pumpkin ---
    if rand.next_int_bound(32) == 0 {
        let px = var4 + rand.next_int_bound(16) + 8;
        let py = rand.next_int_bound(128);
        let pz = var5 + rand.next_int_bound(16) + 8;
        WorldGenPumpkin::new().generate(&accessor, &mut rand, px, py, pz);
    }

    // --- Cactus ---
    let mut cactus_count = 0;
    if biome_type == BiomeType::Desert {
        cactus_count += 10;
    }
    for _ in 0..cactus_count {
        let cx = var4 + rand.next_int_bound(16) + 8;
        let cy = rand.next_int_bound(128);
        let cz = var5 + rand.next_int_bound(16) + 8;
        WorldGenCactus::new().generate(&accessor, &mut rand, cx, cy, cz);
    }

    // --- Underground water springs ---
    for _ in 0..50 {
        let sx = var4 + rand.next_int_bound(16) + 8;
        let step1 = rand.next_int_bound(120) + 8;
        let sy = rand.next_int_bound(step1);
        let sz = var5 + rand.next_int_bound(16) + 8;
        WorldGenLiquids::new(9).generate(&accessor, &mut rand, sx, sy, sz);
    }

    // --- Underground lava springs ---
    for _ in 0..20 {
        let sx = var4 + rand.next_int_bound(16) + 8;
        let step1 = rand.next_int_bound(112) + 8;
        let step2 = rand.next_int_bound(step1) + 8;
        let sy = rand.next_int_bound(step2);
        let sz = var5 + rand.next_int_bound(16) + 8;
        WorldGenLiquids::new(11).generate(&accessor, &mut rand, sx, sy, sz);
    }

    // --- Snow ---
    if !temperatures.is_null() {
        let temps_slice = std::slice::from_raw_parts(temperatures, 256);
        for var17 in (var4 + 8)..(var4 + 8 + 16) {
            for var18 in (var5 + 8)..(var5 + 8 + 16) {
                let var19 = var17 - (var4 + 8);
                let var20 = var18 - (var5 + 8);
                let var21 = (accessor.get_height_value)(var17, var18);
                let var22 = temps_slice[(var19 * 16 + var20) as usize] - ((var21 - 64) as f64) / 64.0 * 0.3;

                if var22 < 0.5 && var21 > 0 && var21 < 128
                    && (accessor.get_block_id)(var17, var21, var18) == 0
                    && (accessor.is_block_solid)(var17, var21 - 1, var18)
                    && (accessor.get_block_id)(var17, var21 - 1, var18) != 79 // not ice
                {
                    (accessor.set_block_id)(var17, var21, var18, 78); // snow layer
                }
            }
        }
    }
}
