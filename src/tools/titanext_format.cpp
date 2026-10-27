#include "titanext/ext4_engine.hpp"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "TitanExt Formatting Utility v1.0.0\n";
        std::cout << "Usage: titanext_format <device_or_image_path> [--size <MB>] [--label <NAME>]\n";
        return 1;
    }

    std::string path = argv[1];
    uint64_t sizeMB = 64; // Default 64 MB
    std::string label = "TitanExt";

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--size" && i + 1 < argc) {
            sizeMB = std::stoull(argv[++i]);
        } else if (arg == "--label" && i + 1 < argc) {
            label = argv[++i];
        }
    }

    std::cout << "[+] Formatting target: " << path << "\n";
    std::cout << "[+] Target size: " << sizeMB << " MB\n";
    std::cout << "[+] Volume label: " << label << "\n";

    uint64_t sizeBytes = sizeMB * 1024ULL * 1024ULL;
    if (!titanext::Ext4Engine::FormatVolume(path, sizeBytes, label)) {
        std::cerr << "[-] Error: Failed to format ext4 filesystem on " << path << "\n";
        return 1;
    }

    std::cout << "[+] Ext4 filesystem successfully initialized!\n";
    std::cout << "[+] Superblock magic: 0xEF53 (Ext4 with Extents)\n";
    std::cout << "[+] Root directory inode: 2\n";
    return 0;
}
