#include "titanext/ext4_engine.hpp"
#include "titanext/crc32c.hpp"
#include "titanext/compression.hpp"

#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <cassert>

namespace fs = std::filesystem;

static int g_failures = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[-] ASSERTION FAILED: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            g_failures++; \
        } else { \
            std::cout << "[+] PASSED: " << msg << "\n"; \
        } \
    } while (0)

void TestCrc32c() {
    std::cout << "\n=== TestCrc32c ===\n";
    std::string test = "123456789";
    std::span<const uint8_t> data(reinterpret_cast<const uint8_t*>(test.data()), test.size());
    uint32_t crc = titanext::Crc32c::Calculate(data);
    TEST_ASSERT(crc == 0xE3069283U, "CRC32C standard test vector (123456789)");

    std::string empty = "";
    std::span<const uint8_t> emptyData(reinterpret_cast<const uint8_t*>(empty.data()), 0);
    uint32_t crcEmpty = titanext::Crc32c::Calculate(emptyData);
    TEST_ASSERT(crcEmpty == 0x00000000U, "CRC32C empty vector");
}

void TestCompression() {
    std::cout << "\n=== TestCompression ===\n";
    std::string text = "TitanExt High-Performance Ext4 Filesystem Storage Suite for Windows NT. ";
    std::vector<uint8_t> src;
    for (int i = 0; i < 50; ++i) src.insert(src.end(), text.begin(), text.end());

    std::vector<uint8_t> comp;
    size_t cSize = titanext::CompressionEngine::CompressLZ4(src, comp);
    TEST_ASSERT(cSize > 0 && cSize < src.size(), "LZ4 compressed size smaller than source");

    std::vector<uint8_t> decomp(src.size(), 0);
    bool ok = titanext::CompressionEngine::DecompressLZ4(comp, decomp);
    TEST_ASSERT(ok, "LZ4 decompression succeeded");
    TEST_ASSERT(decomp == src, "LZ4 decompressed matches original");
}

void TestExt4FormatAndMount() {
    std::cout << "\n=== TestExt4FormatAndMount ===\n";
    fs::path tempDir = fs::temp_directory_path() / "titanext_test_suite";
    fs::create_directories(tempDir);
    fs::path imgPath = tempDir / "test_ext4.img";

    uint64_t diskSize = 16ULL * 1024 * 1024; // 16 MB
    std::string label = "UbuntuTest";

    // 1. Test Format
    bool formatted = titanext::Ext4Engine::FormatVolume(imgPath.string(), diskSize, label);
    TEST_ASSERT(formatted, "FormatVolume succeeded");
    TEST_ASSERT(fs::exists(imgPath), "Image file created on disk");

    // 2. Test Open and Superblock
    titanext::Ext4Engine engine;
    bool opened = engine.Open(imgPath.string(), true);
    TEST_ASSERT(opened, "Engine Open succeeded");
    TEST_ASSERT(engine.IsOpen(), "Engine reports open state");
    TEST_ASSERT(engine.BlockSize() == 4096, "Block size is 4096 bytes");
    TEST_ASSERT(engine.VolumeLabel() == label, "Volume label matches");

    const auto& sb = engine.SuperBlock();
    TEST_ASSERT(sb.s_magic == titanext::EXT4_SUPER_MAGIC, "Superblock magic is 0xEF53");
    TEST_ASSERT(sb.s_inode_size == 256, "Inode size is 256 bytes");
    TEST_ASSERT(sb.s_first_ino == 11, "First user inode is 11");
    TEST_ASSERT(sb.s_feature_incompat & titanext::EXT4_FEATURE_INCOMPAT_EXTENTS, "Extents feature enabled");

    // 3. Test Inode Reading (Root Inode #2)
    titanext::Ext4Inode rootInode{};
    bool rootOk = engine.ReadInode(titanext::EXT4_ROOT_INO, rootInode);
    TEST_ASSERT(rootOk, "ReadInode 2 (Root) succeeded");
    TEST_ASSERT(rootInode.IsDirectory(), "Root inode is a directory");
    TEST_ASSERT(rootInode.HasExtents(), "Root inode uses extents");

    // 4. Test Extents Resolution
    std::vector<titanext::FileExtentMapping> mappings;
    bool extOk = engine.ResolveExtents(rootInode, mappings);
    TEST_ASSERT(extOk, "ResolveExtents succeeded on Root inode");
    TEST_ASSERT(!mappings.empty(), "Root inode has at least one extent");
    TEST_ASSERT(mappings[0].physicalBlock == 60, "Root directory block mapped to block 60");

    // 5. Test Directory Traversal
    std::vector<titanext::DirEntryInfo> dirEntries;
    bool dirOk = engine.ReadDirectory(titanext::EXT4_ROOT_INO, dirEntries);
    TEST_ASSERT(dirOk, "ReadDirectory succeeded on Root directory");
    TEST_ASSERT(dirEntries.size() >= 2, "Directory has at least '.' and '..'");

    bool foundDot = false, foundDotDot = false;
    for (const auto& ent : dirEntries) {
        if (ent.name == ".") foundDot = true;
        if (ent.name == "..") foundDotDot = true;
    }
    TEST_ASSERT(foundDot && foundDotDot, "Root directory contains '.' and '..'");

    // 6. Test Path Resolution
    uint32_t resolvedIno = 0;
    bool resRoot = engine.ResolvePath("/", resolvedIno);
    TEST_ASSERT(resRoot && resolvedIno == titanext::EXT4_ROOT_INO, "ResolvePath '/' returns Inode 2");

    // 7. Test Scrubber & Integrity Check
    titanext::ScrubReport report{};
    bool scrubOk = engine.CheckAndScrub(report);
    TEST_ASSERT(scrubOk, "CheckAndScrub completed successfully");
    TEST_ASSERT(report.isClean, "Scrubber reports volume is clean");
    TEST_ASSERT(report.errorsDetected == 0, "Zero errors detected during scrub");
    TEST_ASSERT(report.scannedBlocks > 0, "Scanned blocks recorded");

    engine.Close();
    fs::remove_all(tempDir);
}

int main() {
    std::cout << "=================================================\n";
    std::cout << "         TitanExt Core Unit Test Battery         \n";
    std::cout << "=================================================\n";

    TestCrc32c();
    TestCompression();
    TestExt4FormatAndMount();

    std::cout << "\n-------------------------------------------------\n";
    if (g_failures == 0) {
        std::cout << " ALL UNIT TESTS COMPLETED SUCCESSFULLY (0 ERRORS)\n";
        std::cout << "-------------------------------------------------\n";
        return 0;
    } else {
        std::cerr << " TEST SUITE FAILED WITH " << g_failures << " ERROR(S)\n";
        std::cout << "-------------------------------------------------\n";
        return 1;
    }
}
