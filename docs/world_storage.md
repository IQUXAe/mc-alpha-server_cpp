# Legacy World Storage Support & On-the-fly Conversion

This document describes how to configure and use the legacy world storage features in the Minecraft Alpha 1.2.6 C++ server.

---

## 1. Features Overview

The server provides complete backwards compatibility with Java-style chunk files (obsolete Gzip-compressed base-36 directory hierarchy of `c.X.Z.dat` files):

### 1.1 Direct Legacy Gzip Storage Mode
* **Property**: `use-legacy-storage=true`
* **Behavior**: The server runs **directly** on the legacy Gzip chunk storage format. 
* **World Generation**: If no world files exist, setting this property will cause the server to generate and write a completely legacy-format world from the very beginning.
* **Performance**: Chunk loading and saving are executed asynchronously on a background thread pool, ensuring lag-free gameplay identical to modern LevelDB performance.
* **Warning System**: When active, a prominent console warning is displayed on startup:
  ```
  WARNING: Server is running in LEGACY Gzip world storage mode!
  Newly generated and modified chunks will be saved in legacy format.
  ```

### 1.2 On-the-fly Legacy World Conversion
* **Property**: `convert-legacy-world=true`
* **Behavior**: Converts existing legacy chunk files on startup to modern LevelDB + ZSTD.
* **Safety & Backup**: Automatically performs a timestamped world folder backup (`<world>_backup_<timestamp>`) before importing to prevent data loss.
* **Cleanup**: Deletes converted Gzip files and automatically cleans up empty base-36 directories from the bottom up.

---

## 2. Server Properties Configuration

Add the following keys to your `server.properties` file:

```properties
# Enable to run on and generate legacy Gzip worlds directly.
use-legacy-storage=false

# Enable to convert existing Gzip legacy chunks to LevelDB + ZSTD on startup.
# Requires 'use-legacy-storage' to be set to false.
convert-legacy-world=false
```

---

## 3. Storage Safety Warning Messages

To prevent accidental data loss or confusion, the server includes automatic directory scanning and prints detailed warnings in the console:

* **Legacy Files Detected under Modern Properties (Old World Type on disk, New configured)**:
  If Gzip chunks (old world type) are present on disk, but both `use-legacy-storage` and `convert-legacy-world` are `false` (meaning the new world type, LevelDB, is configured), a prominent warning is printed on startup to inform you that these legacy chunks will not be loaded or converted, and a new modern LevelDB world will be used/generated instead.

* **Modern LevelDB Files Detected under Legacy Properties (New World Type on disk, Old configured)**:
  If a modern LevelDB database (new world type) is detected on disk under the `db` directory but `use-legacy-storage=true` is set (meaning the old world type, Gzip, is configured in properties), a safety warning is printed on startup to alert you that the server is running on legacy Gzip chunk files and ignoring the existing LevelDB database.
