# alpha_server

A C++ server implementation for Minecraft Alpha 1.2.6, written from scratch. Protocol-compatible with vanilla Alpha 1.2.6 clients.

---

## Building

**Dependencies:** CMake 3.16+, GCC/Clang with C++23, zlib, LevelDB

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
./alpha_server
```

---

## Architecture

| Module | Description |
| :--- | :--- |
| `network/` | TCP socket I/O, packet serialization, login handshake |
| `world/` | Chunk storage (LevelDB), **100% identical terrain & biome generation**, block ticks |
| `entity/` | Entity base, players, item drops, falling sand |
| `server/` | Player management, block interaction, inventory logic |
| `core/` | Items, materials, NBT, math utilities |

---

## Comparison with Vanilla Server

Instead of a bulky list of pros and cons, here is how `alpha_server` fundamentally differs from the original Java implementation:

| Feature / Aspect | Vanilla Alpha 1.2.6 (Java) | `alpha_server` (C++) |
| :--- | :--- | :--- |
| **Performance & Architecture** | | |
| Memory Footprint | High (JVM overhead) | Minimal (Native C++) |
| Latency & Stutter | Impacted by GC pauses | Predictable, zero GC overhead |
| Chunk I/O | Synchronous (blocks main thread)| Asynchronous (dedicated LevelDB thread) |
| Packet Dispatch | Reflection-based | Direct virtual calls |
| Network Loop | Standard | Drain-all per tick (reduces movement lag) |
| Entity Tracking | Every 2 ticks | Every tick |
| **World & Gameplay** | | |
| **Terrain & Biomes** | Original algorithm | **Identical to original (1:1 generation)** |
| Mobs & AI | Fully implemented | 🚧 Work in progress |
| Redstone Logic | Fully implemented | 🚧 Work in progress |
| Tile Entities | Chest, Furnace, Sign logic | Fully work! |
| Dimensions | Overworld & Nether | Overworld only |
| Authentication | Online mode (HTTP session check) | Server ID sent, HTTP session check not implemented |

---

## Roadmap

To reach full feature parity with the vanilla server, the following features are pending implementation:

- Mob AI, spawning, and combat mechanics
- Redstone signal propagation
- Online mode session verification (HTTP check against session server)
- Nether dimension generation and transitions
- Operator commands (`/give`, `/tp`, etc.)
- Proper lighting propagation on block change (currently relies on heightmap only)

---

> Not an official Minecraft product. Not approved by or associated with Mojang or Microsoft.