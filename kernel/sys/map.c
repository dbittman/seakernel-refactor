#include <map.h>
#include <file.h>
#include <process.h>
#include <mmu.h>
#include <thread.h>
#include <map.h>

intptr_t sys_mmap(uintptr_t addr, size_t len, int prot, int flags, int fd, size_t off)
{
	len = ((len - 1) & ~(arch_mm_page_size(0) - 1)) + arch_mm_page_size(0);
	struct file *file = NULL;
	if(fd >= 0)
		file = process_get_file(fd);
	struct inode *ino = file ? file_get_inode(file) : NULL;

	if(!ino && !(flags & MMAP_MAP_ANON))
		return -1;
	uintptr_t virt = addr ? addr : process_allocate_mmap_region(current_thread->process, len);
	map_mmap(virt, ino, prot, flags, len, off);

	inode_put(ino);

	return virt;
}
