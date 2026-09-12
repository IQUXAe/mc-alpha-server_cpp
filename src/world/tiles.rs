//! Block-entity rows and entity string-id helpers, split out of `world.rs`.
//! Re-exported from `world` so `crate::world::TileData` keeps working.

use crate::entity_table::{AnimalKind, LivingBody, MobKind};

/// Block-entity data by cell (mirrors the C++ per-chunk `TileEntity`
/// objects, stored flat until the tile tick slice needs behavior).
#[derive(Clone, Copy, Debug)]
pub enum TileData {
    Furnace(crate::tile_entity_furnace::FfiFurnaceState),
    Chest(crate::tile_entity_chest::FfiChestState),
    Sign(crate::tile_entity_sign::FfiSignState),
}

/// String id for spill/restore (mirrors `getEntityStringId`).
pub(crate) fn mob_string_id(kind: MobKind) -> String {
    match kind {
        MobKind::Spider => "Spider",
        MobKind::Zombie => "Zombie",
        MobKind::Skeleton => "Skeleton",
        MobKind::Creeper => "Creeper",
    }
    .to_string()
}

/// String id for spill/restore (mirrors `getEntityStringId`).
pub(crate) fn animal_string_id(kind: AnimalKind) -> String {
    match kind {
        AnimalKind::Sheep => "Sheep",
        AnimalKind::Pig => "Pig",
        AnimalKind::Chicken => "Chicken",
        AnimalKind::Cow => "Cow",
    }
    .to_string()
}

pub(crate) fn mob_kind_of(id: &str) -> Option<MobKind> {
    match id {
        "Spider" => Some(MobKind::Spider),
        "Zombie" => Some(MobKind::Zombie),
        "Skeleton" => Some(MobKind::Skeleton),
        "Creeper" => Some(MobKind::Creeper),
        _ => None,
    }
}

pub(crate) fn animal_kind_of(id: &str) -> Option<AnimalKind> {
    match id {
        "Sheep" => Some(AnimalKind::Sheep),
        "Pig" => Some(AnimalKind::Pig),
        "Chicken" => Some(AnimalKind::Chicken),
        "Cow" => Some(AnimalKind::Cow),
        _ => None,
    }
}

pub(crate) fn pending_creature(
    string_id: String,
    living: &LivingBody,
    saddled: bool,
    sheared: bool,
    egg_timer: i32,
) -> crate::chunk::PendingCreature {
    crate::chunk::PendingCreature {
        string_id,
        pos: living.body.pos,
        motion: living.body.motion,
        yaw: living.body.yaw,
        pitch: living.body.pitch,
        health: living.health,
        max_health: living.max_health,
        saddled,
        sheared,
        egg_timer,
    }
}
