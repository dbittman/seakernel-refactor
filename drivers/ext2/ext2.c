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

static size_t ext2_write_data(struct ext2 *fs, size_t off, size_t len, void *buf)
{
	int offset = off % ext2_sb_blocksize(&fs->superblock);
	size_t al = offset+len < arch_mm_page_size(0) ? arch_mm_page_size(0) : __round_up_pow2(offset+len);
	uintptr_t phys = mm_physical_allocate(al, false);
	int count = al / ext2_sb_blocksize(&fs->superblock);
	uint32_t fsblock = off / ext2_sb_blocksize(&fs->superblock);

	ext2_read_blockdev(fs, fsblock, count, phys, true);

	memcpy((void *)(phys + PHYS_MAP_START + offset), buf, len);
	ext2_write_blockdev(fs, fsblock, count, phys);
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
	ext2_read_data(ext2, (ext2->superblock.first_data_block + 1) * ext2_sb_blocksize(&ext2->superblock) + 
			(groupnr * sizeof(struct ext2_blockgroup)), sizeof(group), &group);

	int inosz = ext2_sb_inodesize(&ext2->superblock);
	ext2_read_data(ext2, group.inode_table * ext2_sb_blocksize(&ext2->superblock) + inosz * (intid % ext2->superblock.inodes_per_group),
			sizeof(struct ext2_inode), eno);

	return 0;
}

int ext2_inode_write(struct ext2 *ext2, uint64_t extid, struct ext2_inode *eno)
{
	uint64_t intid = extid - 1;

	int groupnr = intid / ext2->superblock.inodes_per_group;
	
	struct ext2_blockgroup group;
	ext2_read_data(ext2, (ext2->superblock.first_data_block + 1) * ext2_sb_blocksize(&ext2->superblock) + 
			(groupnr * sizeof(struct ext2_blockgroup)), sizeof(group), &group);

	int inosz = ext2_sb_inodesize(&ext2->superblock);
	ext2_write_data(ext2, group.inode_table * ext2_sb_blocksize(&ext2->superblock) + inosz * (intid % ext2->superblock.inodes_per_group),
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

static int _readlink(struct inode *node, char *path, size_t len)
{
	struct ext2 *ext2 = node->fs->fsdata;
	struct ext2_inode eno;
	ext2_inode_get(ext2, node->id.inoid, &eno);
	if(len <= eno.size)
		return -ERANGE;
	if(eno.size < 60) {
		/* data is stored in the inode blocks */
		memcpy(path, &eno.blocks, eno.size);
	} else {
		struct inodepage *inopage = inode_get_page(node, 0);
		memcpy(path, (void *)(inopage->frame + PHYS_MAP_START), eno.size);
		inode_release_page(node, inopage);
	}
	path[eno.size] = 0;
	return 0;
}

static int _writelink(struct inode *node, const char *path)
{
	struct ext2 *ext2 = node->fs->fsdata;
	struct ext2_inode eno;
	ext2_inode_get(ext2, node->id.inoid, &eno);
	if(strlen(path) < 60) {
		memcpy(&eno.blocks, path, strlen(path));
	} else {
		struct inodepage *inopage = inode_get_page(node, 0);
		memcpy((void *)(inopage->frame + PHYS_MAP_START), path, strlen(path));
		inopage->flags |= INODEPAGE_DIRTY;
		inode_release_page(node, inopage);
	}
	node->length = eno.size = strlen(path);
	ext2_inode_write(ext2, node->id.inoid, &eno);
	return 0;
}

static int _lookup(struct inode *node, const char *name, size_t namelen, struct dirent *dirent)
{
	size_t dirread = 0;
	if(!node->length)
		return -ENOENT;
	for(unsigned int page = 0;page < (node->length-1) / arch_mm_page_size(0) + 1;page++) {
		struct inodepage *inopage = inode_get_page(node, page);
		struct ext2_dirent *dir = (void *)(inopage->frame + PHYS_MAP_START);
		size_t pagelen = arch_mm_page_size(0);
		while((uintptr_t)dir < inopage->frame + PHYS_MAP_START + pagelen && dirread < node->length) {
			if(dir->record_len == 0) {
				inode_release_page(node, inopage);
				return -ENOENT;
			}
			if(dir->inode > 0
					&& !strncmp(name, (char *)dir->name, namelen > dir->name_len ? dir->name_len : namelen)
					&& dir->name_len == namelen) {
				if(dirent) {
					memcpy(dirent->name, dir->name, dir->name_len);
					dirent->namelen = dir->name_len;
					dirent->ino.inoid = dir->inode;
					dirent->ino.fsid = node->fs->id;
				}
				inode_release_page(node, inopage);
				return 0;
			}
			dirread += dir->record_len;
			dir = (void *)((unsigned char *)dir + dir->record_len);
		}
		inode_release_page(node, inopage);
	}
	return -ENOENT;
}

static size_t _getdents(struct inode *node, _Atomic size_t *start, struct gd_dirent *gd, size_t count)
{
	//struct ext2 *ext = node->fs->fsdata;
	if(*start >= node->length)
		return 0;

	char *rec = (char *)gd;
	size_t read = 0, dirread = 0;
	size_t offset = *start % arch_mm_page_size(0);
	for(unsigned int page = *start / arch_mm_page_size(0);page < (node->length-1) / arch_mm_page_size(0) + 1;page++) {
		struct inodepage *inopage = inode_get_page(node, page);
		struct ext2_dirent *dir = (void *)(inopage->frame + PHYS_MAP_START + offset);
		while((uintptr_t)dir < inopage->frame + PHYS_MAP_START + arch_mm_page_size(0) && dirread < node->length) {
			if(dir->record_len == 0) {
				inode_release_page(node, inopage);
				goto out;
			}
			if(dir->inode > 0) {
				int reclen = dir->name_len + sizeof(struct gd_dirent) + 1;
				reclen = (reclen & ~15) + 16;
				if(read + reclen >= count) {
					inode_release_page(node, inopage);
					goto out;
				}
				struct gd_dirent *out = (void *)(rec + read);
				out->d_type = dir->type;
				out->d_off = *start + reclen + read;
				out->d_ino = dir->inode;
				memcpy(out->d_name, dir->name, dir->name_len);
				out->d_name[dir->name_len] = 0;
				out->d_reclen = reclen;
				read += reclen;
				inopage->flags |= INODEPAGE_DIRTY;
			}
			dirread += dir->record_len;
			dir = (void *)((unsigned char *)dir + dir->record_len);
		}

		inode_release_page(node, inopage);
		offset = 0;
	}
out:
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

static int _get_dirent_type(struct inode *node)
{
	if(S_ISLNK(node->mode)) {
		return DET_SLINK;
	} else if(S_ISFIFO(node->mode)) {
		return DET_FIFO;
	} else if(S_ISDIR(node->mode)) {
		return DET_DIR;
	} else if(S_ISCHR(node->mode)) {
		return DET_CHAR;
	} else if(S_ISBLK(node->mode)) {
		return DET_BLOCK;
	} else if(S_ISSOCK(node->mode)) {
		return DET_SOCK;
	} else if(S_ISREG(node->mode)) {
		return DET_REG;
	} else {
		return DET_UNKNOWN;
	}
}

static int _link(struct inode *node, const char *name, size_t namelen, struct inode *target)
{
	struct ext2 *ext2 = node->fs->fsdata;
	size_t dirread = 0;
	for(unsigned int page = 0;;page++) {
		struct inodepage *inopage = inode_get_page(node, page);
		struct ext2_dirent *dir = (void *)(inopage->frame + PHYS_MAP_START);
		size_t pagelen = arch_mm_page_size(0);
		size_t thispageread = 0;
		while((uintptr_t)dir < inopage->frame + PHYS_MAP_START + pagelen) {
			if(dir->inode == 0) {
				if(dir->name_len >= namelen) {
					memcpy(dir->name, name, namelen);
					dir->name_len = namelen;
					dir->inode = target->id.inoid;
					dir->type = _get_dirent_type(target);
					inopage->flags |= INODEPAGE_DIRTY;
					inode_release_page(node, inopage);
					return 0;
				}
				else if(dir->record_len == 0) {
					assert(thispageread == 0);
					dir->record_len = ext2_sb_blocksize(&ext2->superblock);
					dir->name_len = namelen;
					memcpy(dir->name, name, namelen);
					dir->inode = target->id.inoid;
					dir->type = _get_dirent_type(target);
					assert(((uintptr_t)dir - (inopage->frame + PHYS_MAP_START)) + dir->record_len == ext2_sb_blocksize(&ext2->superblock));
					inopage->flags |= INODEPAGE_DIRTY;
					inode_release_page(node, inopage);
					if(node->length < dir->record_len + dirread)
						node->length = dir->record_len + dirread;
					inode_mark_dirty(node);
					return 0;
				}
			} else if (dir->record_len >= (dir->name_len + sizeof(*dir)*2 + 8 + namelen) && 0) {
				size_t oldlen = dir->record_len;
				dir->record_len = (dir->name_len + sizeof(*dir) + 4) & ~3;
				struct ext2_dirent *nd = (void *)((char *)dir + dir->record_len);
				nd->type = _get_dirent_type(target);
				memcpy(nd->name, name, namelen);
				nd->name_len = namelen;
				nd->record_len = oldlen - dir->record_len;
				nd->inode = target->id.inoid;
				inopage->flags |= INODEPAGE_DIRTY;
				inode_release_page(node, inopage);
				//assert(((uintptr_t)nd - (inopage->frame + PHYS_MAP_START)) + nd->record_len == ext2_sb_blocksize(&ext2->superblock));
				return 0;
			}
			dirread += dir->record_len;
			thispageread += dir->record_len;
			dir = (void *)((unsigned char *)dir + dir->record_len);
		}
		inode_release_page(node, inopage);
	}
}

static int _unlink(struct inode *node, const char *name, size_t namelen)
{
	size_t dirread = 0;
	for(unsigned int page = 0;page < (node->length-1) / arch_mm_page_size(0) + 1;page++) {
		struct inodepage *inopage = inode_get_page(node, page);
		struct ext2_dirent *dir = (void *)(inopage->frame + PHYS_MAP_START);
		size_t pagelen = arch_mm_page_size(0);
		while((uintptr_t)dir < inopage->frame + PHYS_MAP_START + pagelen && dirread < node->length) {
			if(dir->record_len == 0) {
				inode_release_page(node, inopage);
				return -ENOENT;
			}
			if(dir->inode > 0
					&& !strncmp(name, (char *)dir->name, namelen > dir->name_len ? dir->name_len : namelen)
					&& dir->name_len == namelen) {
				dir->inode = 0;
				inopage->flags |= INODEPAGE_DIRTY;
				inode_release_page(node, inopage);
				return 0;
			}
			dirread += dir->record_len;
			dir = (void *)((unsigned char *)dir + dir->record_len);
		}
		inode_release_page(node, inopage);
	}
	return -ENOENT;
}

static uint32_t __allocate_inode_from_group(struct ext2 *ext2, int gid, struct ext2_blockgroup *group)
{
	uintptr_t phys = mm_physical_allocate(ext2_sb_blocksize(&ext2->superblock), false);
	ext2_read_blockdev(ext2, group->inode_bitmap, 1, phys, true);
	char *bitmap = (char *)(phys + PHYS_MAP_START);
	for(unsigned i=0;i<ext2->superblock.inodes_per_group;i++) {
		int offset = i / 8;
		int bit = i % 8;
		if(!(bitmap[offset] & (1 << bit))) {
			bitmap[offset] |= 1 << bit;
			ext2_write_blockdev(ext2, group->inode_bitmap, 1, phys);
			group->free_inodes--;
			ext2_write_data(ext2, (ext2->superblock.first_data_block + 1) * ext2_sb_blocksize(&ext2->superblock) + 
					(gid * sizeof(struct ext2_blockgroup)), sizeof(*group), group);
			mm_physical_deallocate(phys);
			return i;
		}
	}
	mm_physical_deallocate(phys);
	return 0;
}

static int _alloc_inode(struct filesystem *fs, uint64_t *out_id)
{
	struct ext2 *ext2 = fs->fsdata;
	for(unsigned i=0;i<ext2_sb_bgcount(&ext2->superblock);i++) {
		struct ext2_blockgroup group;
		ext2_read_data(ext2, (ext2->superblock.first_data_block + 1) * ext2_sb_blocksize(&ext2->superblock) + 
				(i * sizeof(struct ext2_blockgroup)), sizeof(group), &group);
		if(group.free_inodes > 0) {
			*out_id = (__allocate_inode_from_group(ext2, i, &group) + i * ext2->superblock.inodes_per_group) + 1;
			return *out_id == 0 ? -EIO : 0;
		}
	}

	return -ENOSPC;
}

static void _release_inode(struct filesystem *fs, struct inode *node)
{
	struct ext2 *ext2 = fs->fsdata;
	uint32_t id = node->id.inoid - 1;
	int gid = id / ext2->superblock.inodes_per_group;
	int inoentry = id % ext2->superblock.inodes_per_group;

	struct ext2_blockgroup group;
	ext2_read_data(ext2, (ext2->superblock.first_data_block + 1) * ext2_sb_blocksize(&ext2->superblock) + 
			(gid * sizeof(struct ext2_blockgroup)), sizeof(group), &group);
	group.free_inodes++;

	uintptr_t phys = mm_physical_allocate(ext2_sb_blocksize(&ext2->superblock), false);
	ext2_read_blockdev(ext2, group.inode_bitmap, 1, phys, true);
	char *bitmap = (char *)(phys + PHYS_MAP_START);
	bitmap[inoentry / 8] &= ~(1 << (inoentry % 8));
	ext2_write_blockdev(ext2, group.inode_bitmap, 1, phys);
	ext2_write_data(ext2, (ext2->superblock.first_data_block + 1) * ext2_sb_blocksize(&ext2->superblock) + 
			(gid * sizeof(struct ext2_blockgroup)), sizeof(group), &group);
	mm_physical_deallocate(phys);
}

static int _mount(struct filesystem *fs, struct blockdev *bd, unsigned long flags)
{
	(void)flags;
	struct ext2 *ext2 = fs->fsdata = kobj_allocate(&kobj_ext2);
	ext2->bdev = kobj_getref(bd);
	if(block_read(bd, 2, 1, (uintptr_t)&ext2->superblock - PHYS_MAP_START, sizeof(ext2->superblock)) != 1) {
		kobj_putref(bd);
		kobj_putref(ext2);
		return -EIO;
	}
	printk("mounting ext2 fs: blksz %ld, ino/grp %d\n", ext2_sb_blocksize(&ext2->superblock), ext2->superblock.inodes_per_group);
	return 0;
}

static struct inode_ops ext2_inode_ops = {
	.read_page = _read_page,
	.write_page = _write_page,
	.sync = NULL,
	.update = NULL,
	.lookup = _lookup,
	.link = _link,
	.unlink = _unlink,
	.getdents = _getdents,
	.readlink = _readlink,
	.writelink = _writelink,
};

static struct fs_ops ext2_fs_ops = {
	.load_inode = _load_inode,
	.alloc_inode = _alloc_inode,
	.update_inode = 0,
	.unmount = 0,
	.mount = _mount,
	.release_inode = _release_inode,
};

struct fsdriver ext2fs = {
	.inode_ops = &ext2_inode_ops,
	.fs_ops = &ext2_fs_ops,
	.name = "ext2",
	.rootid = 2,
};

__initializer static void ext2_init(void)
{
	filesystem_register(&ext2fs);
}

