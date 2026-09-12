//! Block materials (mirrors Java `Material.java`).
//!
//! Flag layout (`isLiquid`, `isSolid`, `canBlockGrass`,
//! `blocksMovement`, plus the `canBurn` flag set by `setBurning()`).
//!
//! The Java subclasses are represented as `const` constructors instead of
//! inheritance: `Material::transparent`, `Material::liquid` and
//! `Material::logic`. Static instances are associated constants with the
//! exact vanilla flags, including the burnable-init pass that
//! marks wood, leaves, cloth and tnt as burning.

/// Block material flags. `Copy` so the shared statics stay usable anywhere.
///
/// Equality trap (caught live): AIR, PLANTS, FIRE, CIRCUITS, and SNOW all
/// carry `(false, false, false, false)`, so `==` aliases them — vanilla
/// compares singleton references and never does. Never test "is air" with
/// `== Material::AIR`; compare the material id byte (see `is_air_material`
/// in `world.rs`) or match the specific const you mean.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Material {
    is_liquid: bool,
    is_solid: bool,
    can_block_grass: bool,
    blocks_movement: bool,
    can_burn: bool,
}

impl Material {
    /// Mirrors the C++ constructor defaults
    /// `(isLiquid=false, isSolid=true, canBlockGrass=true,
    /// blocksMovement=true)`.
    pub const fn new(
        is_liquid: bool,
        is_solid: bool,
        can_block_grass: bool,
        blocks_movement: bool,
    ) -> Self {
        Self {
            is_liquid,
            is_solid,
            can_block_grass,
            blocks_movement,
            can_burn: false,
        }
    }

    /// Mirrors `MaterialTransparent` / default-constructed solid material.
    pub const fn transparent() -> Self {
        Self::new(false, false, false, false)
    }

    /// Mirrors `MaterialLiquid`.
    pub const fn liquid() -> Self {
        Self::new(true, false, true, false)
    }

    /// Mirrors `MaterialLogic`.
    pub const fn logic() -> Self {
        Self::new(false, false, false, false)
    }

    /// Const-compatible burning marker for the static table below.
    pub const fn burning(mut self) -> Self {
        self.can_burn = true;
        self
    }

    /// Mirrors `getIsLiquid()`.
    pub const fn is_liquid(self) -> bool {
        self.is_liquid
    }

    /// Mirrors `isSolid()`.
    pub const fn is_solid(self) -> bool {
        self.is_solid
    }

    /// Mirrors `getCanBlockGrass()`.
    pub const fn can_block_grass(self) -> bool {
        self.can_block_grass
    }

    /// Mirrors `blocksMovement()`.
    pub const fn blocks_movement(self) -> bool {
        self.blocks_movement
    }

    /// Mirrors `getBurning()`.
    pub const fn get_burning(self) -> bool {
        self.can_burn
    }

    /// Mirrors `setBurning()` (returns `&mut Self` like the C++ reference).
    pub fn set_burning(&mut self) -> &mut Self {
        self.can_burn = true;
        self
    }

    // Static instances with the exact flags from Material.cpp.
    pub const AIR: Self = Self::new(false, false, false, false);
    pub const GROUND: Self = Self::new(false, true, true, true);
    pub const WOOD: Self = Self::new(false, true, true, true).burning();
    pub const ROCK: Self = Self::new(false, true, true, true);
    pub const IRON: Self = Self::new(false, true, true, true);
    pub const WATER: Self = Self::new(true, false, true, false);
    pub const LAVA: Self = Self::new(true, false, true, false);
    pub const LEAVES: Self = Self::new(false, true, true, true).burning();
    pub const PLANTS: Self = Self::new(false, false, false, false);
    pub const SPONGE: Self = Self::new(false, true, true, true);
    pub const CLOTH: Self = Self::new(false, true, true, true).burning();
    pub const FIRE: Self = Self::new(false, false, false, false);
    pub const SAND: Self = Self::new(false, true, true, true);
    pub const CIRCUITS: Self = Self::new(false, false, false, false);
    pub const GLASS: Self = Self::new(false, true, true, true);
    pub const TNT: Self = Self::new(false, true, true, true).burning();
    pub const UNUSED: Self = Self::new(false, true, true, true);
    pub const ICE: Self = Self::new(false, true, true, true);
    pub const SNOW: Self = Self::new(false, false, false, false);
    pub const BUILT_SNOW: Self = Self::new(false, true, true, true);
    pub const CACTUS: Self = Self::new(false, true, true, true);
    pub const CLAY: Self = Self::new(false, true, true, true);
    pub const PUMPKIN: Self = Self::new(false, true, true, true);
    pub const PORTAL: Self = Self::new(false, true, true, true);
    pub const WEB: Self = Self::new(false, true, true, true);
}

impl Default for Material {
    /// Mirrors a default-constructed `Material` (solid ground-like).
    fn default() -> Self {
        Self::new(false, true, true, true)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // Mirror of test/TestMaterial.cpp: the raw statics as declared in
    // Material.cpp (before Block::initBlocks() swaps liquids in).

    #[test]
    fn air_properties() {
        assert!(!Material::AIR.is_liquid());
        assert!(!Material::AIR.is_solid());
        assert!(!Material::AIR.blocks_movement());
    }

    #[test]
    fn water_is_liquid() {
        assert!(Material::WATER.is_liquid());
        assert!(!Material::WATER.is_solid());
    }

    #[test]
    fn lava_is_liquid() {
        assert!(Material::LAVA.is_liquid());
        assert!(!Material::LAVA.is_solid());
    }

    #[test]
    fn rock_is_solid() {
        assert!(Material::ROCK.is_solid());
        assert!(Material::ROCK.blocks_movement());
        assert!(!Material::ROCK.is_liquid());
        assert!(!Material::ROCK.get_burning());
    }

    #[test]
    fn wood_is_burning() {
        // Static init sets burning for flammable materials.
        assert!(Material::WOOD.get_burning());
        assert!(Material::LEAVES.get_burning());
        assert!(Material::CLOTH.get_burning());
        assert!(Material::TNT.get_burning());
    }

    #[test]
    fn transparent_subclass() {
        let leaves = Material::transparent();
        assert!(!leaves.is_solid());
        assert!(!leaves.blocks_movement());
        assert!(!leaves.can_block_grass());
    }

    #[test]
    fn liquid_subclass() {
        let liquid = Material::liquid();
        assert!(liquid.is_liquid());
        assert!(!liquid.is_solid());
        assert!(!liquid.blocks_movement());
    }

    #[test]
    fn logic_subclass() {
        let logic = Material::logic();
        assert!(!logic.is_solid());
        assert!(!logic.blocks_movement());
        assert!(!logic.can_block_grass());
    }

    #[test]
    fn fire_default_no_burn() {
        assert!(!Material::FIRE.get_burning());
    }

    #[test]
    fn set_burning() {
        let mut m = Material::default();
        assert!(!m.get_burning());
        m.set_burning();
        assert!(m.get_burning());
    }

    #[test]
    fn ground_properties() {
        assert!(Material::GROUND.is_solid());
        assert!(Material::GROUND.blocks_movement());
        assert!(Material::GROUND.can_block_grass());
        assert!(!Material::GROUND.is_liquid());
    }

    #[test]
    fn sand_properties() {
        assert!(Material::SAND.is_solid());
        assert!(Material::SAND.blocks_movement());
    }

    #[test]
    fn web_properties() {
        assert!(Material::WEB.blocks_movement());
    }
}
