//! Block-entity (tile entity) state: chests, furnaces, signs.
//!
//! MAP OF THE TILE ENTITY MODULE TREE:
//! - `mod` (this file) — module map only.
//! - `chest` — chest rows and scatter.
//! - `furnace` — furnace rows, burn/cook ticking.
//! - `sign` — sign text rows.

pub mod chest;
pub mod furnace;
pub mod sign;
