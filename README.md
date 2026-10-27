# TitanExt: High-Performance Ext4/3/2 Storage Engine for Windows

[![TitanExt CI](https://github.com/MicaNT-Kernel/TitanExt/actions/workflows/ci.yml/badge.svg)](https://github.com/MicaNT-Kernel/TitanExt/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-orange.svg)]()
[![Compliance Audited](https://img.shields.io/badge/Clean--Room-Sentinel%20Audited-brightgreen.svg)](CLEAN_ROOM_PROVENANCE.md)

![TitanExt](assets/titanext_hero.jpg)

**TitanExt** is an independent, clean-room implementation of the **Fourth Extended Filesystem (Ext4, Ext3, Ext2)** architecture designed in modern ISO C++23. Built from first principles, TitanExt delivers native userland volume formatting, File Explorer drive mounting via WinFsp, high-speed 48-bit extent tree traversal, Castagnoli CRC32C metadata checksums, and automated consistency scrubbing for Windows NT and POSIX platforms without kernel drivers or BSOD risks.

---

## Key Features

- **Ext4/3/2 Architecture & On-Disk Format**: Clean-room implementation verifying `0xEF53` superblocks at `0x400` (1024 bytes), block group descriptor tables (GDT), 64-bit block numbers, and inode tables.
- **48-Bit Extent Trees (`0xF30A`)**: Ultra-fast logical-to-physical block mapping capable of addressing 32,768 contiguous physical disk blocks per extent with leaf and index node recursion.
- **Native Windows Explorer Drive Mounting (`titanext_mount`)**: Mount any Linux SSD, Raspberry Pi SD card, dual-boot partition, or disk image (`.img`, `.raw`, WSL2 `.vhdx`) directly to a Windows drive letter (e.g. `E:\` or `L:\`). Reports `FileSystemName = L"EXT4"`.
- **Zero Kernel BSODs (WinFsp User-Mode Bridge)**: Runs 100% in userland via the WHQL-certified WinFsp architecture, completely eliminating the crash risks of legacy kernel drivers like `ext2fsd`.
- **Windows NT Background Service (`titanext_svc`)**: Background daemon providing volume auto-mounting, Named Pipe IPC (`\\.\pipe\titanext_ipc`), and autonomous 24-hour extent and inode consistency scrubbing.
- **Inspection & Consistency Scrubber (`titanext_check`)**: Inode auditing, extent verification, directory validation, and volume integrity diagnosis via `--repair` / `/f`.
- **Formatting Utility (`titanext_format`)**: Format disk images or physical drives with standard ext4 layout, root inode (inode 2), and directory records.
- **1-Click Standalone Installer (`titanext_setup`)**: Single executable embedding all binaries, hero artwork, and the signed WinFsp runtime MSI in an LZ4-compressed payload.
- **Titan Storage Hub Integration**: Seamlessly recognized, monitored, mounted, and scrubbed through the unified **Titan Storage Hub** control center.
- **Strict Clean-Room Provenance**: 100% written under 17 U.S.C. § 1201(f) & *Google v. Oracle* interoperability doctrines, audited on every commit by our automated Sentinel.

---

## Utilities & Architecture

### 1. `titanext_mount` (User-Mode Filesystem Bridge)
Mounts any raw disk image (`.raw`, `.img`, `.vhdx`) or physical drive to a Windows drive letter:
```bash
# Mount raw Linux disk image to E:
titanext_mount linux_disk.img E:

# Mount Raspberry Pi physical microSD card to L:
titanext_mount \\.\PhysicalDrive1 L: --label RaspberryPi
```

### 2. `titanext_svc` (Windows NT Background Service)
Autonomous service daemon for volume auto-mounting, IPC, and 24-hour integrity scrubbing:
```bash
# Register and start as Windows Auto-Start Service
titanext_svc install

# Run in interactive console debug mode
titanext_svc run

# Uninstall service
titanext_svc uninstall
```

### 3. `titanext_check` (Inspection & Scrubber Tool)
Inspect volume superblock, verify block groups, audit extent trees, and check directory health:
```bash
# Inspect volume and list directory tree
titanext_check linux_disk.img --tree

# Perform consistency scrub and verification
titanext_check linux_disk.img --repair
```

### 4. `titanext_format` (CLI Formatting Utility)
Format any disk image or physical partition to Ext4:
```bash
titanext_format disk.img --size 512 --label "UbuntuRoot"
```

### 5. `titanext_setup` (Standalone GUI Installer)
Pure Win32 dark-mode GUI installer with Left-Sidebar Studio layout, automatic WinFsp detection, silent driver deployment, PATH configuration, and Windows Add/Remove Programs integration.

---

## Building from Source

### Prerequisites
- CMake 3.24+
- Modern C++23 Compiler (Clang 18+, MSVC 2022 v17.8+, or GCC 14+)
- Ninja build tool (recommended)

### Windows Build (Clang / Ninja)
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
```

### Run Test Battery
```bash
ctest --test-dir build --output-on-failure
# Or directly:
./build/titanext_tests.exe
```

### Run Clean-Room Sentinel Audit
```bash
node scripts/clean_room_sentinel.js --local
```

### Create Distribution ZIP
```bash
cpack --config build/CPackConfig.cmake -B build/dist
```

---

## Legal & Clean-Room Architecture
TitanExt is an independent clean-room implementation developed for functional interoperability under 17 U.S.C. § 1201(f) and SCOTUS fair use doctrine (*Google v. Oracle*). See [CLEAN_ROOM_PROVENANCE.md](CLEAN_ROOM_PROVENANCE.md) for full compliance declarations.

---

## License
Licensed under the Apache License, Version 2.0.  
Copyright © 2026 Daniel Elliott <ssfdre38@gmail.com>.
