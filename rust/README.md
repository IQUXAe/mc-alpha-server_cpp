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

## First module

`alpha_bridge/` is a small `staticlib` crate that exposes:

- `alpha_gzip_compress`
- `alpha_gzip_decompress`
- `alpha_zstd_compress`
- `alpha_zstd_decompress`
- `alpha_buffer_free`

The ABI is intentionally tiny so the C++ side can adopt it incrementally.

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
