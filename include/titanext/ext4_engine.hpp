#pragma once

#include "titanext/ext4_types.hpp"
#include "titanext/crc32c.hpp"

#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <filesystem>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace titanext {

struct ScrubReport {
    uint64_t totalBlocks{0};
    uint64_t scannedBlocks{0};
    uint64_t totalInodes{0};
    uint64_t scannedInodes{0};
    uint64_t errorsDetected{0};
    uint64_t errorsRepaired{0};
    bool     isClean{true};
    std::string volumeLabel;
};

class Ext4Engine {
public:
    Ext4Engine() = default;
    ~Ext4Engine() { Close(); }

    bool Open(const std::string& path, bool readOnly = true) {
        Close();
        filePath_ = path;
        readOnly_ = readOnly;

#ifdef _WIN32
        DWORD access = GENERIC_READ;
        if (!readOnly) access |= GENERIC_WRITE;

        hFile_ = CreateFileA(
            path.c_str(),
            access,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS,
            nullptr
        );
        if (hFile_ == INVALID_HANDLE_VALUE) {
            return false;
        }

        LARGE_INTEGER liSize{};
        if (GetFileSizeEx(hFile_, &liSize)) {
            totalBytes_ = static_cast<uint64_t>(liSize.QuadPart);
        }
#else
        auto mode = std::ios::binary | std::ios::in;
        if (!readOnly) mode |= std::ios::out;
        fileStream_.open(path, mode);
        if (!fileStream_.is_open()) return false;

        fileStream_.seekg(0, std::ios::end);
        totalBytes_ = static_cast<uint64_t>(fileStream_.tellg());
        fileStream_.seekg(0, std::ios::beg);
#endif

        return LoadSuperBlock();
    }

    void Close() {
#ifdef _WIN32
        if (hFile_ != INVALID_HANDLE_VALUE && hFile_ != nullptr) {
            CloseHandle(hFile_);
            hFile_ = INVALID_HANDLE_VALUE;
        }
#else
        if (fileStream_.is_open()) {
            fileStream_.close();
        }
#endif
        isOpen_ = false;
        groupDescriptors_.clear();
    }

    [[nodiscard]] bool IsOpen() const noexcept { return isOpen_; }
    [[nodiscard]] uint32_t BlockSize() const noexcept { return blockSize_; }
    [[nodiscard]] uint64_t TotalBytes() const noexcept { return totalBytes_; }
    [[nodiscard]] const std::string& VolumeLabel() const noexcept { return label_; }
    [[nodiscard]] const Ext4SuperBlock& SuperBlock() const noexcept { return sb_; }
    [[nodiscard]] size_t BlockGroupCount() const noexcept { return groupDescriptors_.size(); }

    bool ReadBytes(uint64_t offset, size_t size, void* outBuf) {
        if (!outBuf || size == 0) return true;

#ifdef _WIN32
        LARGE_INTEGER li;
        li.QuadPart = static_cast<LONGLONG>(offset);
        if (!SetFilePointerEx(hFile_, li, nullptr, FILE_BEGIN)) {
            return false;
        }

        DWORD bytesRead = 0;
        if (!ReadFile(hFile_, outBuf, static_cast<DWORD>(size), &bytesRead, nullptr) || bytesRead != size) {
            return false;
        }
        return true;
#else
        fileStream_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        fileStream_.read(reinterpret_cast<char*>(outBuf), size);
        return fileStream_.gcount() == static_cast<std::streamsize>(size);
#endif
    }

    bool WriteBytes(uint64_t offset, size_t size, const void* inBuf) {
        if (readOnly_ || !inBuf || size == 0) return false;

#ifdef _WIN32
        LARGE_INTEGER li;
        li.QuadPart = static_cast<LONGLONG>(offset);
        if (!SetFilePointerEx(hFile_, li, nullptr, FILE_BEGIN)) {
            return false;
        }

        DWORD written = 0;
        if (!WriteFile(hFile_, inBuf, static_cast<DWORD>(size), &written, nullptr) || written != size) {
            return false;
        }
        return true;
#else
        fileStream_.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
        fileStream_.write(reinterpret_cast<const char*>(inBuf), size);
        return fileStream_.good();
#endif
    }

    bool ReadBlock(uint64_t blockIndex, std::vector<uint8_t>& outBlock) {
        outBlock.resize(blockSize_);
        uint64_t offset = blockIndex * blockSize_;
        return ReadBytes(offset, blockSize_, outBlock.data());
    }

    bool ReadInode(uint32_t ino, Ext4Inode& outInode) {
        if (ino == 0 || ino > sb_.s_inodes_count) return false;

        uint32_t group = (ino - 1) / sb_.s_inodes_per_group;
        uint32_t index = (ino - 1) % sb_.s_inodes_per_group;

        if (group >= groupDescriptors_.size()) return false;
        const auto& gd = groupDescriptors_[group];

        uint64_t inodeTableBlock = gd.bg_inode_table_lo;
        if (sb_.s_feature_incompat & EXT4_FEATURE_INCOMPAT_64BIT) {
            inodeTableBlock |= (static_cast<uint64_t>(gd.bg_inode_table_hi) << 32);
        }

        uint64_t byteOffset = (inodeTableBlock * blockSize_) + (index * sb_.s_inode_size);
        std::vector<uint8_t> inodeRaw(sb_.s_inode_size, 0);
        if (!ReadBytes(byteOffset, sb_.s_inode_size, inodeRaw.data())) {
            return false;
        }

        std::memcpy(&outInode, inodeRaw.data(), sizeof(Ext4Inode));
        return true;
    }

    bool ResolveExtents(const Ext4Inode& inode, std::vector<FileExtentMapping>& outMappings) {
        outMappings.clear();
        if (!inode.HasExtents()) return false;

        const auto* hdr = reinterpret_cast<const Ext4ExtentHeader*>(inode.i_block);
        if (hdr->eh_magic != EXT4_EXT_MAGIC) return false;

        return TraverseExtentTree(hdr, outMappings);
    }

    bool ReadFileData(const Ext4Inode& inode, uint64_t fileOffset, size_t size, std::vector<uint8_t>& outData) {
        outData.clear();
        uint64_t fileSize = inode.FileSize();
        if (fileOffset >= fileSize) return true;

        size_t bytesToRead = std::min(static_cast<uint64_t>(size), fileSize - fileOffset);
        outData.resize(bytesToRead);

        std::vector<FileExtentMapping> mappings;
        if (!ResolveExtents(inode, mappings)) {
            return false;
        }

        size_t bytesDone = 0;
        uint64_t curOffset = fileOffset;

        while (bytesDone < bytesToRead) {
            uint32_t logicalBlock = static_cast<uint32_t>(curOffset / blockSize_);
            uint32_t blockOffset = static_cast<uint32_t>(curOffset % blockSize_);
            uint32_t chunk = std::min(blockSize_ - blockOffset, static_cast<uint32_t>(bytesToRead - bytesDone));

            uint64_t physicalBlock = 0;
            bool found = false;
            for (const auto& m : mappings) {
                if (logicalBlock >= m.logicalBlock && logicalBlock < m.logicalBlock + m.blockCount) {
                    physicalBlock = m.physicalBlock + (logicalBlock - m.logicalBlock);
                    found = true;
                    break;
                }
            }

            if (found && physicalBlock != 0) {
                uint64_t diskOffset = (physicalBlock * blockSize_) + blockOffset;
                if (!ReadBytes(diskOffset, chunk, outData.data() + bytesDone)) {
                    return false;
                }
            } else {
                std::memset(outData.data() + bytesDone, 0, chunk);
            }

            bytesDone += chunk;
            curOffset += chunk;
        }

        return true;
    }

    bool ReadDirectory(uint32_t dirIno, std::vector<DirEntryInfo>& outEntries) {
        outEntries.clear();
        Ext4Inode dirInode{};
        if (!ReadInode(dirIno, dirInode) || !dirInode.IsDirectory()) {
            return false;
        }

        std::vector<uint8_t> dirData;
        if (!ReadFileData(dirInode, 0, static_cast<size_t>(dirInode.FileSize()), dirData)) {
            return false;
        }

        size_t offset = 0;
        while (offset + sizeof(Ext4DirEntry2) - 255 <= dirData.size()) {
            const auto* entry = reinterpret_cast<const Ext4DirEntry2*>(dirData.data() + offset);
            if (entry->rec_len < 8 || offset + entry->rec_len > dirData.size()) {
                break;
            }

            if (entry->inode != 0 && entry->name_len > 0) {
                DirEntryInfo info;
                info.inodeNumber = entry->inode;
                info.fileType = entry->file_type;
                info.name = std::string(entry->name, entry->name_len);

                Ext4Inode childInode{};
                if (ReadInode(entry->inode, childInode)) {
                    info.size = childInode.FileSize();
                    info.mode = childInode.i_mode;
                    info.mtime = childInode.i_mtime;
                }
                outEntries.push_back(std::move(info));
            }

            offset += entry->rec_len;
        }
        return true;
    }

    bool ResolvePath(const std::string& path, uint32_t& outIno) {
        if (path.empty() || path == "/" || path == "\\") {
            outIno = EXT4_ROOT_INO;
            return true;
        }

        std::vector<std::string> parts;
        std::string cur;
        for (char c : path) {
            if (c == '/' || c == '\\') {
                if (!cur.empty()) {
                    parts.push_back(cur);
                    cur.clear();
                }
            } else {
                cur += c;
            }
        }
        if (!cur.empty()) parts.push_back(cur);

        uint32_t currentIno = EXT4_ROOT_INO;
        for (const auto& part : parts) {
            std::vector<DirEntryInfo> entries;
            if (!ReadDirectory(currentIno, entries)) {
                return false;
            }

            bool found = false;
            for (const auto& e : entries) {
                if (e.name == part) {
                    currentIno = e.inodeNumber;
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }

        outIno = currentIno;
        return true;
    }

    bool CheckAndScrub(ScrubReport& report) {
        report = ScrubReport{};
        if (!isOpen_) return false;

        report.totalBlocks = (static_cast<uint64_t>(sb_.s_blocks_count_hi) << 32) | sb_.s_blocks_count_lo;
        report.totalInodes = sb_.s_inodes_count;
        report.volumeLabel = label_;

        // 1. Verify Superblock Magic
        if (sb_.s_magic != EXT4_SUPER_MAGIC) {
            report.errorsDetected++;
            report.isClean = false;
            return false;
        }

        // 2. Validate all Block Group Descriptors
        for (size_t i = 0; i < groupDescriptors_.size(); ++i) {
            const auto& gd = groupDescriptors_[i];
            if (gd.bg_inode_table_lo == 0 || gd.bg_block_bitmap_lo == 0 || gd.bg_inode_bitmap_lo == 0) {
                report.errorsDetected++;
            }
        }

        // 3. Inspect Root Inode
        Ext4Inode rootInode{};
        if (!ReadInode(EXT4_ROOT_INO, rootInode) || !rootInode.IsDirectory()) {
            report.errorsDetected++;
            report.isClean = false;
        }

        // 4. Inspect Inodes & Directory Tree
        std::vector<DirEntryInfo> rootEntries;
        if (ReadDirectory(EXT4_ROOT_INO, rootEntries)) {
            report.scannedInodes += rootEntries.size() + 1;
            for (const auto& e : rootEntries) {
                Ext4Inode child{};
                if (!ReadInode(e.inodeNumber, child)) {
                    report.errorsDetected++;
                } else if (child.HasExtents()) {
                    std::vector<FileExtentMapping> maps;
                    if (!ResolveExtents(child, maps)) {
                        report.errorsDetected++;
                    } else {
                        for (const auto& m : maps) {
                            report.scannedBlocks += m.blockCount;
                        }
                    }
                }
            }
        }

        report.isClean = (report.errorsDetected == 0);
        return true;
    }

    static bool FormatVolume(const std::string& path, uint64_t sizeBytes, const std::string& label = "TitanExt") {
        constexpr uint32_t BLK_SIZE = 4096;
        if (sizeBytes < 16ULL * 1024 * 1024) { // Minimum 16 MB
            sizeBytes = 16ULL * 1024 * 1024;
        }

        uint64_t totalBlocks = sizeBytes / BLK_SIZE;
        uint32_t blocksPerGroup = 32768;
        uint32_t inodesPerGroup = 8192;
        uint32_t groupCount = static_cast<uint32_t>((totalBlocks + blocksPerGroup - 1) / blocksPerGroup);
        if (groupCount == 0) groupCount = 1;

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;

        // Allocate disk image sparse / zeroed
        std::vector<uint8_t> zeroBlock(BLK_SIZE, 0);

        // Block 0: Padding (1024 bytes) + Superblock (1024 bytes)
        std::vector<uint8_t> block0(BLK_SIZE, 0);
        Ext4SuperBlock sb{};
        sb.s_inodes_count = groupCount * inodesPerGroup;
        sb.s_blocks_count_lo = static_cast<uint32_t>(totalBlocks & 0xFFFFFFFF);
        sb.s_blocks_count_hi = static_cast<uint32_t>((totalBlocks >> 32) & 0xFFFFFFFF);
        sb.s_free_blocks_count_lo = static_cast<uint32_t>((totalBlocks - (groupCount * 100)) & 0xFFFFFFFF);
        sb.s_free_inodes_count = sb.s_inodes_count - 11;
        sb.s_first_data_block = 0; // For 4KB blocks
        sb.s_log_block_size = 2;   // 1024 << 2 = 4096
        sb.s_log_cluster_size = 2;
        sb.s_blocks_per_group = blocksPerGroup;
        sb.s_clusters_per_group = blocksPerGroup;
        sb.s_inodes_per_group = inodesPerGroup;
        sb.s_magic = EXT4_SUPER_MAGIC;
        sb.s_state = 1; // Cleanly unmounted
        sb.s_errors = 1;
        sb.s_minor_rev_level = 0;
        sb.s_creator_os = 0; // Linux
        sb.s_rev_level = 1;  // Dynamic
        sb.s_first_ino = 11; // Standard first user inode
        sb.s_inode_size = 256;
        sb.s_feature_compat = EXT4_FEATURE_COMPAT_HAS_JOURNAL | EXT4_FEATURE_COMPAT_DIR_INDEX;
        sb.s_feature_incompat = EXT4_FEATURE_INCOMPAT_FILETYPE | EXT4_FEATURE_INCOMPAT_EXTENTS | EXT4_FEATURE_INCOMPAT_64BIT;
        sb.s_feature_ro_compat = EXT4_FEATURE_RO_COMPAT_SPARSE_SUPER | EXT4_FEATURE_RO_COMPAT_LARGE_FILE | EXT4_FEATURE_RO_COMPAT_METADATA_CSUM;
        
        std::strncpy(sb.s_volume_name, label.c_str(), sizeof(sb.s_volume_name) - 1);
        std::memcpy(block0.data() + 1024, &sb, sizeof(sb));

        out.write(reinterpret_cast<const char*>(block0.data()), BLK_SIZE);

        // Block 1: Group Descriptors Table (GDT)
        std::vector<uint8_t> gdtBlock(BLK_SIZE, 0);
        uint64_t itableStart = 5; // Block 5 starts inode table
        Ext4GroupDesc gd{};
        gd.bg_block_bitmap_lo = 2;
        gd.bg_inode_bitmap_lo = 3;
        gd.bg_inode_table_lo = static_cast<uint32_t>(itableStart);
        gd.bg_free_blocks_count_lo = static_cast<uint16_t>(blocksPerGroup - 100);
        gd.bg_free_inodes_count_lo = static_cast<uint16_t>(inodesPerGroup - 11);
        gd.bg_used_dirs_count_lo = 2;
        std::memcpy(gdtBlock.data(), &gd, sizeof(gd));
        out.write(reinterpret_cast<const char*>(gdtBlock.data()), BLK_SIZE);

        // Block 2: Block allocation bitmap
        std::vector<uint8_t> blockBitmap(BLK_SIZE, 0);
        // Mark blocks 0..50 as used
        for (int i = 0; i < 7; ++i) blockBitmap[i] = 0xFF;
        out.write(reinterpret_cast<const char*>(blockBitmap.data()), BLK_SIZE);

        // Block 3: Inode allocation bitmap
        std::vector<uint8_t> inodeBitmap(BLK_SIZE, 0);
        inodeBitmap[0] = 0xFF; // Inodes 1..8 allocated
        inodeBitmap[1] = 0x07; // Inodes 9..11 allocated
        out.write(reinterpret_cast<const char*>(inodeBitmap.data()), BLK_SIZE);

        // Block 4: Reserved
        out.write(reinterpret_cast<const char*>(zeroBlock.data()), BLK_SIZE);

        // Block 5: Inode Table (Root Inode is Inode #2)
        // Inode 2 offset is at index (2 - 1) * 256 = 256 bytes in Block 5
        std::vector<uint8_t> itableBlock(BLK_SIZE, 0);
        Ext4Inode rootInode{};
        rootInode.i_mode = EXT4_S_IFDIR | 0755;
        rootInode.i_links_count = 3; // ., .., lost+found
        rootInode.i_size_lo = BLK_SIZE;
        rootInode.i_blocks_lo = 8; // 8 * 512 bytes = 4096
        rootInode.i_flags = EXT4_EXTENTS_FL;

        // Root Inode Extent: 1 block at physical block 60
        auto* extHdr = reinterpret_cast<Ext4ExtentHeader*>(rootInode.i_block);
        extHdr->eh_magic = EXT4_EXT_MAGIC;
        extHdr->eh_entries = 1;
        extHdr->eh_max = 4;
        extHdr->eh_depth = 0; // Leaf

        auto* extLeaf = reinterpret_cast<Ext4Extent*>(rootInode.i_block + sizeof(Ext4ExtentHeader));
        extLeaf->ee_block = 0;
        extLeaf->ee_len = 1;
        extLeaf->SetPhysicalBlock(60);

        std::memcpy(itableBlock.data() + 256, &rootInode, sizeof(rootInode));
        out.write(reinterpret_cast<const char*>(itableBlock.data()), BLK_SIZE);

        // Write remaining padding blocks up to block 59
        for (int b = 6; b < 60; ++b) {
            out.write(reinterpret_cast<const char*>(zeroBlock.data()), BLK_SIZE);
        }

        // Block 60: Root Directory Data Block
        // Contains '.' and '..'
        std::vector<uint8_t> rootDirBlock(BLK_SIZE, 0);
        auto* dot = reinterpret_cast<Ext4DirEntry2*>(rootDirBlock.data());
        dot->inode = EXT4_ROOT_INO;
        dot->rec_len = 12;
        dot->name_len = 1;
        dot->file_type = EXT4_FT_DIR;
        dot->name[0] = '.';

        auto* dotdot = reinterpret_cast<Ext4DirEntry2*>(rootDirBlock.data() + 12);
        dotdot->inode = EXT4_ROOT_INO;
        dotdot->rec_len = static_cast<uint16_t>(BLK_SIZE - 12);
        dotdot->name_len = 2;
        dotdot->file_type = EXT4_FT_DIR;
        dotdot->name[0] = '.';
        dotdot->name[1] = '.';

        out.write(reinterpret_cast<const char*>(rootDirBlock.data()), BLK_SIZE);

        // Pad file to target size
        uint64_t currentBlocks = 61;
        for (uint64_t b = currentBlocks; b < totalBlocks; ++b) {
            out.write(reinterpret_cast<const char*>(zeroBlock.data()), BLK_SIZE);
        }

        out.flush();
        return true;
    }

private:
    bool LoadSuperBlock() {
        std::vector<uint8_t> sbBuffer(1024, 0);
        if (!ReadBytes(1024, 1024, sbBuffer.data())) {
            return false;
        }

        std::memcpy(&sb_, sbBuffer.data(), sizeof(Ext4SuperBlock));
        if (sb_.s_magic != EXT4_SUPER_MAGIC) {
            return false;
        }

        blockSize_ = 1024 << sb_.s_log_block_size;
        char labelBuf[17]{0};
        std::memcpy(labelBuf, sb_.s_volume_name, 16);
        label_ = labelBuf;
        if (label_.empty()) label_ = "TitanExt";

        // Read Block Group Descriptors Table (GDT)
        uint64_t totalBlocks = (static_cast<uint64_t>(sb_.s_blocks_count_hi) << 32) | sb_.s_blocks_count_lo;
        uint32_t groupCount = static_cast<uint32_t>((totalBlocks + sb_.s_blocks_per_group - 1) / sb_.s_blocks_per_group);
        if (groupCount == 0) groupCount = 1;

        groupDescriptors_.resize(groupCount);
        uint64_t gdtBlock = (blockSize_ == 1024) ? 2 : 1;
        uint64_t gdtOffset = gdtBlock * blockSize_;

        size_t descSize = (sb_.s_feature_incompat & EXT4_FEATURE_INCOMPAT_64BIT) ? 64 : 32;
        std::vector<uint8_t> gdtRaw(groupCount * descSize);
        if (!ReadBytes(gdtOffset, gdtRaw.size(), gdtRaw.data())) {
            return false;
        }

        for (uint32_t i = 0; i < groupCount; ++i) {
            std::memset(&groupDescriptors_[i], 0, sizeof(Ext4GroupDesc));
            std::memcpy(&groupDescriptors_[i], gdtRaw.data() + (i * descSize), descSize);
        }

        isOpen_ = true;
        return true;
    }

    bool TraverseExtentTree(const Ext4ExtentHeader* hdr, std::vector<FileExtentMapping>& outMappings) {
        if (hdr->eh_magic != EXT4_EXT_MAGIC) return false;

        if (hdr->eh_depth == 0) {
            // Leaf node
            const auto* extent = reinterpret_cast<const Ext4Extent*>(
                reinterpret_cast<const uint8_t*>(hdr) + sizeof(Ext4ExtentHeader));
            for (uint16_t i = 0; i < hdr->eh_entries; ++i) {
                FileExtentMapping m;
                m.logicalBlock = extent[i].ee_block;
                m.blockCount = extent[i].ee_len;
                m.physicalBlock = extent[i].PhysicalBlock();
                outMappings.push_back(m);
            }
            return true;
        } else {
            // Internal index node
            const auto* idx = reinterpret_cast<const Ext4ExtentIdx*>(
                reinterpret_cast<const uint8_t*>(hdr) + sizeof(Ext4ExtentHeader));
            for (uint16_t i = 0; i < hdr->eh_entries; ++i) {
                std::vector<uint8_t> childBlock;
                if (!ReadBlock(idx[i].LeafBlock(), childBlock)) {
                    return false;
                }
                const auto* childHdr = reinterpret_cast<const Ext4ExtentHeader*>(childBlock.data());
                if (!TraverseExtentTree(childHdr, outMappings)) {
                    return false;
                }
            }
            return true;
        }
    }

    std::string filePath_;
    bool readOnly_{true};
    bool isOpen_{false};
    uint64_t totalBytes_{0};
    uint32_t blockSize_{4096};
    std::string label_;
    Ext4SuperBlock sb_{};
    std::vector<Ext4GroupDesc> groupDescriptors_;

#ifdef _WIN32
    HANDLE hFile_{INVALID_HANDLE_VALUE};
#else
    std::fstream fileStream_;
#endif
};

} // namespace titanext
