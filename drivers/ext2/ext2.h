#pragma once

struct ext2_superblock {
	uint32_t inode_count;
	uint32_t block_count;
	uint32_t reserved_blocks;
	uint32_t free_blocks;
	uint32_t free_inodes;
	uint32_t first_data_block;
	uint32_t block_size;
	int32_t fragment_size;
	uint32_t blocks_per_group;
	uint32_t fragments_per_group;
	uint32_t inodes_per_group;
	uint32_t mount_time;
	uint32_t write_time;
	uint16_t mount_count;
	int16_t max_mount_count;
	uint16_t magic;
	uint16_t state;
	uint16_t errors;
	uint16_t minor_revision;
	uint32_t last_check_time;
	uint32_t check_interval;
	uint32_t creator_os;
	uint32_t revision;
	uint16_t default_res_uid;
	uint16_t default_res_gid;
	uint32_t first_inode;
	uint16_t inode_size;
	uint16_t blockgroup_num;
	uint32_t features_opt;
	uint32_t features_req;
	uint32_t features_ro;
	uint32_t volume_id[4];
	char volume_name[16];
	char last_mounted[64];
	uint32_t algo_bitmap;
	uint32_t reserved[205];
} __attribute__((packed));






struct ext2_inode {
	uint16_t mode;
	uint16_t uid;
	uint32_t size;
	uint32_t access_time;
	uint32_t change_time;
	uint32_t modification_time;
	uint32_t deletion_time;
	uint16_t gid;
	uint16_t link_count;
	uint32_t sector_count;
	uint32_t flags;
	uint32_t os1;
	uint32_t blocks[15];
	uint32_t gen_num;
	uint32_t file_acl;
	uint32_t size_up;
	unsigned char pad[20];
} __attribute__((packed));

struct ext2_dirent {
	uint32_t inode;
	uint16_t record_len;
	unsigned char name_len;
	unsigned char type;
	unsigned char name[];
} __attribute__((packed));

#define DET_UNKNOWN 0
#define DET_REG 1
#define DET_DIR 2
#define DET_CHAR 3
#define DET_BLOCK 4
#define DET_FIFO 5
#define DET_SOCK 6
#define DET_SLINK 7

struct ext2_blockgroup {
	uint32_t block_bitmap;
	uint32_t inode_bitmap;
	uint32_t inode_table;
	uint16_t free_blocks;
	uint16_t free_inodes;
	uint16_t used_directories;
	uint16_t padding;
	uint32_t reserved[3];
} __attribute__((packed));












#define EXT2_SB_MAGIC 0xEF53
struct blockdev;
struct ext2 {
	struct ext2_superblock superblock;
	struct blockdev *bdev;
};














static inline size_t ext2_sb_blocksize(struct ext2_superblock *sb) {
	return 1024 << sb->block_size;
}

static inline size_t ext2_sb_bgcount(struct ext2_superblock *sb) {
	return (sb->block_count - sb->first_data_block +
			(sb->blocks_per_group - 1)) / sb->blocks_per_group;
}

static inline size_t ext2_sb_inodesize(struct ext2_superblock *sb) {
	if (sb->revision == 0) {
		return 128;
	} else {
		return sb->inode_size;
	}
}

