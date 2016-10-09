#include <map.h>
#include <file.h>
#include <process.h>
#include <mmu.h>
#include <thread.h>
#include <map.h>
#include <errno.h>
#include <fs/sys.h>
#include <printk.h>
#include <system.h>
#include <frame.h>
static struct file *zero_file;

uintptr_t _zero_map(struct file *file, struct map_region *map, ptrdiff_t d)
{
	(void)file;
	(void)d;
	return frame_allocate(mm_get_pagelevel(map->psize), 0);
}

void _zero_unmap(struct file *file, struct map_region *map, ptrdiff_t d, uintptr_t phys)
{
	(void)file;
	(void)map;
	(void)d;
	frame_release(phys);
}

static struct file_calls zero_calls = {
	.read = 0,
	.write = 0,
	.create = 0, .destroy = 0, .ioctl = 0, .poll = 0, .open = 0, .close = 0,
	.map = _zero_map, .unmap = _zero_unmap,
};

__initializer void _init_zero(void)
{
	zero_file = kobj_allocate(&kobj_file);
	zero_file->ops = &zero_calls;
	zero_file->flags = F_WRITE | F_READ;
}

void map_mmap(struct process *proc, uintptr_t virt, size_t len, int prot, int flags, struct file *file, size_t off)
{
	/* the mapping system doesn't know about ANON, so instead
	 * give it a "fake" file. */
	if(flags & MMAP_MAP_ANON) {
		flags &= ~MMAP_MAP_ANON;
		file = zero_file;
	}
	map_region_setup(proc, virt, len, prot, flags, file, off / arch_mm_page_size(0), arch_mm_page_size(0), false);
	/* TODO: if virt, off, and len are aligned such that we can make use of larger pages, do it
	for(int i=MMU_NUM_PAGESIZE_LEVELS;i>=0;i--) {
		size_t ts = arch_mm_page_size(i);
		if(ts > len) continue;

		size_t thislen = len & ~(ts-1);
		assert(thislen > 0);
		map_region_setup(proc, virt, thislen, prot, flags, file, off / arch_mm_page_size(0), false);
		off += thislen;
		virt += thislen;
	}
	*/
}

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
	map_mmap(current_thread->process, virt, len, prot, flags, file, off);

	if(file)
		kobj_putref(file);

	return virt;
}

sysret_t sys_munmap(void *addr, size_t len)
{
	(void)addr;
	(void)len;
	/* TODO: something weird happens here
	 * where the library calls with with a very large 'len'. figure out why */
	//map_region_remove(current_thread->process, (uintptr_t)addr, len, false);
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
		sys_mmap(current_thread->process->brk, new - current_thread->process->brk, PROT_READ | PROT_WRITE, MMAP_MAP_PRIVATE | MMAP_MAP_ANON, -1, 0);
		current_thread->process->brk = (uintptr_t)new;
		return new;
	} else {
		return current_thread->process->brk;
	}
}

void *sys_mremap(void *old, size_t oldsz, size_t newsz, int flags, void *new)
{
	int err;
	if(newsz == 0 || oldsz == 0 || ((uintptr_t)old != ((uintptr_t)old & page_mask(0))) || ((uintptr_t)new != ((uintptr_t)new & page_mask(0))))
		return (void *)-EINVAL;
	if((flags & MREMAP_FIXED) && !(MREMAP_MAYMOVE))
		return (void *)-EINVAL;
	if(!(flags & MREMAP_FIXED))
		new = NULL;
	if(!(flags & MREMAP_MAYMOVE) || oldsz > newsz) {
		new = old;
		err = mapping_resize(current_thread->process, (uintptr_t)old, oldsz, newsz);
	} else {
		/* try to resize first to save on faults */
		if((err=mapping_resize(current_thread->process, (uintptr_t)old, oldsz, newsz)) < 0) {
			if(new == NULL) {
				new = (void *)process_allocate_mmap_region(current_thread->process, newsz);
			}
			err = mapping_move(current_thread->process, (uintptr_t)old,
					oldsz, newsz, (uintptr_t)new);
		} else {
			new = old;
		}
	}
	if(err < 0)
		return (void *)(long)err;
	return new;
}

