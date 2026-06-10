use std::sync::OnceLock;
use libc::{c_int, size_t};
use std::slice;
use crate::noise::NoiseGeneratorOctaves2;

#[repr(i32)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum BiomeType {
    Rainforest = 0,
    Swampland = 1,
    SeasonalForest = 2,
    Forest = 3,
    Savanna = 4,
    Shrubland = 5,
    Taiga = 6,
    Desert = 7,
    Plains = 8,
    IceDesert = 9,
    Tundra = 10,
    Hell = 11,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct MobSpawnerBase {
    pub biome_type: BiomeType,
    pub top_block: u8,
    pub filler_block: u8,
}

impl MobSpawnerBase {
    pub const DEFAULT: Self = Self {
        biome_type: BiomeType::Plains,
        top_block: 2,
        filler_block: 3,
    };

    pub const RAINFOREST: Self = Self { biome_type: BiomeType::Rainforest, top_block: 2, filler_block: 3 };
    pub const SWAMPLAND: Self = Self { biome_type: BiomeType::Swampland, top_block: 2, filler_block: 3 };
    pub const SEASONAL_FOREST: Self = Self { biome_type: BiomeType::SeasonalForest, top_block: 2, filler_block: 3 };
    pub const FOREST: Self = Self { biome_type: BiomeType::Forest, top_block: 2, filler_block: 3 };
    pub const SAVANNA: Self = Self { biome_type: BiomeType::Savanna, top_block: 2, filler_block: 3 };
    pub const SHRUBLAND: Self = Self { biome_type: BiomeType::Shrubland, top_block: 2, filler_block: 3 };
    pub const TAIGA: Self = Self { biome_type: BiomeType::Taiga, top_block: 2, filler_block: 3 };
    pub const DESERT: Self = Self { biome_type: BiomeType::Desert, top_block: 12, filler_block: 12 };
    pub const PLAINS: Self = Self { biome_type: BiomeType::Plains, top_block: 2, filler_block: 3 };
    pub const ICE_DESERT: Self = Self { biome_type: BiomeType::IceDesert, top_block: 12, filler_block: 12 };
    pub const TUNDRA: Self = Self { biome_type: BiomeType::Tundra, top_block: 2, filler_block: 3 };
    pub const HELL: Self = Self { biome_type: BiomeType::Hell, top_block: 2, filler_block: 3 };
}

pub fn get_biome(temp: f32, mut humid: f32) -> MobSpawnerBase {
    humid *= temp;
    if temp < 0.1 {
        return MobSpawnerBase::TUNDRA;
    }
    if humid < 0.2 {
        if temp < 0.5 {
            return MobSpawnerBase::TUNDRA;
        }
        if temp < 0.95 {
            return MobSpawnerBase::SAVANNA;
        }
        return MobSpawnerBase::DESERT;
    }
    if humid > 0.5 && temp < 0.7 {
        return MobSpawnerBase::SWAMPLAND;
    }
    if temp < 0.5 {
        return MobSpawnerBase::TAIGA;
    }
    if temp < 0.97 {
        if humid < 0.35 {
            return MobSpawnerBase::SHRUBLAND;
        }
        return MobSpawnerBase::FOREST;
    }
    if humid < 0.45 {
        return MobSpawnerBase::PLAINS;
    }
    if humid < 0.9 {
        return MobSpawnerBase::SEASONAL_FOREST;
    }
    MobSpawnerBase::RAINFOREST
}

static BIOME_LOOKUP_TABLE: OnceLock<[MobSpawnerBase; 4096]> = OnceLock::new();

fn init_biome_lookup() -> &'static [MobSpawnerBase; 4096] {
    BIOME_LOOKUP_TABLE.get_or_init(|| {
        let mut table = [MobSpawnerBase::DEFAULT; 4096];
        for i in 0..64 {
            for j in 0..64 {
                table[i + j * 64] = get_biome(i as f32 / 63.0f32, j as f32 / 63.0f32);
            }
        }
        table
    })
}

pub fn generate_biome_lookup() {
    init_biome_lookup();
}

pub fn get_biome_from_lookup(temp: f64, humid: f64) -> MobSpawnerBase {
    let table = init_biome_lookup();
    let mut t = (temp as f32 * 63.0) as i32;
    let mut h = (humid as f32 * 63.0) as i32;
    if t < 0 { t = 0; }
    if t > 63 { t = 63; }
    if h < 0 { h = 0; }
    if h > 63 { h = 63; }
    table[(t + h * 64) as usize]
}

#[no_mangle]
pub unsafe extern "C" fn alpha_biome_get_temperatures(
    temp_noise_gen: *mut NoiseGeneratorOctaves2,
    noise_gen3: *mut NoiseGeneratorOctaves2,
    out_temps: *mut f64,
    out_len: size_t,
    x: c_int,
    z: c_int,
    x_size: c_int,
    z_size: c_int,
) {
    if temp_noise_gen.is_null() || noise_gen3.is_null() || out_temps.is_null() || out_len == 0 {
        return;
    }
    let temp_gen = &*temp_noise_gen;
    let n3_gen = &*noise_gen3;
    let temps = slice::from_raw_parts_mut(out_temps, out_len);

    temp_gen.func_4101_a(temps, x as f64, z as f64, x_size as usize, z_size as usize, 0.025, 0.025, 0.25);

    let mut field_4257_c = vec![0.0; x_size as usize * z_size as usize];
    n3_gen.func_4101_a(&mut field_4257_c, x as f64, z as f64, x_size as usize, z_size as usize, 0.25, 0.25, 0.5882352941176471);

    let mut idx = 0;
    for _i in 0..x_size {
        for _j in 0..z_size {
            let noise = field_4257_c[idx] * 1.1 + 0.5;
            let d1 = 0.01;
            let d2 = 1.0 - d1;
            let mut temp = (temps[idx] * 0.15 + 0.7) * d2 + noise * d1;
            temp = 1.0 - (1.0 - temp) * (1.0 - temp);
            if temp < 0.0 { temp = 0.0; }
            if temp > 1.0 { temp = 1.0; }
            temps[idx] = temp;
            idx += 1;
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn alpha_biome_load_block_generator_data(
    temp_noise_gen: *mut NoiseGeneratorOctaves2,
    humid_noise_gen: *mut NoiseGeneratorOctaves2,
    noise_gen3: *mut NoiseGeneratorOctaves2,
    out_biomes: *mut MobSpawnerBase,
    out_temps: *mut f64,
    out_humids: *mut f64,
    out_len: size_t,
    x: c_int,
    z: c_int,
    x_size: c_int,
    z_size: c_int,
) {
    if temp_noise_gen.is_null() || humid_noise_gen.is_null() || noise_gen3.is_null()
        || out_biomes.is_null() || out_temps.is_null() || out_humids.is_null() || out_len == 0
    {
        return;
    }
    let temp_gen = &*temp_noise_gen;
    let humid_gen = &*humid_noise_gen;
    let n3_gen = &*noise_gen3;

    let biomes = slice::from_raw_parts_mut(out_biomes, out_len);
    let temperatures = slice::from_raw_parts_mut(out_temps, out_len);
    let humidities = slice::from_raw_parts_mut(out_humids, out_len);

    temp_gen.func_4101_a(temperatures, x as f64, z as f64, x_size as usize, z_size as usize, 0.025, 0.025, 0.25);
    humid_gen.func_4101_a(humidities, x as f64, z as f64, x_size as usize, z_size as usize, 0.05, 0.05, 1.0 / 3.0);

    let mut field_4257_c = vec![0.0; x_size as usize * z_size as usize];
    n3_gen.func_4101_a(&mut field_4257_c, x as f64, z as f64, x_size as usize, z_size as usize, 0.25, 0.25, 0.5882352941176471);

    let mut idx = 0;
    for _i in 0..x_size {
        for _j in 0..z_size {
            let noise = field_4257_c[idx] * 1.1 + 0.5;

            let d1_t = 0.01;
            let d2_t = 1.0 - d1_t;
            let mut temp = (temperatures[idx] * 0.15 + 0.7) * d2_t + noise * d1_t;

            let d1_h = 0.002;
            let d2_h = 1.0 - d1_h;
            let mut humid = (humidities[idx] * 0.15 + 0.5) * d2_h + noise * d1_h;

            temp = 1.0 - (1.0 - temp) * (1.0 - temp);
            if temp < 0.0 { temp = 0.0; }
            if humid < 0.0 { humid = 0.0; }
            if temp > 1.0 { temp = 1.0; }
            if humid > 1.0 { humid = 1.0; }

            temperatures[idx] = temp;
            humidities[idx] = humid;
            biomes[idx] = get_biome_from_lookup(temp, humid);
            idx += 1;
        }
    }
}
