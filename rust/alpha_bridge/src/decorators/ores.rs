use crate::random::JavaRandom;
use super::WorldAccessor;

pub struct WorldGenMinable {
    minable_block_id: u8,
    number_of_blocks: i32,
}

impl WorldGenMinable {
    pub fn new(block_id: u8, count: i32) -> Self {
        Self {
            minable_block_id: block_id,
            number_of_blocks: count,
        }
    }

    pub fn generate(&self, accessor: &WorldAccessor, rand: &mut JavaRandom, x: i32, y: i32, z: i32) -> bool {
        let var6 = rand.next_float() * std::f32::consts::PI;
        let var7 = (x + 8) as f64 + (var6.sin() * (self.number_of_blocks as f32) / 8.0) as f64;
        let var9 = (x + 8) as f64 - (var6.sin() * (self.number_of_blocks as f32) / 8.0) as f64;
        let var11 = (z + 8) as f64 + (var6.cos() * (self.number_of_blocks as f32) / 8.0) as f64;
        let var13 = (z + 8) as f64 - (var6.cos() * (self.number_of_blocks as f32) / 8.0) as f64;
        let var15 = (y + rand.next_int_bound(3) + 2) as f64;
        let var17 = (y + rand.next_int_bound(3) + 2) as f64;

        for var19 in 0..=self.number_of_blocks {
            let var20 = var7 + (var9 - var7) * (var19 as f64) / (self.number_of_blocks as f64);
            let var22 = var15 + (var17 - var15) * (var19 as f64) / (self.number_of_blocks as f64);
            let var24 = var11 + (var13 - var11) * (var19 as f64) / (self.number_of_blocks as f64);
            let var26 = rand.next_double() * (self.number_of_blocks as f64) / 16.0;
            let var28 = (((var19 as f32) * std::f32::consts::PI / (self.number_of_blocks as f32)).sin() + 1.0) as f64 * var26 + 1.0;
            let var30 = (((var19 as f32) * std::f32::consts::PI / (self.number_of_blocks as f32)).sin() + 1.0) as f64 * var26 + 1.0;

            let start_x = (var20 - var28 / 2.0) as i32;
            let end_x = (var20 + var28 / 2.0) as i32;
            let start_y = (var22 - var30 / 2.0) as i32;
            let end_y = (var22 + var30 / 2.0) as i32;
            let start_z = (var24 - var28 / 2.0) as i32;
            let end_z = (var24 + var28 / 2.0) as i32;

            for var32 in start_x..=end_x {
                for var33 in start_y..=end_y {
                    for var34 in start_z..=end_z {
                        let var35 = ((var32 as f64) + 0.5 - var20) / (var28 / 2.0);
                        let var37 = ((var33 as f64) + 0.5 - var22) / (var30 / 2.0);
                        let var39 = ((var34 as f64) + 0.5 - var24) / (var28 / 2.0);
                        if var35 * var35 + var37 * var37 + var39 * var39 < 1.0 {
                            if (accessor.get_block_id)(var32, var33, var34) == 1 { // stone
                                (accessor.set_block_id)(var32, var33, var34, self.minable_block_id);
                            }
                        }
                    }
                }
            }
        }
        true
    }
}

pub struct WorldGenClay {
    clay_block_id: u8,
    number_of_blocks: i32,
}

impl WorldGenClay {
    pub fn new(count: i32) -> Self {
        Self {
            clay_block_id: 82, // clay block
            number_of_blocks: count,
        }
    }

    pub fn generate(&self, accessor: &WorldAccessor, rand: &mut JavaRandom, x: i32, y: i32, z: i32) -> bool {
        let bid = (accessor.get_block_id)(x, y, z);
        if bid != 8 && bid != 9 { // water
            return false;
        }

        let var6 = rand.next_float() * std::f32::consts::PI;
        let var7 = (x + 8) as f64 + (var6.sin() * (self.number_of_blocks as f32) / 8.0) as f64;
        let var9 = (x + 8) as f64 - (var6.sin() * (self.number_of_blocks as f32) / 8.0) as f64;
        let var11 = (z + 8) as f64 + (var6.cos() * (self.number_of_blocks as f32) / 8.0) as f64;
        let var13 = (z + 8) as f64 - (var6.cos() * (self.number_of_blocks as f32) / 8.0) as f64;
        let var15 = (y + rand.next_int_bound(3) + 2) as f64;
        let var17 = (y + rand.next_int_bound(3) + 2) as f64;

        for var19 in 0..=self.number_of_blocks {
            let var20 = var7 + (var9 - var7) * (var19 as f64) / (self.number_of_blocks as f64);
            let var22 = var15 + (var17 - var15) * (var19 as f64) / (self.number_of_blocks as f64);
            let var24 = var11 + (var13 - var11) * (var19 as f64) / (self.number_of_blocks as f64);
            let var26 = rand.next_double() * (self.number_of_blocks as f64) / 16.0;
            let var28 = (((var19 as f32) * std::f32::consts::PI / (self.number_of_blocks as f32)).sin() + 1.0) as f64 * var26 + 1.0;
            let var30 = (((var19 as f32) * std::f32::consts::PI / (self.number_of_blocks as f32)).sin() + 1.0) as f64 * var26 + 1.0;

            let start_x = (var20 - var28 / 2.0) as i32;
            let end_x = (var20 + var28 / 2.0) as i32;
            let start_y = (var22 - var30 / 2.0) as i32;
            let end_y = (var22 + var30 / 2.0) as i32;
            let start_z = (var24 - var28 / 2.0) as i32;
            let end_z = (var24 + var28 / 2.0) as i32;

            for var32 in start_x..=end_x {
                for var33 in start_y..=end_y {
                    for var34 in start_z..=end_z {
                        let var35 = ((var32 as f64) + 0.5 - var20) / (var28 / 2.0);
                        let var37 = ((var33 as f64) + 0.5 - var22) / (var30 / 2.0);
                        let var39 = ((var34 as f64) + 0.5 - var24) / (var28 / 2.0);
                        if var35 * var35 + var37 * var37 + var39 * var39 < 1.0 {
                            if (accessor.get_block_id)(var32, var33, var34) == 12 { // sand
                                (accessor.set_block_id)(var32, var33, var34, self.clay_block_id);
                            }
                        }
                    }
                }
            }
        }
        true
    }
}
