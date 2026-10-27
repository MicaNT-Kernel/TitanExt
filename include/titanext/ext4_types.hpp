#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <cstring>

namespace titanext {

constexpr uint16_t EXT4_SUPER_MAGIC     = 0xEF53;
constexpr uint16_t EXT4_EXT_MAGIC       = 0xF30A;
constexpr uint32_t EXT4_ROOT_INO        = 2;
constexpr uint32_t EXT4_GOOD_OLD_INODE_SIZE = 128;

// Feature Incompat Flags
constexpr uint32_t EXT4_FEATURE_INCOMPAT_FILETYPE = 0x0002;
constexpr uint32_t EXT4_FEATURE_INCOMPAT_EXTENTS  = 0x0040;
constexpr uint32_t EXT4_FEATURE_INCOMPAT_64BIT    = 0x0080;
constexpr uint32_t EXT4_FEATURE_INCOMPAT_FLEX_BG  = 0x0200;

// Feature Compat Flags
constexpr uint32_t EXT4_FEATURE_COMPAT_HAS_JOURNAL = 0x0004;
constexpr uint32_t EXT4_FEATURE_COMPAT_DIR_INDEX   = 0x0020;

// Feature Ro Compat Flags
constexpr uint32_t EXT4_FEATURE_RO_COMPAT_SPARSE_SUPER = 0x0001;
constexpr uint32_t EXT4_FEATURE_RO_COMPAT_LARGE_FILE   = 0x0002;
constexpr uint32_t EXT4_FEATURE_RO_COMPAT_METADATA_CSUM = 0x0400;

// Inode flags
constexpr uint32_t EXT4_EXTENTS_FL = 0x00080000;

// File types in dir entry
constexpr uint8_t EXT4_FT_UNKNOWN  = 0;
constexpr uint8_t EXT4_FT_REG_FILE = 1;
constexpr uint8_t EXT4_FT_DIR      = 2;
constexpr uint8_t EXT4_FT_CHRDEV   = 3;
constexpr uint8_t EXT4_FT_BLKDEV   = 4;
constexpr uint8_t EXT4_FT_FIFO     = 5;
constexpr uint8_t EXT4_FT_SOCK     = 6;
constexpr uint8_t EXT4_FT_SYMLINK  = 7;

// Inode file mode types (POSIX octal)
constexpr uint16_t EXT4_S_IFMT   = 0170000;
constexpr uint16_t EXT4_S_IFSOCK = 0140000;
constexpr uint16_t EXT4_S_IFLNK  = 0120000;
constexpr uint16_t EXT4_S_IFREG  = 0100000;
constexpr uint16_t EXT4_S_IFBLK  = 0060000;
constexpr uint16_t EXT4_S_IFDIR  = 0040000;
constexpr uint16_t EXT4_S_IFCHR  = 0020000;
constexpr uint16_t EXT4_S_IFIFO  = 0010000;

#pragma pack(push, 1)

struct Ext4SuperBlock {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count_lo;
    uint32_t s_r_blocks_count_lo;
    uint32_t s_free_blocks_count_lo;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_cluster_size;
    uint32_t s_blocks_per_group;
    uint32_t s_clusters_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    int16_t  s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;

    // Dynamic rev fields
    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    char     s_volume_name[16];
    char     s_last_mounted[64];
    uint32_t s_algorithm_usage_bitmap;

    // Performance hints
    uint8_t  s_prealloc_blocks;
    uint8_t  s_prealloc_dir_blocks;
    uint16_t s_reserved_gdt_blocks;

    // Journaling support
    uint8_t  s_journal_uuid[16];
    uint32_t s_journal_inum;
    uint32_t s_journal_dev;
    uint32_t s_last_orphan;
    uint32_t s_hash_seed[4];
    uint8_t  s_def_hash_version;
    uint8_t  s_jnl_backup_type;
    uint16_t s_desc_size;
    uint32_t s_default_mount_opts;
    uint32_t s_first_meta_bg;
    uint32_t s_mkfs_time;
    uint32_t s_jnl_blocks[17];

    // 64-bit support
    uint32_t s_blocks_count_hi;
    uint32_t s_r_blocks_count_hi;
    uint32_t s_free_blocks_count_hi;
    uint16_t s_min_extra_isize;
    uint16_t s_want_extra_isize;
    uint32_t s_flags;
    uint16_t s_raid_stride;
    uint16_t s_mmp_interval;
    uint64_t s_mmp_block;
    uint32_t s_raid_stripe_width;
    uint8_t  s_log_groups_per_flex;
    uint8_t  s_checksum_type;
    uint16_t s_reserved_pad;
    uint64_t s_kbytes_written;
    uint32_t s_snapshot_inum;
    uint32_t s_snapshot_id;
    uint64_t s_snapshot_r_blocks_count;
    uint32_t s_snapshot_list;
    uint32_t s_error_count;
    uint32_t s_first_error_time;
    uint32_t s_first_error_ino;
    uint64_t s_first_error_block;
    uint8_t  s_first_error_func[32];
    uint32_t s_first_error_line;
    uint32_t s_last_error_time;
    uint32_t s_last_error_ino;
    uint32_t s_last_error_line;
    uint64_t s_last_error_block;
    uint8_t  s_last_error_func[32];
    uint8_t  s_mount_opts[64];
    uint32_t s_usr_quota_inum;
    uint32_t s_grp_quota_inum;
    uint32_t s_overhead_blocks;
    uint32_t s_backup_bgs[2];
    uint8_t  s_encrypt_algos[4];
    uint8_t  s_encrypt_pw_salt[16];
    uint32_t s_lpf_ino;
    uint32_t s_prj_quota_inum;
    uint32_t s_checksum_seed;
    uint32_t s_reserved[98];
    uint32_t s_checksum;
};
static_assert(sizeof(Ext4SuperBlock) == 1024, "Ext4SuperBlock must be 1024 bytes");

struct Ext4GroupDesc {
    uint32_t bg_block_bitmap_lo;
    uint32_t bg_inode_bitmap_lo;
    uint32_t bg_inode_table_lo;
    uint16_t bg_free_blocks_count_lo;
    uint16_t bg_free_inodes_count_lo;
    uint16_t bg_used_dirs_count_lo;
    uint16_t bg_flags;
    uint32_t bg_exclude_bitmap_lo;
    uint16_t bg_block_bitmap_csum_lo;
    uint16_t bg_inode_bitmap_csum_lo;
    uint16_t bg_itable_unused_lo;
    uint16_t bg_checksum;

    // 64-bit support fields
    uint32_t bg_block_bitmap_hi;
    uint32_t bg_inode_bitmap_hi;
    uint32_t bg_inode_table_hi;
    uint16_t bg_free_blocks_count_hi;
    uint16_t bg_free_inodes_count_hi;
    uint16_t bg_used_dirs_count_hi;
    uint16_t bg_itable_unused_hi;
    uint32_t bg_exclude_bitmap_hi;
    uint16_t bg_block_bitmap_csum_hi;
    uint16_t bg_inode_bitmap_csum_hi;
    uint32_t bg_reserved;
};
static_assert(sizeof(Ext4GroupDesc) == 64, "Ext4GroupDesc 64-bit must be 64 bytes");

struct Ext4ExtentHeader {
    uint16_t eh_magic;      // 0xF30A
    uint16_t eh_entries;    // Number of valid entries
    uint16_t eh_max;        // Capacity of store
    uint16_t eh_depth;      // 0 for leaf, >0 for internal node
    uint32_t eh_generation; // Generation
};
static_assert(sizeof(Ext4ExtentHeader) == 12, "Ext4ExtentHeader must be 12 bytes");

struct Ext4Extent {
    uint32_t ee_block;      // First logical block extent covers
    uint16_t ee_len;        // Number of blocks covered
    uint16_t ee_start_hi;   // High 16 bits of physical block
    uint32_t ee_start_lo;   // Low 32 bits of physical block

    [[nodiscard]] uint64_t PhysicalBlock() const noexcept {
        return (static_cast<uint64_t>(ee_start_hi) << 32) | ee_start_lo;
    }

    void SetPhysicalBlock(uint64_t blk) noexcept {
        ee_start_lo = static_cast<uint32_t>(blk & 0xFFFFFFFF);
        ee_start_hi = static_cast<uint16_t>((blk >> 32) & 0xFFFF);
    }
};
static_assert(sizeof(Ext4Extent) == 12, "Ext4Extent must be 12 bytes");

struct Ext4ExtentIdx {
    uint32_t ei_block;      // Logical block covered
    uint32_t ei_leaf_lo;    // Child extent block physical lo
    uint16_t ei_leaf_hi;    // Child extent block physical hi
    uint16_t ei_unused;

    [[nodiscard]] uint64_t LeafBlock() const noexcept {
        return (static_cast<uint64_t>(ei_leaf_hi) << 32) | ei_leaf_lo;
    }
};
static_assert(sizeof(Ext4ExtentIdx) == 12, "Ext4ExtentIdx must be 12 bytes");

struct Ext4Inode {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size_lo;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks_lo;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint8_t  i_block[60];   // Extent tree header or direct block pointers
    uint32_t i_generation;
    uint32_t i_file_acl_lo;
    uint32_t i_size_high;
    uint32_t i_obso_faddr;
    uint16_t i_osd2_blocks_high;
    uint16_t i_osd2_file_acl_high;
    uint16_t i_osd2_uid_high;
    uint16_t i_osd2_gid_high;
    uint16_t i_checksum_lo;
    uint16_t i_reserved;

    // Extra fields if inode_size > 128
    uint16_t i_extra_isize;
    uint16_t i_checksum_hi;
    uint32_t i_ctime_extra;
    uint32_t i_mtime_extra;
    uint32_t i_atime_extra;
    uint32_t i_crtime;
    uint32_t i_crtime_extra;
    uint32_t i_version_hi;
    uint32_t i_projid;
    uint8_t  i_pad[96];

    [[nodiscard]] uint64_t FileSize() const noexcept {
        return (static_cast<uint64_t>(i_size_high) << 32) | i_size_lo;
    }

    void SetFileSize(uint64_t sz) noexcept {
        i_size_lo = static_cast<uint32_t>(sz & 0xFFFFFFFF);
        i_size_high = static_cast<uint32_t>((sz >> 32) & 0xFFFFFFFF);
    }

    [[nodiscard]] bool IsDirectory() const noexcept {
        return (i_mode & EXT4_S_IFMT) == EXT4_S_IFDIR;
    }

    [[nodiscard]] bool IsRegularFile() const noexcept {
        return (i_mode & EXT4_S_IFMT) == EXT4_S_IFREG;
    }

    [[nodiscard]] bool HasExtents() const noexcept {
        return (i_flags & EXT4_EXTENTS_FL) != 0;
    }
};
static_assert(sizeof(Ext4Inode) == 256, "Ext4Inode must be 256 bytes");

struct Ext4DirEntry2 {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[255];
};

#pragma pack(pop)

struct DirEntryInfo {
    uint32_t    inodeNumber{0};
    uint8_t     fileType{EXT4_FT_UNKNOWN};
    std::string name;
    uint64_t    size{0};
    uint16_t    mode{0};
    uint32_t    mtime{0};
};

struct FileExtentMapping {
    uint32_t logicalBlock{0};
    uint64_t physicalBlock{0};
    uint16_t blockCount{0};
};

} // namespace titanext
