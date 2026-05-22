use crate::random::JavaRandom;
use super::WorldAccessor;

// ============================================
// WorldGenLakes
// ============================================
pub struct WorldGenLakes {
    liquid_block_id: u8,
}

impl WorldGenLakes {
    pub fn new(block_id: u8) -> Self {
        Self { liquid_block_id: block_id }
    }

    pub fn generate(&self, accessor: &WorldAccessor, rand: &mut JavaRandom, mut x: i32, mut y: i32, mut z: i32) -> bool {
        x -= 8;
        z -= 8;
        while y > 0 && (accessor.get_block_id)(x, y, z) == 0 {
            y -= 1;
        }
        y -= 4;

        let mut arr = [false; 2048];
        let num_blobs = rand.next_int_bound(4) + 4;

        for _ in 0..num_blobs {
            let rx = rand.next_double() * 6.0 + 3.0;
            let ry = rand.next_double() * 4.0 + 2.0;
            let rz = rand.next_double() * 6.0 + 3.0;
            let cx = rand.next_double() * (16.0 - rx - 2.0) + 1.0 + rx / 2.0;
            let cy = rand.next_double() * (8.0 - ry - 4.0) + 2.0 + ry / 2.0;
            let cz = rand.next_double() * (16.0 - rz - 2.0) + 1.0 + rz / 2.0;

            for bx in 1..15 {
                for bz in 1..15 {
                    for by in 1..7 {
                        let dx = ((bx as f64) - cx) / (rx / 2.0);
                        let dy = ((by as f64) - cy) / (ry / 2.0);
                        let dz = ((bz as f64) - cz) / (rz / 2.0);
                        if dx * dx + dy * dy + dz * dz < 1.0 {
                            arr[(bx * 16 + bz) * 8 + by] = true;
                        }
                    }
                }
            }
        }

        // Check edges for liquids/solids
        for bx in 0..16 {
            let ibx = bx as i32;
            for bz in 0..16 {
                let ibz = bz as i32;
                for by in 0..8 {
                    let iby = by as i32;
                    let idx = (bx * 16 + bz) * 8 + by;
                    let is_edge = !arr[idx] &&
                        ((bx < 15 && arr[((bx + 1) * 16 + bz) * 8 + by]) ||
                         (bx > 0 && arr[((bx - 1) * 16 + bz) * 8 + by]) ||
                         (bz < 15 && arr[(bx * 16 + bz + 1) * 8 + by]) ||
                         (bz > 0 && arr[(bx * 16 + (bz - 1)) * 8 + by]) ||
                         (by < 7 && arr[(bx * 16 + bz) * 8 + by + 1]) ||
                         (by > 0 && arr[(bx * 16 + bz) * 8 + (by - 1)]));
                    if is_edge {
                        let bid = (accessor.get_block_id)(x + ibx, y + iby, z + ibz);
                        if by >= 4 && (bid == 8 || bid == 9 || bid == 10 || bid == 11) {
                            return false;
                        }
                        let solid = bid != 0 && bid != 8 && bid != 9 && bid != 10 && bid != 11;
                        if by < 4 && !solid && (accessor.get_block_id)(x + ibx, y + iby, z + ibz) != self.liquid_block_id {
                            return false;
                        }
                    }
                }
            }
        }

        // Place blocks
        for bx in 0..16 {
            let ibx = bx as i32;
            for bz in 0..16 {
                let ibz = bz as i32;
                for by in 0..8 {
                    let iby = by as i32;
                    if arr[(bx * 16 + bz) * 8 + by] {
                        (accessor.set_block_id)(x + ibx, y + iby, z + ibz, if by >= 4 { 0 } else { self.liquid_block_id });
                    }
                }
            }
        }

        // Grass conversion
        for bx in 0..16 {
            let ibx = bx as i32;
            for bz in 0..16 {
                let ibz = bz as i32;
                for by in 4..8 {
                    let iby = by as i32;
                    if arr[(bx * 16 + bz) * 8 + by] && (accessor.get_block_id)(x + ibx, y + iby - 1, z + ibz) == 3 {
                        (accessor.set_block_id)(x + ibx, y + iby - 1, z + ibz, 2); // grass
                    }
                }
            }
        }

        true
    }
}

// ============================================
// WorldGenFlowers
// ============================================
pub struct WorldGenFlowers {
    plant_block_id: u8,
}

impl WorldGenFlowers {
    pub fn new(block_id: u8) -> Self {
        Self { plant_block_id: block_id }
    }

    pub fn generate(&self, accessor: &WorldAccessor, rand: &mut JavaRandom, x: i32, y: i32, z: i32) -> bool {
        for _ in 0..64 {
            let fx = x + rand.next_int_bound(8) - rand.next_int_bound(8);
            let fy = y + rand.next_int_bound(4) - rand.next_int_bound(4);
            let fz = z + rand.next_int_bound(8) - rand.next_int_bound(8);
            if (accessor.get_block_id)(fx, fy, fz) == 0 && (accessor.get_block_id)(fx, fy - 1, fz) == 2 {
                (accessor.set_block_id)(fx, fy, fz, self.plant_block_id);
            }
        }
        true
    }
}

// ============================================
// WorldGenReed
// ============================================
pub struct WorldGenReed;

impl WorldGenReed {
    pub fn new() -> Self {
        Self
    }

    pub fn generate(&self, accessor: &WorldAccessor, rand: &mut JavaRandom, x: i32, y: i32, z: i32) -> bool {
        for _ in 0..20 {
            let rx = x + rand.next_int_bound(4) - rand.next_int_bound(4);
            let ry = y;
            let rz = z + rand.next_int_bound(4) - rand.next_int_bound(4);
            if (accessor.get_block_id)(rx, ry, rz) == 0 {
                let has_water = (accessor.get_block_id)(rx - 1, ry - 1, rz) == 8 || (accessor.get_block_id)(rx - 1, ry - 1, rz) == 9 ||
                                (accessor.get_block_id)(rx + 1, ry - 1, rz) == 8 || (accessor.get_block_id)(rx + 1, ry - 1, rz) == 9 ||
                                (accessor.get_block_id)(rx, ry - 1, rz - 1) == 8 || (accessor.get_block_id)(rx, ry - 1, rz - 1) == 9 ||
                                (accessor.get_block_id)(rx, ry - 1, rz + 1) == 8 || (accessor.get_block_id)(rx, ry - 1, rz + 1) == 9;
                if has_water {
                    let step1 = rand.next_int_bound(3) + 1;
                    let height = 2 + rand.next_int_bound(step1);
                    for h in 0..height {
                        let below = (accessor.get_block_id)(rx, ry + h - 1, rz);
                        if h == 0 {
                            if below != 2 && below != 3 && below != 12 { // grass, dirt, sand
                                break;
                            }
                        } else {
                            if below != 83 { // reed
                                break;
                            }
                        }
                        if (accessor.get_block_id)(rx, ry + h, rz) == 0 {
                            (accessor.set_block_id)(rx, ry + h, rz, 83); // reed
                        }
                    }
                }
            }
        }
        true
    }
}

// ============================================
// WorldGenCactus
// ============================================
pub struct WorldGenCactus;

impl WorldGenCactus {
    pub fn new() -> Self {
        Self
    }

    pub fn generate(&self, accessor: &WorldAccessor, rand: &mut JavaRandom, x: i32, y: i32, z: i32) -> bool {
        for _ in 0..10 {
            let cx = x + rand.next_int_bound(8) - rand.next_int_bound(8);
            let cy = y + rand.next_int_bound(4) - rand.next_int_bound(4);
            let cz = z + rand.next_int_bound(8) - rand.next_int_bound(8);
            if (accessor.get_block_id)(cx, cy, cz) == 0 {
                let step1 = rand.next_int_bound(3) + 1;
                let height = 1 + rand.next_int_bound(step1);
                for h in 0..height {
                    let below = (accessor.get_block_id)(cx, cy + h - 1, cz);
                    if h == 0 {
                        if below != 12 { // sand
                            break;
                        }
                    } else {
                        if below != 81 { // cactus
                            break;
                        }
                    }
                    if (accessor.get_block_id)(cx - 1, cy + h, cz) != 0 ||
                       (accessor.get_block_id)(cx + 1, cy + h, cz) != 0 ||
                       (accessor.get_block_id)(cx, cy + h, cz - 1) != 0 ||
                       (accessor.get_block_id)(cx, cy + h, cz + 1) != 0 {
                        break;
                    }
                    if (accessor.get_block_id)(cx, cy + h, cz) == 0 {
                        (accessor.set_block_id)(cx, cy + h, cz, 81); // cactus
                    }
                }
            }
        }
        true
    }
}

// ============================================
// WorldGenPumpkin
// ============================================
pub struct WorldGenPumpkin;

impl WorldGenPumpkin {
    pub fn new() -> Self {
        Self
    }

    pub fn generate(&self, accessor: &WorldAccessor, rand: &mut JavaRandom, x: i32, y: i32, z: i32) -> bool {
        for _ in 0..64 {
            let px = x + rand.next_int_bound(8) - rand.next_int_bound(8);
            let py = y + rand.next_int_bound(4) - rand.next_int_bound(4);
            let pz = z + rand.next_int_bound(8) - rand.next_int_bound(8);
            if (accessor.get_block_id)(px, py, pz) == 0 && (accessor.get_block_id)(px, py - 1, pz) == 2 {
                (accessor.set_block_id)(px, py, pz, 86); // pumpkin
                (accessor.set_block_meta)(px, py, pz, rand.next_int_bound(4) as u8);
            }
        }
        true
    }
}

// ============================================
// WorldGenLiquids
// ============================================
pub struct WorldGenLiquids {
    liquid_block_id: u8,
}

impl WorldGenLiquids {
    pub fn new(block_id: u8) -> Self {
        Self { liquid_block_id: block_id }
    }

    pub fn generate(&self, accessor: &WorldAccessor, _rand: &mut JavaRandom, x: i32, y: i32, z: i32) -> bool {
        if (accessor.get_block_id)(x, y + 1, z) != 1 { return false; } // stone above
        if (accessor.get_block_id)(x, y - 1, z) != 1 { return false; } // stone below
        let current = (accessor.get_block_id)(x, y, z);
        if current != 0 && current != 1 { return false; }

        let mut stone_count = 0;
        if (accessor.get_block_id)(x - 1, y, z) == 1 { stone_count += 1; }
        if (accessor.get_block_id)(x + 1, y, z) == 1 { stone_count += 1; }
        if (accessor.get_block_id)(x, y, z - 1) == 1 { stone_count += 1; }
        if (accessor.get_block_id)(x, y, z + 1) == 1 { stone_count += 1; }

        let mut air_count = 0;
        if (accessor.get_block_id)(x - 1, y, z) == 0 { air_count += 1; }
        if (accessor.get_block_id)(x + 1, y, z) == 0 { air_count += 1; }
        if (accessor.get_block_id)(x, y, z - 1) == 0 { air_count += 1; }
        if (accessor.get_block_id)(x, y, z + 1) == 0 { air_count += 1; }

        if stone_count == 3 && air_count == 1 {
            (accessor.set_block_id)(x, y, z, self.liquid_block_id);
        }
        true
    }
}

// ============================================
// WorldGenDungeons
// ============================================
pub struct WorldGenDungeons;

impl WorldGenDungeons {
    pub fn new() -> Self {
        Self
    }

    pub fn generate(&self, accessor: &WorldAccessor, rand: &mut JavaRandom, x: i32, y: i32, z: i32) -> bool {
        let half_x = rand.next_int_bound(2) + 2;
        let half_z = rand.next_int_bound(2) + 2;
        let height = 3;
        let mut solid_count = 0;

        // Check walls
        for dx in (x - half_x - 1)..=(x + half_x + 1) {
            for dy in (y - 1)..=(y + height + 1) {
                for dz in (z - half_z - 1)..=(z + half_z + 1) {
                    let bid = (accessor.get_block_id)(dx, dy, dz);
                    let solid = bid != 0 && bid != 8 && bid != 9 && bid != 10 && bid != 11;
                    if dy == y - 1 && !solid { return false; }
                    if dy == y + height + 1 && !solid { return false; }
                    if (dx == x - half_x - 1 || dx == x + half_x + 1 || dz == z - half_z - 1 || dz == z + half_z + 1) &&
                        dy == y && (accessor.get_block_id)(dx, dy, dz) == 0 && (accessor.get_block_id)(dx, dy + 1, dz) == 0
                    {
                        solid_count += 1;
                    }
                }
            }
        }

        if solid_count >= 1 && solid_count <= 5 {
            // Hollow out and place walls
            for dx in (x - half_x - 1)..=(x + half_x + 1) {
                for dy in (y - 1..=y + height).rev() {
                    for dz in (z - half_z - 1)..=(z + half_z + 1) {
                        if dx != x - half_x - 1 && dy != y - 1 && dz != z - half_z - 1 &&
                            dx != x + half_x + 1 && dy != y + height + 1 && dz != z + half_z + 1
                        {
                            (accessor.set_block_id)(dx, dy, dz, 0); // air
                        } else {
                            let bid = (accessor.get_block_id)(dx, dy, dz);
                            let solid = bid != 0 && bid != 8 && bid != 9;
                            if dy >= 0 && !solid {
                                (accessor.set_block_id)(dx, dy, dz, 0);
                            } else if solid {
                                if dy == y - 1 && rand.next_int_bound(4) != 0 {
                                    (accessor.set_block_id)(dx, dy, dz, 48); // mossy cobblestone
                                } else {
                                    (accessor.set_block_id)(dx, dy, dz, 4); // cobblestone
                                }
                            }
                        }
                    }
                }
            }
            // Place spawner
            (accessor.set_block_id)(x, y, z, 52); // mob spawner
            true
        } else {
            false
        }
    }
}
