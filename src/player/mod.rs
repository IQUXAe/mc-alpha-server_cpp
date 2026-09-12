//! Player action kernels (pure logic, no world access).
//!
//! MAP OF THE PLAYER MODULE TREE:
//! - `mod` (this file) — module map only.
//! - `movement` — movement packet validation.
//! - `digging` — progressive-digging state machine.
//! - `mining` — harvest rules (tools vs blocks).
//! - `combat` — damage pipeline (armor, scaling).
//! - `inventory` — player inventory state.

pub mod combat;
pub mod digging;
pub mod inventory;
pub mod mining;
pub mod movement;
