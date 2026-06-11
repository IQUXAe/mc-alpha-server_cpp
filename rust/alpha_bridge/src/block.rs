use std::mem;

#[repr(u8)]
#[derive(Clone, Copy, PartialEq)]
pub enum BlockMaterial {
    Air = 0,
    Ground = 1,
    Wood = 2,
    Rock = 3,
    Iron = 4,
    Water = 5,
    Lava = 6,
    Leaves = 7,
    Plants = 8,
    Sponge = 9,
    Cloth = 10,
    Fire = 11,
    Sand = 12,
    Circuits = 13,
    Glass = 14,
    Tnt = 15,
    Unused = 16,
    Ice = 17,
    Snow = 18,
    BuiltSnow = 19,
    Cactus = 20,
    Clay = 21,
    Pumpkin = 22,
    Portal = 23,
    Web = 24,
}

#[repr(u8)]
#[derive(Clone, Copy, PartialEq)]
pub enum BlockType {
    Normal = 0,
    Sand = 1,
    Fluid = 2,
    Flower = 3,
    TallGrass = 4,
    Mushroom = 5,
    Torch = 6,
    Cactus = 7,
    Reed = 8,
    Leaves = 9,
    Sapling = 10,
    Crops = 11,
    Soil = 12,
    Fire = 13,
    Ore = 14,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct AlphaBlockProperties {
    pub hardness: f32,
    pub resistance: f32,
    pub light_opacity: i32,
    pub light_value: i32,
    pub tick_on_load: bool,
    pub is_block_container: bool,
    pub allows_attachment: bool,
    pub material: u8,
    pub block_type: u8,
    pub id_dropped: i32,
    pub quantity_dropped: i32,
    pub can_harvest_block: bool,
    pub min_x: f32,
    pub min_y: f32,
    pub min_z: f32,
    pub max_x: f32,
    pub max_y: f32,
    pub max_z: f32,
}

macro_rules! b {
    ($hardness:expr, $resistance:expr, $mat:ident, $opacity:expr, $light:expr,
     $tick:expr, $container:expr, $attach:expr, $type:ident,
     $dropped:expr, $qty:expr, $harvest:expr $(,)?) => {
        AlphaBlockProperties {
            hardness: $hardness,
            resistance: $resistance,
            material: BlockMaterial::$mat as u8,
            light_opacity: $opacity,
            light_value: $light,
            tick_on_load: $tick,
            is_block_container: $container,
            allows_attachment: $attach,
            block_type: BlockType::$type as u8,
            id_dropped: $dropped,
            quantity_dropped: $qty,
            can_harvest_block: $harvest,
            min_x: 0.0, min_y: 0.0, min_z: 0.0,
            max_x: 1.0, max_y: 1.0, max_z: 1.0,
        }
    };
    ($hardness:expr, $resistance:expr, $mat:ident, $opacity:expr, $light:expr,
     $tick:expr, $container:expr, $attach:expr, $type:ident,
     $dropped:expr, $qty:expr, $harvest:expr,
     $minx:expr, $miny:expr, $minz:expr, $maxx:expr, $maxy:expr, $maxz:expr $(,)?) => {
        AlphaBlockProperties {
            hardness: $hardness,
            resistance: $resistance,
            material: BlockMaterial::$mat as u8,
            light_opacity: $opacity,
            light_value: $light,
            tick_on_load: $tick,
            is_block_container: $container,
            allows_attachment: $attach,
            block_type: BlockType::$type as u8,
            id_dropped: $dropped,
            quantity_dropped: $qty,
            can_harvest_block: $harvest,
            min_x: $minx, min_y: $miny, min_z: $minz,
            max_x: $maxx, max_y: $maxy, max_z: $maxz,
        }
    };
}

const fn hardness_default_resistance(h: f32) -> f32 {
    if h * 5.0 > 0.0 { h * 5.0 } else { 0.0 }
}

const AIR: AlphaBlockProperties = AlphaBlockProperties {
    hardness: 0.0, resistance: 0.0,
    material: BlockMaterial::Air as u8,
    light_opacity: 0, light_value: 0,
    tick_on_load: false, is_block_container: false, allows_attachment: true,
    block_type: BlockType::Normal as u8,
    id_dropped: 0, quantity_dropped: 0, can_harvest_block: true,
    min_x: 0.0, min_y: 0.0, min_z: 0.0,
    max_x: 1.0, max_y: 1.0, max_z: 1.0,
};

static BLOCK_PROPS: [AlphaBlockProperties; 256] = {
    let mut props: [AlphaBlockProperties; 256] = [AIR; 256];

    props[1] = b!(1.5, 30.0, Rock, 255, 0, false, false, true, Normal, 4, 1, false);
    props[2] = b!(0.6, 3.0, Ground, 255, 0, false, false, true, Normal, 3, 1, true);
    props[3] = b!(0.5, 2.5, Ground, 255, 0, false, false, true, Normal, 3, 1, true);
    props[4] = b!(2.0, 30.0, Rock, 255, 0, false, false, true, Normal, 4, 1, true);
    props[5] = b!(2.0, 15.0, Wood, 255, 0, false, false, true, Normal, 5, 1, true);
    props[6] = b!(0.0, 0.0, Plants, 0, 0, true, false, false, Sapling, 0, 1, true,
        0.1, 0.0, 0.1, 0.9, 0.8, 0.9);
    props[7] = b!(-1.0, 18000000.0, Rock, 255, 0, false, false, true, Normal, 7, 1, true);
    props[8] = b!(0.0, 0.0, Water, 3, 0, false, false, false, Fluid, 8, 1, true);
    props[9] = b!(0.0, 0.0, Water, 3, 0, false, false, false, Fluid, 9, 1, true);
    props[10] = b!(0.0, 0.0, Lava, 255, 0, false, false, false, Fluid, 10, 1, true);
    props[11] = b!(0.0, 0.0, Lava, 255, 0, false, false, false, Fluid, 11, 1, true);
    props[12] = b!(0.5, 2.5, Sand, 255, 0, false, false, true, Sand, 12, 1, true);
    props[13] = b!(0.6, 3.0, Sand, 255, 0, false, false, true, Sand, 13, 1, true);
    props[14] = b!(3.0, 15.0, Rock, 255, 0, false, false, true, Ore, 14, 1, true);
    props[15] = b!(3.0, 15.0, Rock, 255, 0, false, false, true, Ore, 15, 1, true);
    props[16] = b!(3.0, 15.0, Rock, 255, 0, false, false, true, Ore, 263, 1, true);
    props[17] = b!(2.0, 10.0, Wood, 255, 0, false, false, true, Normal, 17, 1, true);
    props[18] = b!(0.2, 1.0, Leaves, 1, 0, true, false, false, Leaves, 0, 0, true);
    props[19] = b!(0.4, 2.0, Ground, 255, 0, false, false, true, Normal, 19, 1, true);
    props[20] = b!(0.3, 1.5, Glass, 0, 0, false, false, true, Normal, 20, 1, true);
    props[21] = b!(3.0, 15.0, Rock, 255, 0, false, false, true, Ore, 351, 1, true);
    props[22] = b!(3.0, 15.0, Rock, 255, 0, false, false, true, Normal, 22, 1, true);
    props[23] = b!(3.5, 17.5, Rock, 255, 0, false, false, true, Normal, 23, 1, true);
    props[24] = b!(0.8, 4.0, Rock, 255, 0, false, false, true, Normal, 24, 1, true);
    props[25] = b!(0.8, 4.0, Wood, 255, 0, false, false, true, Normal, 25, 1, true);
    props[27] = b!(0.7, 3.5, Ground, 255, 0, false, false, true, Normal, 27, 1, true);
    props[28] = b!(0.7, 3.5, Ground, 255, 0, false, false, true, Normal, 28, 1, true);
    props[29] = b!(3.5, 17.5, Rock, 255, 0, false, false, true, Normal, 29, 1, true);
    props[30] = b!(4.0, 20.0, Web, 1, 0, false, false, true, Normal, 30, 1, true);
    props[31] = b!(0.0, 0.0, Plants, 0, 0, true, false, false, TallGrass, 0, 0, true,
        0.3, 0.0, 0.3, 0.7, 0.6, 0.7);
    props[32] = b!(0.0, 0.0, Plants, 0, 0, false, false, false, Normal, 0, 0, true);
    props[33] = b!(3.5, 17.5, Rock, 255, 0, false, false, true, Normal, 33, 1, true);
    props[35] = b!(0.8, 4.0, Cloth, 255, 0, false, false, true, Normal, 35, 1, true);
    props[37] = b!(0.0, 0.0, Plants, 0, 0, true, false, false, Flower, 0, 1, true,
        0.3, 0.0, 0.3, 0.7, 0.6, 0.7);
    props[38] = b!(0.0, 0.0, Plants, 0, 0, true, false, false, Flower, 0, 1, true,
        0.3, 0.0, 0.3, 0.7, 0.6, 0.7);
    props[39] = b!(0.0, 0.0, Plants, 0, 0, false, false, false, Mushroom, 0, 1, true);
    props[40] = b!(0.0, 0.0, Plants, 0, 0, false, false, false, Mushroom, 0, 1, true);
    props[41] = b!(3.0, 15.0, Iron, 255, 0, false, false, true, Normal, 41, 1, true);
    props[42] = b!(5.0, 25.0, Iron, 255, 0, false, false, true, Normal, 42, 1, true);
    props[43] = b!(2.0, 10.0, Rock, 255, 0, false, false, true, Normal, 43, 1, true);
    props[44] = b!(2.0, 10.0, Rock, 0, 0, false, false, true, Normal, 44, 1, true);
    props[45] = b!(2.0, 10.0, Rock, 255, 0, false, false, true, Normal, 45, 1, true);
    props[46] = b!(0.0, 0.0, Tnt, 255, 0, false, false, true, Normal, 46, 1, true);
    props[47] = b!(1.5, 7.5, Wood, 255, 0, false, false, true, Normal, 47, 1, true);
    props[48] = b!(2.0, 30.0, Rock, 255, 0, false, false, true, Normal, 48, 1, true);
    props[49] = b!(2000.0, 30000.0, Rock, 255, 0, false, false, true, Normal, 49, 1, true);
    props[50] = b!(0.0, 0.0, Circuits, 0, 0, false, false, false, Torch, 50, 1, true);
    props[51] = b!(0.0, 0.0, Fire, 255, 0, true, false, false, Fire, 0, 0, true);
    props[52] = b!(5.0, 25.0, Rock, 255, 0, false, false, true, Normal, 52, 1, true);
    props[53] = b!(2.0, 10.0, Wood, 0, 0, false, false, true, Normal, 53, 1, true);
    props[54] = b!(2.5, 12.5, Wood, 255, 0, false, false, true, Normal, 54, 1, true);
    props[55] = b!(0.0, 0.0, Circuits, 0, 0, false, false, true, Normal, 55, 1, true);
    props[56] = b!(3.0, 15.0, Rock, 255, 0, false, false, true, Ore, 264, 1, true);
    props[57] = b!(5.0, 25.0, Iron, 255, 0, false, false, true, Normal, 57, 1, true);
    props[58] = b!(2.5, 12.5, Wood, 255, 0, false, false, true, Normal, 58, 1, true);
    props[59] = b!(0.0, 0.0, Plants, 0, 0, true, false, false, Crops, 0, 0, true);
    props[60] = b!(0.6, 3.0, Ground, 255, 0, true, false, false, Soil, 3, 1, true,
        0.0, 0.0, 0.0, 1.0, 0.9375, 1.0);
    props[61] = b!(3.5, 17.5, Rock, 255, 0, false, true, true, Normal, 61, 1, true);
    props[62] = b!(3.5, 17.5, Rock, 255, 0, false, true, true, Normal, 62, 1, true);
    props[63] = b!(1.0, 5.0, Wood, 0, 0, false, true, true, Normal, 63, 1, true);
    props[64] = b!(3.0, 15.0, Wood, 255, 0, false, false, true, Normal, 64, 1, true);
    props[65] = b!(0.4, 2.0, Wood, 255, 0, false, false, true, Normal, 65, 1, true);
    props[66] = b!(0.7, 3.5, Ground, 255, 0, false, false, true, Normal, 66, 1, true);
    props[67] = b!(2.0, 10.0, Rock, 0, 0, false, false, true, Normal, 67, 1, true);
    props[68] = b!(1.0, 5.0, Wood, 0, 0, false, true, true, Normal, 68, 1, true);
    props[69] = b!(0.5, 2.5, Circuits, 255, 0, false, false, true, Normal, 69, 1, true);
    props[70] = b!(0.5, 2.5, Rock, 255, 0, false, false, true, Normal, 70, 1, true);
    props[71] = b!(3.0, 15.0, Iron, 255, 0, false, false, true, Normal, 71, 1, true);
    props[72] = b!(0.5, 2.5, Wood, 255, 0, false, false, true, Normal, 72, 1, true);
    props[73] = b!(3.0, 15.0, Rock, 255, 0, false, false, true, Ore, 331, 1, true);
    props[74] = b!(3.0, 15.0, Rock, 255, 0, false, false, true, Ore, 331, 1, true);
    props[75] = b!(0.0, 0.0, Circuits, 255, 0, false, false, true, Normal, 75, 1, true);
    props[76] = b!(0.0, 0.0, Circuits, 255, 0, false, false, true, Normal, 76, 1, true);
    props[77] = b!(0.5, 2.5, Circuits, 255, 0, false, false, true, Normal, 77, 1, true);
    props[78] = b!(0.1, 0.5, Snow, 255, 0, false, false, true, Normal, 78, 1, true);
    props[79] = b!(0.5, 2.5, Ice, 3, 0, false, false, true, Normal, 79, 1, true);
    props[80] = b!(0.2, 1.0, Snow, 255, 0, false, false, true, Normal, 80, 1, true);
    props[81] = b!(0.4, 2.0, Cactus, 0, 0, true, false, false, Cactus, 81, 1, true,
        0.0625, 0.0, 0.0625, 0.9375, 1.0, 0.9375);
    props[82] = b!(0.6, 3.0, Clay, 255, 0, false, false, true, Normal, 82, 1, true);
    props[83] = b!(0.0, 0.0, Plants, 0, 0, true, false, false, Reed, 0, 1, true);
    props[84] = b!(0.8, 4.0, Wood, 255, 0, false, false, true, Normal, 84, 1, true);
    props[85] = b!(2.0, 10.0, Wood, 255, 0, false, false, true, Normal, 85, 1, true);
    props[86] = b!(1.0, 5.0, Pumpkin, 255, 0, false, false, true, Normal, 86, 1, true);
    props[87] = b!(0.4, 2.0, Rock, 255, 0, false, false, true, Normal, 87, 1, true);
    props[88] = b!(0.5, 2.5, Sand, 255, 0, false, false, true, Normal, 88, 1, true);
    props[89] = b!(0.3, 1.5, Rock, 255, 0, false, false, true, Normal, 89, 1, true);
    props[91] = b!(1.0, 5.0, Pumpkin, 255, 0, false, false, true, Normal, 91, 1, true);

    props
};

#[no_mangle]
pub extern "C" fn alpha_block_properties_get(id: u32) -> AlphaBlockProperties {
    if id >= 256 {
        return AIR;
    }
    BLOCK_PROPS[id as usize]
}
