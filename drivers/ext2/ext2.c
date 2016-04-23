#include "ext2.h"
#include <block.h>
#include <system.h>
#include <fs/path.h>
#include <printk.h>
#include <fs/inode.h>
#include <errno.h>
#include <fs/filesystem.h>

static struct kobj kobj_ext2 = KOBJ_DEFAULT(ext2);

static int ext2_read_blockdev(struct ext2 *fs, uint32_t fsblock, int count, uintptr_t phys, bool cache)
{
	unsigned long block = fsblock * ext2_sb_blocksize(&fs->superblock) / fs->bdev->drv->blksz;
	count *= ext2_sb_blocksize(&fs->superblock) / fs->bdev->drv->blksz;
	return block_read(fs->bdev, block, count, phys, cache);
}

static int ext2_write_blockdev(struct ext2 *fs, uint32_t fsblock, int count, uintptr_t phys)
{
	unsigned long block = fsblock * ext2_sb_blocksize(&fs->superblock) / fs->bdev->drv->blksz;
	count *= ext2_sb_blocksize(&fs->superblock) / fs->bdev->drv->blksz;
	return block_write(fs->bdev, block, count, phys);
}

static size_t ext2_read_data(struct ext2 *fs, size_t off, size_t len, void *buf)
{
	int offset = off % ext2_sb_blocksize(&fs->superblock);
	size_t al = offset+len < arch_mm_page_size(0) ? arch_mm_page_size(0) : __round_up_pow2(offset+len);
	uintptr_t phys = mm_physical_allocate(al, false);
	int count = al / ext2_sb_blocksize(&fs->superblock);
	uint32_t fsblock = off / ext2_sb_blocksize(&fs->superblock);

	ext2_read_blockdev(fs, fsblock, count, phys, true);

	memcpy(buf, (void *)(phys + PHYS_MAP_START + offset), len);
	mm_physical_deallocate(phys);
	return len;
}

static int ext2_get_indirection(struct ext2 *ext2, uint32_t blknum, uint32_t *direct, uint32_t *indirect)
{
	unsigned int num_ptrs = ext2_sb_blocksize(&ext2->superblock) / 4;
	if(blknum < 12) {
		*direct = blknum;
		*indirect = 0;
		return 0;
	} else if(blknum >= 12 && blknum < num_ptrs + 12) {
		*direct = 12;
		*indirect = blknum - 12;
		return 1;
	} else if(blknum < num_ptrs + 12 + num_ptrs * num_ptrs) {
		*direct = 13;
		*indirect = blknum - 12 - num_ptrs;
		return 2;
	} else {
		*direct = 14;
		*indirect = blknum - 12 - num_ptrs - num_ptrs * num_ptrs;
		return 3;
	}
}

static uint32_t ext2_inode_get_block(struct ext2 *ext2, struct ext2_inode *eno, uint32_t blknum)
{
	uint32_t direct, indirect;
	int level = ext2_get_indirection(ext2, blknum, &direct, &indirect);

	unsigned int blksz = ext2_sb_blocksize(&ext2->superblock);
	uintptr_t phys = mm_physical_allocate(blksz < arch_mm_page_size(0) ? arch_mm_page_size(0) : blksz, false);
	char *buf = (char *)(phys + PHYS_MAP_START);

	int num_ptrs = ext2_sb_blocksize(&ext2->superblock) / 4;
	uint32_t block = eno->blocks[direct];

	for(int i=0;i<level;i++) {
		if(block == 0)
			break;
		uint32_t div = 1;
		if(level - i == 3)
			div = num_ptrs * num_ptrs;
		else if(level - i == 2)
			div = num_ptrs;
		
		int entry = indirect / div;
		indirect %= div;
		
		ext2_read_blockdev(ext2, block, 1, phys, true);
		block = ((uint32_t *)buf)[entry];
	}

	mm_physical_deallocate(phys);
	return block;
}

int ext2_inode_get(struct ext2 *ext2, uint64_t extid, struct ext2_inode *eno)
{
	uint64_t intid = extid - 1;

	int groupnr = intid / ext2->superblock.inodes_per_group;
	
	struct ext2_blockgroup group;
	ext2_read_data(ext2, ext2->superblock.first_data_block + 1 + 
			(groupnr * sizeof(struct ext2_blockgroup)) / ext2_sb_blocksize(&ext2->superblock), sizeof(group), &group);

	int inosz = ext2_sb_inodesize(&ext2->superblock);
	ext2_read_data(ext2, group.inode_table + inosz * (intid % ext2->superblock.inodes_per_group),
			sizeof(struct ext2_inode), eno);

	return 0;
}















static int _read_page(struct inode *node, int pagenr, uintptr_t phys)
{
	struct ext2 *ext2 = node->fs->fsdata;
	int blocknr = pagenr * arch_mm_page_size(0) / ext2_sb_blocksize(&ext2->superblock);
	int count = arch_mm_page_size(0) / ext2_sb_blocksize(&ext2->superblock);

	struct ext2_inode eno;
	ext2_inode_get(ext2, node->id.inoid, &eno);

	uint32_t block = ext2_inode_get_block(ext2, &eno, blocknr);
	if(block == 0) {
		memset((void *)(phys + PHYS_MAP_START), 0, arch_mm_page_size(0));
	}
	ext2_read_blockdev(ext2, block, count, phys, false);
	return 0;
}

static int _lookup(struct inode *node, const char *name, size_t namelen, struct dirent *dirent)
{
	//struct ext2 *ext = node->fs->fsdata;
	uintptr_t phys = mm_physical_allocate(arch_mm_page_size(0), false);

	for(unsigned int page = 0;page < node->length / arch_mm_page_size(0);page++) {
		_read_page(node, page, phys);
		struct ext2_dirent *dir = (void *)(phys + PHYS_MAP_START);
		while((uintptr_t)dir < phys + PHYS_MAP_START + arch_mm_page_size(0)) {
			if(dir->inode > 0
					&& !strncmp(name, (char *)dir->name, namelen > dir->name_len ? dir->name_len : namelen)
					&& dir->name_len == namelen) {
				memcpy(dirent->name, dir->name, dir->name_len);
				dirent->namelen = dir->name_len;
				dirent->ino.inoid = dir->inode;
				dirent->ino.fsid = node->fs->id;
				mm_physical_deallocate(phys);
				return 0;
			}
			dir = (void *)((unsigned char *)dir + dir->record_len);
		}
	}
	mm_physical_deallocate(phys);
	return -ENOENT;
}

static size_t _getdents(struct inode *node, _Atomic size_t *start, struct gd_dirent *gd, size_t count)
{
	//struct ext2 *ext = node->fs->fsdata;
	uintptr_t phys = mm_physical_allocate(arch_mm_page_size(0), false);

	char *rec = (char *)gd;
	size_t read = 0, dirread = 0;
	size_t offset = *start % arch_mm_page_size(0);
	for(unsigned int page = *start / arch_mm_page_size(0);page < (node->length-1) / arch_mm_page_size(0) + 1;page++) {
		_read_page(node, page, phys);
		struct ext2_dirent *dir = (void *)(phys + PHYS_MAP_START + offset);
		while((uintptr_t)dir < phys + PHYS_MAP_START + arch_mm_page_size(0)) {
			if(dir->inode > 0) {
				int reclen = dir->name_len + sizeof(struct gd_dirent) + 1;
				reclen = (reclen & ~15) + 16;
				if(read + reclen >= count)
					goto out;
				struct gd_dirent *out = (void *)rec;
				out->d_type = dir->type;
				out->d_off = *start + reclen + read;
				out->d_ino = dir->inode;
				memcpy(out->d_name, dir->name, dir->name_len);
				out->d_name[dir->name_len] = 0;
				out->d_reclen = reclen;
				read += reclen;
			}
			dirread += dir->record_len;
			dir = (void *)((unsigned char *)dir + dir->record_len);
		}

		offset = 0;
	}
out:
	mm_physical_deallocate(phys);
	*start += dirread;
	return read;
}

static int _write_page(struct inode *node, int pagenr, uintptr_t phys)
{
	struct ext2 *ext2 = node->fs->fsdata;
	int blocknr = pagenr * arch_mm_page_size(0) / ext2_sb_blocksize(&ext2->superblock);
	int count = arch_mm_page_size(0) / ext2_sb_blocksize(&ext2->superblock);

	struct ext2_inode eno;
	ext2_inode_get(ext2, node->id.inoid, &eno);

	/* TODO: allocate... */
	uint32_t block = ext2_inode_get_block(ext2, &eno, blocknr);
	ext2_write_blockdev(ext2, block, count, phys);
	return 0;
}

static int _load_inode(struct filesystem *fs, uint64_t inoid, struct inode *node)
{
	struct ext2 *ext2 = fs->fsdata;
	struct ext2_inode eno;
	ext2_inode_get(ext2, inoid, &eno);

	node->uid = eno.uid;
	node->mode = eno.mode;
	node->links = eno.link_count;
	node->gid = eno.gid;
	node->atime = eno.access_time;
	node->mtime = eno.modification_time;
	node->ctime = eno.change_time;
	node->length = eno.size;
	
	return 0;
}

static int _link(struct inode *node, const char *name, size_t namelen, struct inode *target)
{
	(void)node;
	(void)name;
	(void)namelen;
	(void)target;
	return 0;
}


_Atomic int _next_id = 100000;
static int _alloc_inode(struct filesystem *fs, uint64_t *out_id)
{
	(void)fs;
	*out_id = _next_id++;
	return 0;
}

static struct inode_ops ext2_inode_ops = {
	.read_page = _read_page,
	.write_page = _write_page,
	.sync = NULL,
	.update = NULL,
	.lookup = _lookup,
	.link = _link,
	.getdents = _getdents,
};

static struct fs_ops ext2_fs_ops = {
	.load_inode = _load_inode,
	.alloc_inode = _alloc_inode,
};

struct fsdriver ext2fs = {
	.inode_ops = &ext2_inode_ops,
	.fs_ops = &ext2_fs_ops,
	.name = "ext2",
	.rootid = 2,
};




static void _late_init(void)
{
	printk("TESTING\n");
	struct inode *node;
	int err = fs_path_resolve("/dev/ada0", 0, 0, 0, 0, &node);
	if(err < 0) {
		panic(0, "no find");
	}
	struct blockdev *bd = blockdev_get(node->major, node->minor);
	assert(bd != NULL);

	struct ext2 *ext2 = kobj_allocate(&kobj_ext2);

	block_read(bd, 2, 1, (uintptr_t)&ext2->superblock - PHYS_MAP_START, sizeof(ext2->superblock));


	printk(":: %x\n", ext2->superblock.magic);


}

__initializer static void ext2_init(void)
{
	init_register_late_call(&_late_init, NULL);
	filesystem_register(&ext2fs);
}

