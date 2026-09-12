//! Entity rows and pure entity kernels.
//!
//! MAP OF THE ENTITY MODULE TREE:
//! - `mod` (this file) — module map only.
//! - `table` — `EntityTable` storage: bodies, living rows, players, mobs,
//!   animals, items, arrows, boats, falling blocks.
//! - `physics` — body movement, collision boxes, push-out.
//! - `living` — damage pipeline, health, fall (pure kernels).
//! - `ai` — creature steering math (chase/wander speeds).
//! - `misc` — small-entity kernels (items, falling sand, boats, arrows).
//! - `player` — player-specific helpers (death drops).

pub mod ai;
pub mod living;
pub mod misc;
pub mod physics;
pub mod player;
pub mod table;
