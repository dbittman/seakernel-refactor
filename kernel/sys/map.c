#include <map.h>
#include <file.h>
#include <process.h>
#include <mmu.h>
#include <thread.h>
#include <map.h>
#include <errno.h>
#include <fs/sys.h>
#include <printk.h>

intptr_t sys_mmap(uintptr_t addr, size_t len, int prot, int flags, int fd, size_t off)
{
	if(len == 0
			|| (!(flags & MMAP_MAP_PRIVATE) && !(flags & MMAP_MAP_SHARED))
			|| ((flags & MMAP_MAP_PRIVATE) && (flags & MMAP_MAP_SHARED)))
		return -EINVAL;
	len = ((len - 1) & ~(arch_mm_page_size(0) - 1)) + arch_mm_page_size(0);
	if(len + addr > USER_MMAP_REGION_END)
		return -ENOMEM;
	if(addr >= USER_REGION_END || addr < USER_REGION_START) {
		if(flags & MMAP_MAP_FIXED)
			return -ENOMEM;
		addr = 0;
	}
	struct file *file = NULL;
	if(fd >= 0)
		file = process_get_file(fd);

	if(!file && !(flags & MMAP_MAP_ANON))
		return -EBADF;
	if(file && !file->ops->map) {
		kobj_putref(file);
		return -ENOTSUP;
	}
	if(file) {
		if((prot & PROT_WRITE) && !(file->flags & F_WRITE) && (flags & MMAP_MAP_SHARED)) {
			kobj_putref(file);
			return -EACCES;
		} else if(!(file->flags & F_READ)) {
			kobj_putref(file);
			return -EACCES;
		}
	}
	uintptr_t virt;
	if(addr) {
		virt = addr;
		if(virt >= USER_MMAP_REGION_START && virt < USER_MMAP_REGION_END) {
			if(virt + len > current_thread->process->next_mmap_reg)
				current_thread->process->next_mmap_reg = (virt + len + arch_mm_page_size(0)) & page_mask(0);
		}
	} else {
		virt = process_allocate_mmap_region(current_thread->process, len);
	}
	map_mmap(virt, file, prot, flags, len, off);

	if(file)
		kobj_putref(file);

	return virt;
}

sysret_t sys_munmap(void *addr, size_t len)
{
	map_unmap((uintptr_t)addr, len);
	return 0;
}

sysret_t sys_mprotect(void *addr, size_t len, int prot)
{
	return map_change_protect(current_thread->process, (uintptr_t)addr, len, prot);
}

/* TODO: I would like to not have this */
uintptr_t sys_brk(void *nb)
{
	uintptr_t new = (((uintptr_t)nb + arch_mm_page_size(0) - 1) & page_mask(0)) + arch_mm_page_size(0);
	if(new < USER_MAX_BRK && new >= USER_MIN_BRK) {
		if(new < current_thread->process->brk)
			return new;
		map_mmap(current_thread->process->brk, NULL, PROT_READ | PROT_WRITE, MMAP_MAP_PRIVATE | MMAP_MAP_ANON, new - current_thread->process->brk, 0);
		current_thread->process->brk = (uintptr_t)new;
		return new;
	} else {
		return current_thread->process->brk;
	}
}

void *sys_mremap(void *old, size_t oldsz, size_t newsz, int flags, void *new)
{
	int err;
	if(newsz == 0 || oldsz == 0 || ((uintptr_t)old != ((uintptr_t)old & page_mask(0))))
		return (void *)-EINVAL;
	if((flags & MREMAP_FIXED) && !(MREMAP_MAYMOVE))
		return (void *)-EINVAL;
	if(!(flags & MREMAP_FIXED))
		new = NULL;
	if(!(flags & MREMAP_MAYMOVE)) {
		new = old;
		err = mapping_try_expand((uintptr_t)old, oldsz, newsz);
	} else {
		if(new == NULL) {
			if(oldsz < newsz)
				new = (void *)process_allocate_mmap_region(current_thread->process, newsz);
			else
				new = old;
		}
		err = mapping_move((uintptr_t)old, oldsz, newsz, (uintptr_t)new);
	}

	if(err < 0)
		return (void *)(long)err;
	return new;
}

