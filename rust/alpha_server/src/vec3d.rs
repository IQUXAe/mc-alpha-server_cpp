//! Port of C++ `core/Vec3D.h`.
//!
//! Mapping notes:
//! - Field names use `snake_case` (`x_coord` for C++ `xCoord`, etc.).
//! - `squareDistanceTo` is overloaded in C++; here it is split into
//!   [`Vec3D::square_distance_to`] (vector form) and
//!   [`Vec3D::square_distance_to_coords`] (coordinate form).
//! - `normalize`, `distance_to`, `length_vector` route through
//!   `math_helper::sqrt_double` (which returns `f32`, widened to `f64`),
//!   matching the C++ data flow exactly.
//! - Intermediate-point helpers return `Option<Vec3D>` for C++
//!   `std::optional<Vec3D>`, with the same `dx*dx < 1e-7` guard and the
//!   same closed `[0, 1]` range check on `t`.

use crate::math_helper;

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Vec3D {
    pub x_coord: f64,
    pub y_coord: f64,
    pub z_coord: f64,
}

impl Default for Vec3D {
    fn default() -> Self {
        Self {
            x_coord: 0.0,
            y_coord: 0.0,
            z_coord: 0.0,
        }
    }
}

impl Vec3D {
    pub fn new(x: f64, y: f64, z: f64) -> Self {
        Self {
            x_coord: x,
            y_coord: y,
            z_coord: z,
        }
    }

    pub fn create_vector_helper(x: f64, y: f64, z: f64) -> Self {
        Self::new(x, y, z)
    }

    pub fn set_components(&mut self, x: f64, y: f64, z: f64) -> &mut Self {
        self.x_coord = x;
        self.y_coord = y;
        self.z_coord = z;
        self
    }

    pub fn normalize(&self) -> Self {
        let len =
            math_helper::sqrt_double(self.x_coord * self.x_coord + self.y_coord * self.y_coord + self.z_coord * self.z_coord)
                as f64;
        if len < 1.0E-4 {
            return Self::new(0.0, 0.0, 0.0);
        }
        Self::new(self.x_coord / len, self.y_coord / len, self.z_coord / len)
    }

    pub fn add_vector(&self, x: f64, y: f64, z: f64) -> Self {
        Self::new(self.x_coord + x, self.y_coord + y, self.z_coord + z)
    }

    pub fn distance_to(&self, other: &Vec3D) -> f64 {
        let dx = other.x_coord - self.x_coord;
        let dy = other.y_coord - self.y_coord;
        let dz = other.z_coord - self.z_coord;
        math_helper::sqrt_double(dx * dx + dy * dy + dz * dz) as f64
    }

    pub fn square_distance_to(&self, other: &Vec3D) -> f64 {
        let dx = other.x_coord - self.x_coord;
        let dy = other.y_coord - self.y_coord;
        let dz = other.z_coord - self.z_coord;
        dx * dx + dy * dy + dz * dz
    }

    pub fn square_distance_to_coords(&self, x: f64, y: f64, z: f64) -> f64 {
        let dx = x - self.x_coord;
        let dy = y - self.y_coord;
        let dz = z - self.z_coord;
        dx * dx + dy * dy + dz * dz
    }

    pub fn length_vector(&self) -> f64 {
        math_helper::sqrt_double(self.x_coord * self.x_coord + self.y_coord * self.y_coord + self.z_coord * self.z_coord)
            as f64
    }

    pub fn get_intermediate_with_x_value(&self, other: &Vec3D, x: f64) -> Option<Vec3D> {
        let dx = other.x_coord - self.x_coord;
        let dy = other.y_coord - self.y_coord;
        let dz = other.z_coord - self.z_coord;
        if dx * dx < 1.0E-7 {
            return None;
        }
        let t = (x - self.x_coord) / dx;
        if t >= 0.0 && t <= 1.0 {
            Some(Vec3D::new(
                self.x_coord + dx * t,
                self.y_coord + dy * t,
                self.z_coord + dz * t,
            ))
        } else {
            None
        }
    }

    pub fn get_intermediate_with_y_value(&self, other: &Vec3D, y: f64) -> Option<Vec3D> {
        let dx = other.x_coord - self.x_coord;
        let dy = other.y_coord - self.y_coord;
        let dz = other.z_coord - self.z_coord;
        if dy * dy < 1.0E-7 {
            return None;
        }
        let t = (y - self.y_coord) / dy;
        if t >= 0.0 && t <= 1.0 {
            Some(Vec3D::new(
                self.x_coord + dx * t,
                self.y_coord + dy * t,
                self.z_coord + dz * t,
            ))
        } else {
            None
        }
    }

    pub fn get_intermediate_with_z_value(&self, other: &Vec3D, z: f64) -> Option<Vec3D> {
        let dx = other.x_coord - self.x_coord;
        let dy = other.y_coord - self.y_coord;
        let dz = other.z_coord - self.z_coord;
        if dz * dz < 1.0E-7 {
            return None;
        }
        let t = (z - self.z_coord) / dz;
        if t >= 0.0 && t <= 1.0 {
            Some(Vec3D::new(
                self.x_coord + dx * t,
                self.y_coord + dy * t,
                self.z_coord + dz * t,
            ))
        } else {
            None
        }
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
        let v = Vec3D::default();
        assert_eq!(v.x_coord, 0.0);
        assert_eq!(v.y_coord, 0.0);
        assert_eq!(v.z_coord, 0.0);
    }

    #[test]
    fn parameter_constructor() {
        let v = Vec3D::new(1.0, 2.0, 3.0);
        assert_eq!(v.x_coord, 1.0);
        assert_eq!(v.y_coord, 2.0);
        assert_eq!(v.z_coord, 3.0);
    }

    #[test]
    fn negative_zero_normalization() {
        let v = Vec3D::new(-0.0, -0.0, -0.0);
        assert_eq!(v.x_coord, 0.0);
        assert_eq!(v.y_coord, 0.0);
        assert_eq!(v.z_coord, 0.0);
    }

    #[test]
    fn add_vector() {
        let v = Vec3D::new(1.0, 2.0, 3.0);
        let r = v.add_vector(10.0, 20.0, 30.0);
        assert_eq!(r.x_coord, 11.0);
        assert_eq!(r.y_coord, 22.0);
        assert_eq!(r.z_coord, 33.0);
    }

    #[test]
    fn distance_to() {
        let a = Vec3D::new(0.0, 0.0, 0.0);
        let b = Vec3D::new(3.0, 4.0, 0.0);
        assert_eq!(a.distance_to(&b), 5.0);
    }

    #[test]
    fn square_distance_to() {
        let a = Vec3D::new(0.0, 0.0, 0.0);
        let b = Vec3D::new(3.0, 4.0, 0.0);
        assert_eq!(a.square_distance_to(&b), 25.0);
    }

    #[test]
    fn normalize_zero() {
        let v = Vec3D::default();
        let n = v.normalize();
        assert_eq!(n.x_coord, 0.0);
        assert_eq!(n.y_coord, 0.0);
        assert_eq!(n.z_coord, 0.0);
    }

    #[test]
    fn normalize_unit() {
        let v = Vec3D::new(3.0, 0.0, 0.0);
        let n = v.normalize();
        assert!(near(n.x_coord, 1.0, 1e-6));
        assert!(near(n.length_vector(), 1.0, 1e-6));
    }

    #[test]
    fn length_vector() {
        let v = Vec3D::new(3.0, 4.0, 0.0);
        assert_eq!(v.length_vector(), 5.0);
    }

    #[test]
    fn get_intermediate_with_x_value() {
        let from = Vec3D::new(0.0, 0.0, 0.0);
        let to = Vec3D::new(10.0, 10.0, 10.0);
        let r = from.get_intermediate_with_x_value(&to, 5.0);
        assert!(r.is_some());
        let r = r.unwrap();
        assert!(near(r.x_coord, 5.0, 1e-6));
        assert!(near(r.y_coord, 5.0, 1e-6));
        assert!(near(r.z_coord, 5.0, 1e-6));
    }

    #[test]
    fn get_intermediate_out_of_range() {
        let from = Vec3D::new(0.0, 0.0, 0.0);
        let to = Vec3D::new(10.0, 10.0, 10.0);
        assert!(from.get_intermediate_with_x_value(&to, -1.0).is_none());
        assert!(from.get_intermediate_with_x_value(&to, 11.0).is_none());
    }

    #[test]
    fn get_intermediate_with_y_value() {
        let from = Vec3D::new(0.0, 0.0, 0.0);
        let to = Vec3D::new(10.0, 10.0, 10.0);
        let r = from.get_intermediate_with_y_value(&to, 5.0);
        assert!(r.is_some());
        assert!(near(r.unwrap().y_coord, 5.0, 1e-6));
    }

    #[test]
    fn get_intermediate_with_z_value() {
        let from = Vec3D::new(0.0, 0.0, 0.0);
        let to = Vec3D::new(10.0, 10.0, 10.0);
        let r = from.get_intermediate_with_z_value(&to, 5.0);
        assert!(r.is_some());
        assert!(near(r.unwrap().z_coord, 5.0, 1e-6));
    }

    #[test]
    fn set_components() {
        let mut v = Vec3D::default();
        v.set_components(7.0, 8.0, 9.0);
        assert_eq!(v.x_coord, 7.0);
        assert_eq!(v.y_coord, 8.0);
        assert_eq!(v.z_coord, 9.0);
    }

    #[test]
    fn create_vector_helper() {
        let v = Vec3D::create_vector_helper(1.0, 2.0, 3.0);
        assert_eq!(v.x_coord, 1.0);
        assert_eq!(v.y_coord, 2.0);
        assert_eq!(v.z_coord, 3.0);
    }

    #[test]
    fn square_distance_to_coords() {
        let v = Vec3D::new(1.0, 2.0, 3.0);
        assert_eq!(v.square_distance_to_coords(4.0, 6.0, 3.0), 25.0);
    }

    // Extra edge coverage.

    #[test]
    fn normalize_tiny_vector_returns_zero() {
        // Length below 1e-4 takes the early-out branch.
        let v = Vec3D::new(1e-5, 0.0, 0.0);
        let n = v.normalize();
        assert_eq!(n, Vec3D::new(0.0, 0.0, 0.0));
    }

    #[test]
    fn intermediate_parallel_axis_returns_none() {
        // dx == 0 hits the dx*dx < 1e-7 guard.
        let from = Vec3D::new(1.0, 0.0, 0.0);
        let to = Vec3D::new(1.0, 10.0, 10.0);
        assert!(from.get_intermediate_with_x_value(&to, 1.0).is_none());
        // Same for the other axes.
        let from = Vec3D::new(0.0, 1.0, 0.0);
        let to = Vec3D::new(10.0, 1.0, 10.0);
        assert!(from.get_intermediate_with_y_value(&to, 1.0).is_none());
        let from = Vec3D::new(0.0, 0.0, 1.0);
        let to = Vec3D::new(10.0, 10.0, 1.0);
        assert!(from.get_intermediate_with_z_value(&to, 1.0).is_none());
    }

    #[test]
    fn intermediate_boundary_t_is_inclusive() {
        let from = Vec3D::new(0.0, 0.0, 0.0);
        let to = Vec3D::new(10.0, 10.0, 10.0);
        // t == 0 and t == 1 are accepted.
        assert!(from.get_intermediate_with_x_value(&to, 0.0).is_some());
        assert!(from.get_intermediate_with_x_value(&to, 10.0).is_some());
        assert!(from.get_intermediate_with_y_value(&to, 0.0).is_some());
        assert!(from.get_intermediate_with_z_value(&to, 10.0).is_some());
    }

    #[test]
    fn distance_is_symmetric() {
        let a = Vec3D::new(1.0, 2.0, 3.0);
        let b = Vec3D::new(4.0, 6.0, 8.0);
        assert_eq!(a.distance_to(&b), b.distance_to(&a));
        assert_eq!(a.square_distance_to(&b), b.square_distance_to(&a));
    }
}
