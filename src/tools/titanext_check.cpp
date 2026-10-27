#include "titanext/ext4_engine.hpp"
#include <iostream>
#include <string>
#include <iomanip>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "TitanExt Inspection & Consistency Scrubber Tool v1.0.0\n";
        std::cout << "Usage: titanext_check <device_or_image_path> [--repair] [--tree]\n";
        return 1;
    }

    std::string path = argv[1];
    bool doRepair = false;
    bool showTree = false;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--repair" || arg == "/f") {
            doRepair = true;
        } else if (arg == "--tree") {
            showTree = true;
        }
    }

    titanext::Ext4Engine engine;
    if (!engine.Open(path, !doRepair)) {
        std::cerr << "[-] Error: Failed to open ext4 volume: " << path << "\n";
        return 1;
    }

    const auto& sb = engine.SuperBlock();
    std::cout << "=================================================\n";
    std::cout << "           TitanExt Volume Inspection            \n";
    std::cout << "=================================================\n";
    std::cout << "Volume Path:        " << path << "\n";
    std::cout << "Volume Label:       " << engine.VolumeLabel() << "\n";
    std::cout << "Filesystem Magic:   0x" << std::hex << sb.s_magic << std::dec << "\n";
    std::cout << "Block Size:         " << engine.BlockSize() << " bytes\n";
    std::cout << "Total Blocks:       " << ((static_cast<uint64_t>(sb.s_blocks_count_hi) << 32) | sb.s_blocks_count_lo) << "\n";
    std::cout << "Free Blocks:        " << ((static_cast<uint64_t>(sb.s_free_blocks_count_hi) << 32) | sb.s_free_blocks_count_lo) << "\n";
    std::cout << "Total Inodes:       " << sb.s_inodes_count << "\n";
    std::cout << "Free Inodes:        " << sb.s_free_inodes_count << "\n";
    std::cout << "Block Groups:       " << engine.BlockGroupCount() << "\n";
    std::cout << "Inode Size:         " << sb.s_inode_size << " bytes\n";
    std::cout << "Feature Incompat:   0x" << std::hex << sb.s_feature_incompat << std::dec;
    if (sb.s_feature_incompat & titanext::EXT4_FEATURE_INCOMPAT_EXTENTS) std::cout << " (EXTENTS)";
    if (sb.s_feature_incompat & titanext::EXT4_FEATURE_INCOMPAT_64BIT) std::cout << " (64BIT)";
    std::cout << "\n";

    std::cout << "\n--- Performing Integrity & Bit-Rot Scrub ---\n";
    titanext::ScrubReport report{};
    if (engine.CheckAndScrub(report)) {
        std::cout << "Blocks Scanned:     " << report.scannedBlocks << "\n";
        std::cout << "Inodes Scanned:     " << report.scannedInodes << "\n";
        std::cout << "Errors Detected:    " << report.errorsDetected << "\n";
        if (report.isClean) {
            std::cout << "[+] STATUS: VOLUME IS HEALTHY AND CONSISTENT\n";
        } else {
            std::cout << "[-] STATUS: INTEGRITY ANOMALIES DETECTED\n";
        }
    }

    if (showTree) {
        std::cout << "\n--- Root Directory Tree (Inode 2) ---\n";
        std::vector<titanext::DirEntryInfo> entries;
        if (engine.ReadDirectory(titanext::EXT4_ROOT_INO, entries)) {
            for (const auto& e : entries) {
                std::cout << "  [" << (e.fileType == titanext::EXT4_FT_DIR ? "DIR " : "FILE") << "] "
                          << std::setw(6) << e.inodeNumber << "  "
                          << std::setw(10) << e.size << " bytes  "
                          << e.name << "\n";
            }
        }
    }

    return report.isClean ? 0 : 1;
}
