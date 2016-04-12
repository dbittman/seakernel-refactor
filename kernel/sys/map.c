#include <map.h>
#include <file.h>
#include <process.h>
#include <mmu.h>
#include <thread.h>
#include <map.h>
#include <errno.h>

intptr_t sys_mmap(uintptr_t addr, size_t len, int prot, int flags, int fd, size_t off)
{
	if(len == 0 || (!(flags & MMAP_MAP_PRIVATE) && !(flags & MMAP_MAP_SHARED)))
		return -EINVAL;
	len = ((len - 1) & ~(arch_mm_page_size(0) - 1)) + arch_mm_page_size(0);
	if(len + addr > (USER_MMAP_REGION_END - USER_MMAP_REGION_START))
		return -ENOMEM;
	if(addr >= USER_REGION_END || addr < USER_REGION_START) {
		if(flags & MMAP_MAP_FIXED)
			return -ENOMEM;
		addr = 0;
	}
	struct file *file = NULL;
	if(fd >= 0)
		file = process_get_file(fd);
	struct inode *ino = file ? file_get_inode(file) : NULL;
	if(file)
		kobj_putref(file);

	if(!ino && !(flags & MMAP_MAP_ANON))
		return -EBADF;
	uintptr_t virt = addr ? addr : process_allocate_mmap_region(current_thread->process, len);
	map_mmap(virt, ino, prot, flags, len, off);

	if(ino)
		inode_put(ino);

	return virt;
}
