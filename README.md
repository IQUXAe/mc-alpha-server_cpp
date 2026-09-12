# alpha_server

A Rust server implementation for Minecraft Alpha 1.2.6, written from scratch. Protocol-compatible with vanilla Alpha 1.2.6 clients.

---

## Building & running

**Dependencies:** a recent stable Rust toolchain.

```bash
cargo build --release
./target/release/alpha_server [server.properties] [world-dir] [port]
```

All three arguments are optional: properties default to
`server.properties`, the world to `world/<level-name>`, the port to
`server-port`. Run from the directory that holds `server.properties`
(the world, player files, and `ops.txt` / ban lists live next to it).

---

## Architecture

All logic lives in the `alpha_server` library (`src/`):

| Module | Description |
| :--- | :--- |
| `server.rs` + `main.rs` | 20 TPS loop, sessions, chunk streaming, console, saves |
| `session.rs` | TCP transport, login state machine, play packet handlers |
| `world.rs` + `chunk.rs` | Chunk storage, block ticks, skylight, scheduled updates |
| `generator.rs` + `noise`/`biome`/`density`/`caves`/`decorators` | **1:1 terrain & biome generation** |
| `entity_*` | Players, mobs, animals, arrows, boats, physics, combat |
| `tracker.rs` | Entity tracking fan-out to nearby players |
| `persist.rs` | LevelDB chunk store, `level.dat`, player files (gzip-NBT) |
| `network.rs` | Packet encode/decode for the Alpha protocol |
| `server_config`/`server_admin`/`commands` | Properties, ops/ban lists, console commands |

World data (`world/`), `server.properties`, and the ops/ban lists are
runtime files and stay out of git.

---

## Comparison with the Vanilla Server

| Feature / Aspect | Vanilla Alpha 1.2.6 (Java) | `alpha_server` (Rust) |
| :--- | :--- | :--- |
| Memory footprint | High (JVM overhead) | Minimal (native, no GC pauses) |
| Terrain & biomes | Original algorithm | **Identical to original (1:1 generation)** |
| Mobs, animals, combat | Fully implemented | Implemented (AI, spawns, attacks, drops) |
| Tile entities | Chest, furnace, sign | Chest, furnace (with ticking), sign |
| Multiplayer | Full | Chat, tracking, chunk streaming, inventory sync |
| Persistence | LevelDB world, NBT players | Same formats (proven against a live C++ DB) |
| Lighting | Full propagation | Single-chunk skylight pass on set (border seam known) |
| Dimensions | Overworld & Nether | Overworld only |
| Authentication | Online mode (session check) | Same (legacy endpoint; prefer offline for LAN) |

---

## Roadmap

- Nether dimension generation and transitions
- Redstone wire signal propagation
- Cross-chunk skylight spread (remove the border seam)

---

## License

GPLv3 or later, see [LICENSE](LICENSE). Copyright (C) 2026 IQUXAe.

> Not an official Minecraft product. Not approved by or associated with Mojang or Microsoft.
