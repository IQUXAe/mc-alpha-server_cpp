# Rust server sources

`alpha_server/` is the whole server: a library with the game logic plus
the `alpha_server` binary (`src/main.rs`).

## Build & test

```bash
cd rust/alpha_server
cargo build --release   # binary at target/release/alpha_server
cargo test              # full suite (world, entities, sessions, server)
```

## Layout highlights

- `server.rs`, `main.rs` — 20 TPS loop, sessions, chunk streaming,
  console commands, saves, shutdown.
- `session.rs` — TCP transport (`Conn`), login state machine, play
  packet handlers with loopback tests.
- `world.rs`, `chunk.rs`, `generator.rs` (+ `noise`, `biome`,
  `density`, `caves`, `decorators`) — simulation and 1:1 terrain gen.
- `persist.rs` — LevelDB chunk store, `level.dat`, player files.
- `network.rs` — Alpha protocol packet encode/decode.

Docs on the storage format live in `../docs/world_storage.md`.
