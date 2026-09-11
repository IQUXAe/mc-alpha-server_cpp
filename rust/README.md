# Rust Migration Staging Area

This folder is the first safe integration point for rewriting selected subsystems in Rust.

## Why start here

The current C++ server is tightly coupled around the main 20 TPS server tick, but some
areas already have narrow data-oriented boundaries:

- gzip/zstd compression and decompression
- chunk persistence payloads
- NBT-oriented serialization helpers

Those are good first Rust targets because they can be tested independently and exposed
through a small C ABI without rewriting gameplay.

## What lives here now

`alpha_bridge/` is a `staticlib` crate. It started with compression
(`alpha_gzip_*`, `alpha_zstd_*`, `alpha_buffer_free`) and has since grown
into the main migration vehicle — see `alpha_bridge.h` (~1000 lines) for the
full FFI surface. Current Rust-owned areas include:

- compression: gzip/zstd/zlib + `level.dat` NBT encode/decode
- worldgen: `noise`, `biome`, `density`, `caves`, `decorators`, `generator`
- storage: `chunk_loader`, `player_storage`, `nbt`
- player logic: `player_inventory`, `player_combat`, `player_movement`,
  `player_mining`, `player_digging` (digging state machine)
- entities/network: `network` (packet encode + `RustNetworkManager`),
  `pathfinder`, `tracker_math` (tracking math), `commands`, `block`,
  `tile_entity_*`, `random`

## Build locally once Rust is installed

```bash
cd rust/alpha_bridge
cargo build --release
```

The resulting static library can later be linked from CMake.

## Suggested migration order

1. Replace selected compression call sites in `World.cpp` and `ServerConfigurationManager.cpp`.
2. Add Rust-side tests for round-tripping chunk payloads.
3. Move NBT and chunk blob encoding behind the same library.
