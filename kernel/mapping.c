#include <map.h>
#include <frame.h>
#include <file.h>
#include <thread.h>
#include <process.h>
#include <assert.h>
#include <printk.h>
#include <string.h>
#include <errno.h>
#include <fs/sys.h>

static void _map_region_init(void *obj)
{
	struct map_region *reg = obj;
	reg->file = NULL;
	reg->flags = 0;
}

static void _map_region_create(void *obj)
{
	_map_region_init(obj);
}

struct kobj kobj_map_region = {
	KOBJ_DEFAULT_ELEM(map_region),
	.create = _map_region_create,
	.init = _map_region_init,
	.put = NULL, .destroy = NULL,
};

void map_region_remove(uintptr_t start, size_t len, bool locked)
{
	(void)start;
	(void)len;
	(void)locked;
}

void map_region_setup(struct process *proc, uintptr_t start, size_t len, int prot, int flags, struct file *file, int nodepage, size_t psize, bool locked)
{
	struct map_region *reg = kobj_allocate(&kobj_map_region);
	reg->prot = prot;
	reg->flags = flags;
	reg->file = kobj_getref(file);
	reg->nodepage = nodepage;
	reg->start = start;
	reg->length = len;
	int pl = mm_get_pagelevel(psize);
	reg->psize = psize;

	if(!locked)
		mutex_acquire(&proc->map_lock);
	/* previous mappings are removed */
	map_region_remove(start, len, true);
	linkedlist_insert(&proc->maps[pl], &reg->entry, reg);
	if(!locked)
		mutex_release(&proc->map_lock);
}

static void __mapping_remove(struct process *proc, struct map_region *reg)
{
	linkedlist_remove(&proc->maps[mm_get_pagelevel(reg->psize)], &reg->entry);
	for(uintptr_t v = reg->start;v < reg->start + reg->length;v += reg->psize) {
		uintptr_t phys = arch_mm_virtual_unmap(proc->ctx, v);
		if(phys != 0) {
			if(reg->file->ops->unmap)
				reg->file->ops->unmap(reg->file, reg, v - reg->start, phys);
		}
	}
	kobj_putref(reg->file);
	kobj_putref(reg);
}

void process_remove_mappings(struct process *proc, bool user_tls_too)
{
	mutex_acquire(&proc->map_lock);
	for(int i=0;i<MMU_NUM_PAGESIZE_LEVELS;i++) {
		struct linkedentry *next;
		for(struct linkedentry *entry = linkedlist_iter_start(&proc->maps[i]);
				entry != linkedlist_iter_end(&proc->maps[i]);
				entry = next) {
			struct map_region *reg = linkedentry_obj(entry);
			next = linkedlist_iter_next(entry);

			if(!user_tls_too && reg->start >= USER_TLS_REGION_START
					&& reg->start < USER_TLS_REGION_END)
				continue;
			__mapping_remove(proc, reg);
		}
	}
	mutex_release(&proc->map_lock);
}

void process_copy_mappings(struct process *from, struct process *to)
{
	mutex_acquire(&from->map_lock);
	for(int i=0;i<MMU_NUM_PAGESIZE_LEVELS;i++) {
		for(struct linkedentry *entry = linkedlist_iter_start(&from->maps[i]);
				entry != linkedlist_iter_end(&from->maps[i]);
				entry = linkedlist_iter_next(entry)) {
			struct map_region *reg = linkedentry_obj(entry);
			map_region_setup(to, reg->start, reg->length, reg->prot,
					reg->flags, reg->file, reg->nodepage, reg->psize, true);

			for(uintptr_t addr = reg->start; addr < reg->start + reg->length;addr += reg->psize) {
				uintptr_t phys;
				int flags;
				if(arch_mm_virtual_getmap(from->ctx, addr, &phys, &flags, NULL)) {
					/* allow for copy-on-write */
					if(!(reg->flags & MMAP_MAP_SHARED)) {
						flags &= ~MAP_WRITE;
						arch_mm_virtual_chattr(from->ctx, addr, flags);
					}
					frame_acquire(phys);
					arch_mm_virtual_map(to->ctx, addr, phys, reg->psize, flags);
				}
			}
		}
	}
	mutex_release(&from->map_lock);
}

static struct map_region *__split_region(struct process *proc,
		struct map_region *old, uintptr_t split)
{
	size_t oldlen = old->length;
	split = ((split - 1) & ~(old->psize - 1)) + old->psize;
	size_t newlen = split - old->start;
	struct map_region *reg = kobj_allocate(&kobj_map_region);
	reg->length = old->length - newlen;
	old->length = newlen;
	
	reg->prot = old->prot;
	reg->flags = old->flags;
	reg->file = kobj_getref(old->file);
	reg->nodepage = old->nodepage;
	reg->start = split;
	reg->psize = old->psize;

	assert(reg->length + old->length == oldlen);
	linkedlist_insert(&proc->maps[mm_get_pagelevel(old->psize)], &reg->entry, reg);

	return reg;
}

static struct map_region *__find_region(struct process *proc, uintptr_t addr)
{
	for(int i=MMU_NUM_PAGESIZE_LEVELS-1;i>=0;i--) {
		for(struct linkedentry *entry = linkedlist_iter_start(&proc->maps[i]);
				entry != linkedlist_iter_end(&proc->maps[i]);
				entry = linkedlist_iter_next(entry)) {
			struct map_region *reg = linkedentry_obj(entry);
			if(addr >= reg->start && addr < (reg->start + reg->length)) {
				return reg;
			}
		}
	}
	return NULL;
}

int mapping_resize(struct process *proc, uintptr_t virt, size_t oldlen, size_t newlen)
{
	if(oldlen == newlen)
		return 0;
	mutex_acquire(&proc->map_lock);
	struct map_region *reg = __find_region(proc, virt);
	if(!reg) {
		mutex_release(&proc->map_lock);
		return -EFAULT;
	}
	/* ensure that the whole region is mapped */
	if(reg->start != virt || reg->length < oldlen) {
		mutex_release(&proc->map_lock);
		return -EFAULT;
	}
	if(oldlen > newlen) {
		struct map_region *split = __split_region(proc, reg, virt + oldlen);
		__mapping_remove(proc, split);
	} else {
		if(reg->length < newlen) {
			/* try to resize to be bigger */
			for(uintptr_t tmp = ((reg->start + reg->length - 1) & page_mask(0)) + arch_mm_page_size(0); tmp < virt + newlen; tmp += arch_mm_page_size(0)) {
				if(__find_region(proc, tmp)) {
					mutex_release(&proc->map_lock);
					return -ENOMEM;
				}
			}

			reg->length = newlen;
			if(newlen > oldlen) {
				atomic_fetch_add(&proc->next_mmap_reg,
						(((newlen - oldlen) & page_mask(0)) + arch_mm_page_size(0)));
			}
		}
	}
	mutex_release(&proc->map_lock);
	return 0;
}

int mapping_move(struct process *proc, uintptr_t old, size_t oldlen, size_t newlen, uintptr_t new)
{
	mutex_acquire(&proc->map_lock);
	struct map_region *regold = __find_region(proc, old);
	if(!regold) {
		mutex_release(&proc->map_lock);
		return -EFAULT;
	}
	map_region_setup(proc, new, newlen, regold->prot, regold->flags,
			regold->file, regold->nodepage, regold->psize, true);
	/* remap everything */
	uintptr_t dest = new;
	uintptr_t tmp = old;
	for(;tmp < old + oldlen;tmp += regold->psize, dest += regold->psize) {
		uintptr_t phys;
		int flags;
		if(arch_mm_virtual_getmap(proc->ctx, tmp, &phys, &flags, NULL)) {
			arch_mm_virtual_map(proc->ctx, dest, phys, regold->psize, flags);
			arch_mm_virtual_unmap(proc->ctx, tmp);
		}
	}
	__mapping_remove(proc, regold);
	
	mutex_release(&proc->map_lock);
	return 0;
}

int map_change_protect(struct process *proc, uintptr_t virt, size_t len, int prot)
{
	mutex_acquire(&proc->map_lock);
	for(uintptr_t start = virt;start < virt + len;start += arch_mm_page_size(0)) {
		struct map_region *reg = __find_region(proc, start);
		if(!reg)
			continue;

		if((prot & PROT_WRITE) && !(reg->file->flags & F_WRITE)) {
			mutex_release(&proc->map_lock);
			return -EACCES;
		}
		if(start != reg->start) {
			reg = __split_region(proc, reg, start);
		}
		size_t rem = len - (start - virt);
		if(rem < reg->length) {
			__split_region(proc, reg, start + rem);
		}
		reg->prot = prot;

		/* remap with new protection, if the mapping is already
		 * established */
		int set = MAP_PRIVATE | MAP_USER | MAP_ACCESSED;
		if(reg->prot & PROT_WRITE)
			set |= MAP_WRITE;
		if(reg->prot & PROT_EXEC)
			set |= MAP_EXECUTE;
		arch_mm_virtual_chattr(proc->ctx, start & page_mask(0), set);
		start += reg->length - arch_mm_page_size(0);
	}
	mutex_release(&proc->map_lock);
	return 0;
}

uintptr_t __get_phys_to_map(struct map_region *reg, uintptr_t v)
{
	return reg->file->ops->map(reg->file, reg, v - reg->start);
}

int mmu_mappings_handle_fault(struct process *proc, uintptr_t addr, int flags)
{
	/*
	 * get region.
	 * if no region, fail.
	 * check write perms.
	 * if fault_present:
	 *     read mapping
	 *     if mapped, succeed.
	 *     get frame or page
	 *     if private and not anon, clear write perm when mapping
	 *     map, succeed.
	 * elif fault_perm:
	 *     exec -> fail.
	 *     if fault_write and private:
	 *         if anon:
	 *             if count > 1, copy to new frame, dec count, else, mark writable
	 *         else:
	 *             copy data to new frame, mark as anon, call unmap callback
	 *     elif fault_write and !anon and shared:
	 *         mark writable
	 */
	bool success = false;
	mutex_acquire(&proc->map_lock);
	struct map_region *reg = __find_region(proc, addr);
	if(!reg)
		goto out;
	if((flags & FAULT_WRITE) && !(reg->prot & PROT_WRITE)) {
		goto out;
	}
	
	int set = MAP_PRIVATE | MAP_USER | MAP_ACCESSED;
	if(reg->prot & PROT_WRITE)
		set |= MAP_WRITE;
	if(reg->prot & PROT_EXEC)
		set |= MAP_EXECUTE;

	uintptr_t v = addr & ~(reg->psize - 1);
	if(flags & FAULT_ERROR_PRES) {
		if(arch_mm_virtual_getmap(proc->ctx, v, NULL, NULL, NULL)) {
			/* another thread may have gotten here first */
			success = true;
			goto next;
		}

		uintptr_t phys = __get_phys_to_map(reg, v);
		struct frame *frame = frame_get_from_address(phys);
		if((reg->flags & MMAP_MAP_PRIVATE) && ((frame->flags & FRAME_PERSIST) || frame->count > 1))
			set &= ~MAP_WRITE;

		arch_mm_virtual_map(proc->ctx, v, phys, reg->psize, set);
	}
next:
	/* this could have been cleared above */
	if(reg->prot & PROT_WRITE)
		set |= MAP_WRITE;
	/* do this separately so that we can handle both PERM and PRES at once, since this function
	 * can be called by normal kernel code, not just the fault handler. */
	if(flags & FAULT_ERROR_PERM) {
		if(flags & FAULT_EXEC) {
			goto out;
		}

		if((flags & FAULT_WRITE) && (reg->flags & MMAP_MAP_PRIVATE)) {
			uintptr_t phys;
			bool r = arch_mm_virtual_getmap(proc->ctx, v, &phys, NULL, NULL);
			assert(r);

			uintptr_t newframe = frame_allocate(0, 0);
			memcpy((void *)(newframe + PHYS_MAP_START),
					(void *)(phys + PHYS_MAP_START), reg->psize);

			arch_mm_virtual_unmap(proc->ctx, v);
			arch_mm_virtual_map(proc->ctx, v, newframe, reg->psize, set);
			frame_release(phys);
		} else if((flags & FAULT_WRITE) && (reg->flags & MMAP_MAP_SHARED)) {
			uintptr_t phys;
			bool r = arch_mm_virtual_getmap(proc->ctx, v, &phys, NULL, NULL);
			assert(r);
			struct frame *frame = frame_get_from_address(phys);
			frame->flags |= FRAME_DIRTY;
			arch_mm_virtual_chattr(proc->ctx, v, set);
		}
	}

	success = true;
out:
	mutex_release(&current_thread->process->map_lock);
	return success;
} 

