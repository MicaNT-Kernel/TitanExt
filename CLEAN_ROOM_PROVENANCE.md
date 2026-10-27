# Clean-Room Provenance & Interoperability Declaration

## 1. Statutory Authority and Legal Foundation
This codebase, **TitanExt**, is an independent clean-room implementation of the Linux **Ext4, Ext3, and Ext2** filesystem specifications, developed exclusively for functional interoperability on Windows NT and POSIX platforms.

Development strictly adheres to:
1. **17 U.S.C. § 1201(f)** (Reverse Engineering for Interoperability):
   Permits reverse engineering and clean-room implementation of computer programs solely for the purpose of enabling functional interoperability of an independently created computer program with other programs.
2. ***Google LLC v. Oracle America, Inc.*, 141 S. Ct. 1164 (2021)**:
   Affirmed that declaring code, filesystem data structures, functional interfaces, and on-disk record geometries necessary for interoperability are not subject to copyright preclusion.
3. ***Sega Enterprises Ltd. v. Accolade, Inc.*, 977 F.2d 1510 (9th Cir. 1992)**:
   Established that functional interfaces and data structures required to achieve system compatibility constitute unprotected functional requirements.

---

## 2. Clean-Room Methodology & Safeguards
- **Zero Exposure**: No proprietary kernel source code, internal leaked trees, or decompiled intellectual property was accessed or used during the creation of this engine.
- **Specification-Based**: All disk formats, superblock layouts, extent trees, block group descriptors, and inode fields were authored strictly from publicly documented on-disk specifications and mathematical standards.
- **Independent Modern Implementation**: 100% of the logic is written in ISO C++23, using modern memory safety features (`std::span`, RAII handles, strict bounds verification).
- **Automated Sentinel Audit**: Every commit is inspected by the automated Sentinel script (`scripts/clean_room_sentinel.js`) before acceptance.

---

## 3. Author & Ownership
- **Lead Architect**: Daniel Elliott <ssfdre38@gmail.com>
- **Organization**: MicaNT-Kernel
- **License**: Apache License, Version 2.0
