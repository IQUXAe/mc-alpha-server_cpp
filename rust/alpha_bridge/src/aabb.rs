//! Port of C++ `core/AxisAlignedBB.h` (plus `MovingObjectPosition`).
//!
//! Mapping notes:
//! - Field names use `snake_case` (`min_x` for C++ `minX`, etc.).
//! - `clip` mirrors the C++ face order exactly: X-min, X-max, Y-min,
//!   Y-max, Z-min, Z-max with side map `[4, 5, 0, 1, 2, 3]`, the same
//!   per-face slab checks (`is_vec_in_*`), and the same
//!   closest-to-`from` selection via squared distance.
//! - `calculate_*_offset` keep the C++ comparison shape (`>` / `<`,
//!   `<=` / `>=`) so touching versus overlapping behaves identically.
//! - `add_coord` / `shrink` keep the C++ sign branches: only strictly
//!   negative values touch the min side, only strictly positive values
//!   touch the max side.

use crate::vec3d::Vec3D;

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct MovingObjectPosition {
    pub block_x: i32,
    pub block_y: i32,
    pub block_z: i32,
    pub side_hit: i8,
    pub hit_vec: Vec3D,
}

impl MovingObjectPosition {
    pub fn new(x: i32, y: i32, z: i32, side: i8, hit: Vec3D) -> Self {
        Self {
            block_x: x,
            block_y: y,
            block_z: z,
            side_hit: side,
            hit_vec: hit,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct AxisAlignedBB {
    pub min_x: f64,
    pub min_y: f64,
    pub min_z: f64,
    pub max_x: f64,
    pub max_y: f64,
    pub max_z: f64,
}

impl Default for AxisAlignedBB {
    fn default() -> Self {
        Self {
            min_x: 0.0,
            min_y: 0.0,
            min_z: 0.0,
            max_x: 0.0,
            max_y: 0.0,
            max_z: 0.0,
        }
    }
}

impl AxisAlignedBB {
    pub fn new(x0: f64, y0: f64, z0: f64, x1: f64, y1: f64, z1: f64) -> Self {
        Self {
            min_x: x0,
            min_y: y0,
            min_z: z0,
            max_x: x1,
            max_y: y1,
            max_z: z1,
        }
    }

    pub fn get_bounding_box(x0: f64, y0: f64, z0: f64, x1: f64, y1: f64, z1: f64) -> Self {
        Self::new(x0, y0, z0, x1, y1, z1)
    }

    pub fn set_bounds(
        &mut self,
        x0: f64,
        y0: f64,
        z0: f64,
        x1: f64,
        y1: f64,
        z1: f64,
    ) -> &mut Self {
        self.min_x = x0;
        self.min_y = y0;
        self.min_z = z0;
        self.max_x = x1;
        self.max_y = y1;
        self.max_z = z1;
        self
    }

    pub fn add_coord(&self, x: f64, y: f64, z: f64) -> Self {
        let mut nx0 = self.min_x;
        let mut ny0 = self.min_y;
        let mut nz0 = self.min_z;
        let mut nx1 = self.max_x;
        let mut ny1 = self.max_y;
        let mut nz1 = self.max_z;
        if x < 0.0 {
            nx0 += x;
        }
        if x > 0.0 {
            nx1 += x;
        }
        if y < 0.0 {
            ny0 += y;
        }
        if y > 0.0 {
            ny1 += y;
        }
        if z < 0.0 {
            nz0 += z;
        }
        if z > 0.0 {
            nz1 += z;
        }
        Self::new(nx0, ny0, nz0, nx1, ny1, nz1)
    }

    pub fn expand(&self, x: f64, y: f64, z: f64) -> Self {
        Self::new(
            self.min_x - x,
            self.min_y - y,
            self.min_z - z,
            self.max_x + x,
            self.max_y + y,
            self.max_z + z,
        )
    }

    pub fn get_offset_bounding_box(&self, x: f64, y: f64, z: f64) -> Self {
        Self::new(
            self.min_x + x,
            self.min_y + y,
            self.min_z + z,
            self.max_x + x,
            self.max_y + y,
            self.max_z + z,
        )
    }

    pub fn calculate_x_offset(&self, other: &AxisAlignedBB, mut dx: f64) -> f64 {
        if other.max_y > self.min_y
            && other.min_y < self.max_y
            && other.max_z > self.min_z
            && other.min_z < self.max_z
        {
            if dx > 0.0 && other.max_x <= self.min_x {
                let d = self.min_x - other.max_x;
                if d < dx {
                    dx = d;
                }
            }
            if dx < 0.0 && other.min_x >= self.max_x {
                let d = self.max_x - other.min_x;
                if d > dx {
                    dx = d;
                }
            }
        }
        dx
    }

    pub fn calculate_y_offset(&self, other: &AxisAlignedBB, mut dy: f64) -> f64 {
        if other.max_x > self.min_x
            && other.min_x < self.max_x
            && other.max_z > self.min_z
            && other.min_z < self.max_z
        {
            if dy > 0.0 && other.max_y <= self.min_y {
                let d = self.min_y - other.max_y;
                if d < dy {
                    dy = d;
                }
            }
            if dy < 0.0 && other.min_y >= self.max_y {
                let d = self.max_y - other.min_y;
                if d > dy {
                    dy = d;
                }
            }
        }
        dy
    }

    pub fn calculate_z_offset(&self, other: &AxisAlignedBB, mut dz: f64) -> f64 {
        if other.max_x > self.min_x
            && other.min_x < self.max_x
            && other.max_y > self.min_y
            && other.min_y < self.max_y
        {
            if dz > 0.0 && other.max_z <= self.min_z {
                let d = self.min_z - other.max_z;
                if d < dz {
                    dz = d;
                }
            }
            if dz < 0.0 && other.min_z >= self.max_z {
                let d = self.max_z - other.min_z;
                if d > dz {
                    dz = d;
                }
            }
        }
        dz
    }

    pub fn intersects_with(&self, other: &AxisAlignedBB) -> bool {
        other.max_x > self.min_x
            && other.min_x < self.max_x
            && other.max_y > self.min_y
            && other.min_y < self.max_y
            && other.max_z > self.min_z
            && other.min_z < self.max_z
    }

    pub fn offset(&mut self, x: f64, y: f64, z: f64) -> &mut Self {
        self.min_x += x;
        self.min_y += y;
        self.min_z += z;
        self.max_x += x;
        self.max_y += y;
        self.max_z += z;
        self
    }

    pub fn shrink(&self, x: f64, y: f64, z: f64) -> Self {
        let mut nx0 = self.min_x;
        let mut ny0 = self.min_y;
        let mut nz0 = self.min_z;
        let mut nx1 = self.max_x;
        let mut ny1 = self.max_y;
        let mut nz1 = self.max_z;
        if x < 0.0 {
            nx0 -= x;
        }
        if x > 0.0 {
            nx1 -= x;
        }
        if y < 0.0 {
            ny0 -= y;
        }
        if y > 0.0 {
            ny1 -= y;
        }
        if z < 0.0 {
            nz0 -= z;
        }
        if z > 0.0 {
            nz1 -= z;
        }
        Self::new(nx0, ny0, nz0, nx1, ny1, nz1)
    }

    pub fn copy(&self) -> Self {
        Self::new(
            self.min_x,
            self.min_y,
            self.min_z,
            self.max_x,
            self.max_y,
            self.max_z,
        )
    }

    pub fn set_bb(&mut self, other: &AxisAlignedBB) {
        self.min_x = other.min_x;
        self.min_y = other.min_y;
        self.min_z = other.min_z;
        self.max_x = other.max_x;
        self.max_y = other.max_y;
        self.max_z = other.max_z;
    }

    pub fn clip(&self, from: &Vec3D, to: &Vec3D) -> Option<MovingObjectPosition> {
        let mut hits: [Option<Vec3D>; 6] = [
            from.get_intermediate_with_x_value(to, self.min_x),
            from.get_intermediate_with_x_value(to, self.max_x),
            from.get_intermediate_with_y_value(to, self.min_y),
            from.get_intermediate_with_y_value(to, self.max_y),
            from.get_intermediate_with_z_value(to, self.min_z),
            from.get_intermediate_with_z_value(to, self.max_z),
        ];

        if let Some(v) = hits[0] {
            if !self.is_vec_in_yz(&v) {
                hits[0] = None;
            }
        }
        if let Some(v) = hits[1] {
            if !self.is_vec_in_yz(&v) {
                hits[1] = None;
            }
        }
        if let Some(v) = hits[2] {
            if !self.is_vec_in_xz(&v) {
                hits[2] = None;
            }
        }
        if let Some(v) = hits[3] {
            if !self.is_vec_in_xz(&v) {
                hits[3] = None;
            }
        }
        if let Some(v) = hits[4] {
            if !self.is_vec_in_xy(&v) {
                hits[4] = None;
            }
        }
        if let Some(v) = hits[5] {
            if !self.is_vec_in_xy(&v) {
                hits[5] = None;
            }
        }

        const FACE_MAP: [i8; 6] = [4, 5, 0, 1, 2, 3];

        let mut closest_idx: i32 = -1;
        let mut closest_dist: f64 = 0.0;
        let mut i: usize = 0;
        while i < 6 {
            if let Some(h) = hits[i] {
                let d = from.square_distance_to(&h);
                if closest_idx < 0 || d < closest_dist {
                    closest_idx = i as i32;
                    closest_dist = d;
                }
            }
            i += 1;
        }

        if closest_idx < 0 {
            return None;
        }
        let mut face: i8 = 4;
        let mut hit: Option<Vec3D> = None;
        let mut i: usize = 0;
        while i < 6 {
            if closest_idx == i as i32 {
                face = FACE_MAP[i];
                hit = hits[i];
            }
            i += 1;
        }
        if let Some(h) = hit {
            Some(MovingObjectPosition::new(0, 0, 0, face, h))
        } else {
            None
        }
    }

    fn is_vec_in_yz(&self, v: &Vec3D) -> bool {
        v.y_coord >= self.min_y
            && v.y_coord <= self.max_y
            && v.z_coord >= self.min_z
            && v.z_coord <= self.max_z
    }

    fn is_vec_in_xz(&self, v: &Vec3D) -> bool {
        v.x_coord >= self.min_x
            && v.x_coord <= self.max_x
            && v.z_coord >= self.min_z
            && v.z_coord <= self.max_z
    }

    fn is_vec_in_xy(&self, v: &Vec3D) -> bool {
        v.x_coord >= self.min_x
            && v.x_coord <= self.max_x
            && v.y_coord >= self.min_y
            && v.y_coord <= self.max_y
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn near(a: f64, b: f64, eps: f64) -> bool {
        (a - b).abs() <= eps
    }

    #[test]
    fn default_constructor() {
        let bb = AxisAlignedBB::default();
        assert_eq!(bb.min_x, 0.0);
        assert_eq!(bb.max_x, 0.0);
    }

    #[test]
    fn parameter_constructor() {
        let bb = AxisAlignedBB::new(0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
        assert_eq!(bb.min_x, 0.0);
        assert_eq!(bb.max_x, 1.0);
        assert_eq!(bb.min_y, 0.0);
        assert_eq!(bb.max_y, 1.0);
    }

    #[test]
    fn intersects_with() {
        let a = AxisAlignedBB::new(0.0, 0.0, 0.0, 2.0, 2.0, 2.0);
        let b = AxisAlignedBB::new(1.0, 1.0, 1.0, 3.0, 3.0, 3.0);
        assert!(a.intersects_with(&b));
    }

    #[test]
    fn not_intersects() {
        let a = AxisAlignedBB::new(0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
        let b = AxisAlignedBB::new(2.0, 2.0, 2.0, 3.0, 3.0, 3.0);
        assert!(!a.intersects_with(&b));
    }

    #[test]
    fn touching_edges_not_intersect() {
        let a = AxisAlignedBB::new(0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
        let b = AxisAlignedBB::new(1.0, 0.0, 0.0, 2.0, 1.0, 1.0);
        assert!(!a.intersects_with(&b));
    }

    #[test]
    fn expand() {
        let bb = AxisAlignedBB::new(0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
        let e = bb.expand(1.0, 1.0, 1.0);
        assert_eq!(e.min_x, -1.0);
        assert_eq!(e.max_x, 2.0);
        assert_eq!(e.min_y, -1.0);
        assert_eq!(e.max_y, 2.0);
    }

    #[test]
    fn get_offset_bounding_box() {
        let bb = AxisAlignedBB::new(0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
        let o = bb.get_offset_bounding_box(5.0, 5.0, 5.0);
        assert_eq!(o.min_x, 5.0);
        assert_eq!(o.max_x, 6.0);
    }

    #[test]
    fn calculate_x_offset_block_right_of_entity() {
        let block = AxisAlignedBB::new(2.0, 0.0, 0.0, 3.0, 1.0, 1.0);
        let entity = AxisAlignedBB::new(0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
        let dx = block.calculate_x_offset(&entity, 3.0);
        assert!(near(dx, 1.0, 1e-6));
    }

    #[test]
    fn calculate_x_offset_block_left_of_entity() {
        let block = AxisAlignedBB::new(-2.0, 0.0, 0.0, -1.0, 1.0, 1.0);
        let entity = AxisAlignedBB::new(0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
        let dx = block.calculate_x_offset(&entity, -3.0);
        assert!(near(dx, -1.0, 1e-6));
    }

    #[test]
    fn calculate_x_offset_no_collision() {
        let block = AxisAlignedBB::new(5.0, 0.0, 0.0, 6.0, 1.0, 1.0);
        let entity = AxisAlignedBB::new(0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
        let dx = block.calculate_x_offset(&entity, 3.0);
        assert!(near(dx, 3.0, 1e-6));
    }

    #[test]
    fn calculate_y_offset_block_above_entity() {
        let block = AxisAlignedBB::new(0.0, 2.0, 0.0, 1.0, 3.0, 1.0);
        let entity = AxisAlignedBB::new(0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
        let dy = block.calculate_y_offset(&entity, 3.0);
        assert!(near(dy, 1.0, 1e-6));
    }

    #[test]
    fn calculate_y_offset_block_below_entity() {
        let block = AxisAlignedBB::new(0.0, -2.0, 0.0, 1.0, -1.0, 1.0);
        let entity = AxisAlignedBB::new(0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
        let dy = block.calculate_y_offset(&entity, -3.0);
        assert!(near(dy, -1.0, 1e-6));
    }

    #[test]
    fn calculate_z_offset_block_south_of_entity() {
        let block = AxisAlignedBB::new(0.0, 0.0, 2.0, 1.0, 1.0, 3.0);
        let entity = AxisAlignedBB::new(0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
        let dz = block.calculate_z_offset(&entity, 3.0);
        assert!(near(dz, 1.0, 1e-6));
    }

    #[test]
    fn no_collision_x_offset() {
        let a = AxisAlignedBB::new(0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
        let b = AxisAlignedBB::new(5.0, 5.0, 5.0, 6.0, 6.0, 6.0);
        let dx = a.calculate_x_offset(&b, 1.0);
        assert_eq!(dx, 1.0);
    }

    #[test]
    fn add_coord() {
        let bb = AxisAlignedBB::new(0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
        let a = bb.add_coord(-1.0, 2.0, 0.0);
        assert_eq!(a.min_x, -1.0);
        assert_eq!(a.max_x, 1.0);
        assert_eq!(a.min_y, 0.0);
        assert_eq!(a.max_y, 3.0);
    }

    #[test]
    fn shrink_positive_reduces_max() {
        let bb = AxisAlignedBB::new(0.0, 0.0, 0.0, 2.0, 2.0, 2.0);
        let s = bb.shrink(0.5, 0.5, 0.5);
        assert_eq!(s.min_x, 0.0);
        assert_eq!(s.max_x, 1.5);
    }

    #[test]
    fn shrink_negative_increases_min() {
        let bb = AxisAlignedBB::new(0.0, 0.0, 0.0, 2.0, 2.0, 2.0);
        let s = bb.shrink(-0.5, -0.5, -0.5);
        assert_eq!(s.min_x, 0.5);
        assert_eq!(s.max_x, 2.0);
    }

    #[test]
    fn copy() {
        let bb = AxisAlignedBB::new(1.0, 2.0, 3.0, 4.0, 5.0, 6.0);
        let c = bb.copy();
        assert_eq!(c.min_x, 1.0);
        assert_eq!(c.max_z, 6.0);
    }

    #[test]
    fn set_bb() {
        let mut bb = AxisAlignedBB::default();
        let other = AxisAlignedBB::new(1.0, 2.0, 3.0, 4.0, 5.0, 6.0);
        bb.set_bb(&other);
        assert_eq!(bb.min_x, 1.0);
        assert_eq!(bb.max_z, 6.0);
    }

    #[test]
    fn offset() {
        let mut bb = AxisAlignedBB::new(0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
        bb.offset(10.0, 20.0, 30.0);
        assert_eq!(bb.min_x, 10.0);
        assert_eq!(bb.min_y, 20.0);
        assert_eq!(bb.min_z, 30.0);
        assert_eq!(bb.max_x, 11.0);
        assert_eq!(bb.max_y, 21.0);
        assert_eq!(bb.max_z, 31.0);
    }

    #[test]
    fn clip_ray_hit() {
        let bb = AxisAlignedBB::new(0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
        let from = Vec3D::new(-1.0, 0.5, 0.5);
        let to = Vec3D::new(2.0, 0.5, 0.5);
        let r = bb.clip(&from, &to);
        assert!(r.is_some());
        let r = r.unwrap();
        assert_eq!(r.side_hit, 4);
        assert!(near(r.hit_vec.x_coord, 0.0, 1e-6));
    }

    #[test]
    fn clip_ray_miss() {
        let bb = AxisAlignedBB::new(0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
        let from = Vec3D::new(-1.0, 5.0, 5.0);
        let to = Vec3D::new(2.0, 5.0, 5.0);
        let r = bb.clip(&from, &to);
        assert!(r.is_none());
    }

    #[test]
    fn get_bounding_box() {
        let bb = AxisAlignedBB::get_bounding_box(1.0, 2.0, 3.0, 4.0, 5.0, 6.0);
        assert_eq!(bb.min_x, 1.0);
        assert_eq!(bb.max_y, 5.0);
    }

    #[test]
    fn set_bounds() {
        let mut bb = AxisAlignedBB::default();
        bb.set_bounds(1.0, 2.0, 3.0, 4.0, 5.0, 6.0);
        assert_eq!(bb.min_x, 1.0);
        assert_eq!(bb.max_x, 4.0);
    }

    // Extra edge coverage.

    #[test]
    fn clip_ray_hits_top_face() {
        // Ray falling straight down onto the box: enters at maxY -> side 1.
        let bb = AxisAlignedBB::new(0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
        let from = Vec3D::new(0.5, 2.0, 0.5);
        let to = Vec3D::new(0.5, -1.0, 0.5);
        let r = bb.clip(&from, &to);
        assert!(r.is_some());
        let r = r.unwrap();
        assert_eq!(r.side_hit, 1);
        assert!(near(r.hit_vec.y_coord, 1.0, 1e-6));
    }

    #[test]
    fn clip_ray_parallel_misses() {
        // Ray parallel to X (dx == 0) and offset outside the box.
        let bb = AxisAlignedBB::new(0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
        let from = Vec3D::new(2.0, 0.5, 0.5);
        let to = Vec3D::new(2.0, 0.5, 2.0);
        assert!(bb.clip(&from, &to).is_none());
    }

    #[test]
    fn add_coord_zero_leaves_box_unchanged() {
        let bb = AxisAlignedBB::new(0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
        let a = bb.add_coord(0.0, 0.0, 0.0);
        assert_eq!(a, bb);
    }

    #[test]
    fn offset_roundtrip() {
        let mut bb = AxisAlignedBB::new(1.0, 2.0, 3.0, 4.0, 5.0, 6.0);
        bb.offset(1.0, -2.0, 3.0);
        bb.offset(-1.0, 2.0, -3.0);
        assert_eq!(bb, AxisAlignedBB::new(1.0, 2.0, 3.0, 4.0, 5.0, 6.0));
    }

    #[test]
    fn y_offset_without_xz_overlap_is_untouched() {
        // Block far away in X: no XZ overlap, dy passes through.
        let block = AxisAlignedBB::new(10.0, 2.0, 10.0, 11.0, 3.0, 11.0);
        let entity = AxisAlignedBB::new(0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
        assert_eq!(block.calculate_y_offset(&entity, 3.0), 3.0);
    }
}
