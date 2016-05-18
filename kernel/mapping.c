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

static inline int __get_pagelevel(size_t len)
{
	for(int i=MMU_NUM_PAGESIZE_LEVELS-1;i>=0;i--) {
		if(arch_mm_page_size(i) <= len)
			return i;
	}
	return 0;
}

static inline size_t __get_pagesize(size_t len)
{
	return arch_mm_page_size(__get_pagelevel(len));
}

void map_region_remove(uintptr_t start, size_t len, bool locked)
{

}

void map_region_setup(struct process *proc, uintptr_t start, size_t len, int prot, int flags, struct file *file, int nodepage, bool locked)
{
	struct map_region *reg = kobj_allocate(&kobj_map_region);
	reg->prot = prot;
	reg->flags = flags;
	reg->file = file ? kobj_getref(file) : NULL;
	reg->nodepage = nodepage;
	reg->start = start;
	reg->length = len;
	int pl = __get_pagelevel(len);
	reg->psize = arch_mm_page_size(pl);

	mutex_acquire(&proc->map_lock);
	map_region_remove(start, len, true);
	linkedlist_insert(&proc->maps[pl], &reg->entry, reg);
	mutex_release(&proc->map_lock);
}



static struct map_region *__find_region(struct process *proc, uintptr_t addr)
{
	for(int i=MMU_NUM_PAGESIZE_LEVELS;i>=0;i--) {
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

uintptr_t __get_phys_to_map(struct process *proc, struct map_region *reg, uintptr_t v)
{
	int pg = (v - reg->start) / arch_mm_page_size(0);
	if(reg->file) {
		uintptr_t frame = reg->file->ops->map(reg->file, reg, v - reg->start);
		frame_acquire(frame);
		return frame;
	} else {
		return frame_allocate();
	}
}

int mmu_mappings_handle_fault(uintptr_t addr, int flags)
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
	struct process *proc = current_thread->process;
	mutex_acquire(&proc->map_lock);
	struct map_region *reg = __find_region(proc, addr);
	if(!reg)
		goto out;
	if((flags & FAULT_WRITE) && !(reg->prot & PROT_WRITE)) {
		goto out;
	}
	
	int set = MAP_PRIVATE | MAP_USER;
	if(reg->prot & PROT_WRITE)
		set |= MAP_WRITE;
	if(reg->prot & PROT_EXEC)
		set |= MAP_EXECUTE;

	uintptr_t v = addr & ~(reg->psize - 1);
	if(flags & FAULT_ERROR_PRES) {
		if(arch_mm_virtual_getmap(proc->ctx, v, NULL, NULL, NULL)) {
			/* another thread may have gotten here first */
			success = true;
			goto out;
		}

		uintptr_t phys = __get_phys_to_map(proc, reg, v);
		
		if((reg->flags & MMAP_MAP_PRIVATE) || !(reg->flags & MMAP_MAP_ANON))
			set &= ~MAP_WRITE;

		arch_mm_virtual_map(proc->ctx, v, phys, reg->psize, set);
	} else {
		if(flags & FAULT_EXEC) {
			goto out;
		}

		if((flags & FAULT_WRITE) && (reg->flags & MMAP_MAP_PRIVATE)) {
			if(reg->flags & MMAP_MAP_ANON) {
				uintptr_t phys;
				bool r = arch_mm_virtual_getmap(proc->ctx, v, &phys, NULL, NULL);
				assert(r);

				struct frame *frame = frame_get_from_address(phys);
				if(frame->count > 1) {
					uintptr_t newframe = frame_allocate();
					
					memcpy((void *)(newframe + PHYS_MAP_START), (void *)(phys + PHYS_MAP_START), arch_mm_page_size(0));
					uintptr_t r2 = arch_mm_virtual_unmap(proc->ctx, v);
					assert(r2 != 0);
					r = arch_mm_virtual_map(proc->ctx, v, newframe, reg->psize, set);
					assert(r);
					frame_release(phys);
				} else {
					arch_mm_virtual_chattr(proc->ctx, v, set);
				}
			} else {
				uintptr_t phys;
				bool r = arch_mm_virtual_getmap(proc->ctx, v,&phys, NULL, NULL);
				assert(r);

				uintptr_t newframe = frame_allocate();
				memcpy((void *)(newframe + PHYS_MAP_START), (void *)(phys + PHYS_MAP_START), reg->psize);

				arch_mm_virtual_unmap(proc->ctx, v);
				arch_mm_virtual_map(proc->ctx, v, newframe, reg->psize, set);
				frame_release(phys);
			}
		} else if((flags & FAULT_WRITE) && !(reg->flags & MMAP_MAP_ANON) && (reg->flags & MMAP_MAP_SHARED)) {
			/* TODO: MARK INODE PAGE DIRTY */
			arch_mm_virtual_chattr(proc->ctx, addr & page_mask(0), set);
		}
	}

	success = true;
out:
	mutex_release(&current_thread->process->map_lock);
	return success;
} 

#if 0
int mmu_mappings_handle_fault(uintptr_t addr, int flags)
{
	(void)flags;
	struct process *proc = current_thread->process;
	int success = false;
	uintptr_t vpage = addr / arch_mm_page_size(0);
	mutex_acquire(&proc->map_lock);
	struct mapping *map = hash_lookup(&proc->mappings, &vpage, sizeof(vpage));
	
	if(!map) {
		goto out;
	}
	int setflags = MAP_USER | MAP_PRIVATE;

	if(map->prot & PROT_WRITE)
		setflags |= MAP_WRITE;
	if(map->prot & PROT_EXEC)
		setflags |= MAP_EXECUTE;

	if((flags & FAULT_WRITE) && !(map->prot & PROT_WRITE)) {
		goto out;
	}

	/* TODO: there are times when we may know that we're doing a
	 * write but the page isn't present. We could skip ahead to
	 * the FAULT_ERROR_PERM case */
	if(flags & FAULT_ERROR_PRES) {
		uintptr_t frame = 0;
		if(map->flags & MMAP_MAP_MAPPED) {
			/* possible another thread got here first */
			goto done;
		}
		if(map->flags & MMAP_MAP_ANON) {
			if(!map->frame)
				map->frame = frame_allocate();
			frame = map->frame;
		} else {
			if(!map->page) {
				if(!map->file->ops->map(map->file, map)) {
					/* failed */
					goto out;
				}
			}
			frame = map->page->frame;
		}
		map->flags |= MMAP_MAP_MAPPED;

		if((map->flags & MMAP_MAP_PRIVATE) || !(map->flags & MMAP_MAP_ANON))
			setflags &= ~MAP_WRITE;

		arch_mm_virtual_map(proc->ctx, addr & page_mask(0),
				frame, arch_mm_page_size(0), setflags);
	} else {
		if(flags & FAULT_EXEC)
			goto out;

		if((flags & FAULT_WRITE) && (map->flags & MMAP_MAP_PRIVATE)) {
			if(map->flags & MMAP_MAP_ANON) {
				assert(map->frame);
				struct frame *frame = frame_get_from_address(map->frame);

				assert(frame->count > 0);
				if(frame->count > 1) {
					uintptr_t newframe = frame_allocate();
					memcpy((void *)(newframe + PHYS_MAP_START), (void *)(map->frame + PHYS_MAP_START), arch_mm_page_size(0));
					uintptr_t r = arch_mm_virtual_unmap(proc->ctx, addr & page_mask(0));
					assert(r != 0);
					r = arch_mm_virtual_map(proc->ctx, addr & page_mask(0), newframe, arch_mm_page_size(0), setflags);
					assert(r);
					frame_release(map->frame);
					map->frame = newframe;
				} else {
					arch_mm_virtual_chattr(proc->ctx, addr & page_mask(0), setflags);
				}
			} else {
				struct inodepage *page = map->page;
				map->flags |= MMAP_MAP_ANON;
				uintptr_t newframe = frame_allocate();
				memcpy((void *)(newframe + PHYS_MAP_START), (void *)(page->frame + PHYS_MAP_START), arch_mm_page_size(0));

				arch_mm_virtual_unmap(proc->ctx, addr & page_mask(0));
				arch_mm_virtual_map(proc->ctx, addr & page_mask(0), newframe, arch_mm_page_size(0), setflags);
				if(map->file->ops->unmap) {
					map->file->ops->unmap(map->file, map);
					kobj_putref(map->file);
					map->file = NULL;
				}
				map->frame = newframe;
			}
		} else if(flags & FAULT_WRITE) {
			if(!(map->flags & MMAP_MAP_ANON)) {
				if(map->flags & MMAP_MAP_SHARED)
					map->page->flags |= INODEPAGE_DIRTY;
				arch_mm_virtual_chattr(proc->ctx, addr & page_mask(0), setflags);
			}
		}

	}
done:
	success = true;

out:
	mutex_release(&proc->map_lock);
	return success;
}






#endif







#if 0

/* TODO: simplify this system. I don't
 * think it needs to store the page, just
 * the frame. */

static void _mapping_init(void *obj)
{
	struct mapping *map = obj;
	map->file = NULL;
	map->frame = 0;
	map->flags = 0;
}

static void _mapping_create(void *obj)
{
	_mapping_init(obj);
}

struct kobj kobj_mapping = {
	KOBJ_DEFAULT_ELEM(mapping),
	.create = _mapping_create,
	.init = _mapping_init,
	.put = NULL,
	.destroy = NULL,
};

static struct mapping *_do_mapping_establish(struct process *proc, uintptr_t virtual, int prot,
		int flags, struct file *file, int nodepage, bool locked)
{
	uintptr_t vpage = virtual / arch_mm_page_size(0);
	if(!locked)
		mutex_acquire(&proc->map_lock);
	struct mapping *map = hash_lookup(&proc->mappings, &vpage, sizeof(vpage));
	if(map) {
		mutex_release(&proc->map_lock);
		return NULL;
	}

	assert((flags & MMAP_MAP_SHARED) || (flags & MMAP_MAP_PRIVATE));
	assert(!((flags & MMAP_MAP_SHARED) && (flags & MMAP_MAP_PRIVATE)));

	map = kobj_allocate(&kobj_mapping);
	map->file = file ? kobj_getref(file) : NULL;
	map->flags = flags;
	map->prot = prot;
	map->vpage = vpage;
	map->nodepage = nodepage;
	hash_insert(&proc->mappings, &map->vpage, sizeof(map->vpage), &map->elem, map);
	if(!locked)
		mutex_release(&proc->map_lock);
	return map;
}

struct mapping *mapping_establish(struct process *proc, uintptr_t virtual, int prot,
		int flags, struct file *file, int nodepage)
{
	return _do_mapping_establish(proc, virtual, prot, flags, file, nodepage, false);
}

static bool _do_mapping_remove(struct process *proc, uintptr_t virtual, bool locked)
{
	uintptr_t vpage = virtual / arch_mm_page_size(0);
	if(!locked)
		mutex_acquire(&proc->map_lock);

	struct mapping *map = hash_lookup(&proc->mappings, &vpage, sizeof(vpage));
	if(!map) {
		mutex_release(&proc->map_lock);
		return false;
	}
	hash_delete(&proc->mappings, &vpage, sizeof(vpage));
	arch_mm_virtual_unmap(proc->ctx, virtual);
	if(map->flags & MMAP_MAP_ANON) {
		if(map->frame != 0) {
			frame_release(map->frame);
		}
	} else {
		assert(map->file != NULL);
		if(map->flags & MMAP_MAP_MAPPED)
			if(map->file->ops->unmap)
				map->file->ops->unmap(map->file, map);
		kobj_putref(map->file);
	}
	map->file = NULL;
	kobj_putref(map);
	if(!locked)
		mutex_release(&proc->map_lock);
	return true;
}

int mapping_move(uintptr_t virt, size_t oldsz, size_t newsz, uintptr_t new)
{
	struct process *proc = current_thread->process;
	mutex_acquire(&proc->map_lock);
	struct mapping *base = NULL;
	for(uintptr_t off = 0;off < oldsz;off += arch_mm_page_size(0)) {
		uintptr_t vpage = (off + virt) / arch_mm_page_size(0);
		struct mapping *map = hash_lookup(&proc->mappings, &vpage, sizeof(vpage));
		if(!map) {
			mutex_release(&proc->map_lock);
			return -EFAULT;
		}
		if(!base)
			base = map;
		int cpage = base->nodepage + vpage - virt / arch_mm_page_size(0);
		if(map->prot != base->prot || (map->flags & (MMAP_MAP_SHARED | MMAP_MAP_ANON | MMAP_MAP_PRIVATE)) != (base->flags & (MMAP_MAP_SHARED | MMAP_MAP_ANON | MMAP_MAP_PRIVATE))
				|| (map->file != base->file) || (map->nodepage != cpage)) {
			mutex_release(&proc->map_lock);
			return -EFAULT;
		}
	}

	assert(base != NULL);

	int page = 0;
	uintptr_t off;
	for(off = 0;off < newsz;off += arch_mm_page_size(0)) {
		uintptr_t vpage_old = (off + virt) / arch_mm_page_size(0);
		uintptr_t vpage_new = (off + new) / arch_mm_page_size(0);
		if(off < oldsz) {
			struct mapping *map = hash_lookup(&proc->mappings, &vpage_old, sizeof(vpage_old));
			assert(map != NULL);
			int r = hash_delete(&proc->mappings, &vpage_old, sizeof(vpage_old));
			assert(r == 0);
			arch_mm_virtual_unmap(proc->ctx, vpage_old * arch_mm_page_size(0));
			map->vpage = vpage_new;
			map->flags &= ~MMAP_MAP_MAPPED;
			r = hash_insert(&proc->mappings, &map->vpage, sizeof(map->vpage), &map->elem, map);
			assert(r == 0);
		} else {
			_do_mapping_establish(proc, new + off, base->prot, base->flags & ~MMAP_MAP_MAPPED, base->file, base->nodepage + page, true);
		}
		page++;
	}
	if(newsz < oldsz) {
		for(;off < oldsz;off+=arch_mm_page_size(0)) {
			_do_mapping_remove(proc, virt + off, true);
		}
	}
	mutex_release(&proc->map_lock);
	return 0;
}

int mapping_try_expand(uintptr_t virt, size_t oldsz, size_t newsz)
{
	struct process *proc = current_thread->process;
	mutex_acquire(&proc->map_lock);
	uintptr_t tmp = 0;
	struct mapping *base = 0;
	if(oldsz < newsz) {
		for(uintptr_t off = 0;off < newsz;off += arch_mm_page_size(0)) {
			uintptr_t vpage = (off + virt) / arch_mm_page_size(0);
			struct mapping *map = hash_lookup(&proc->mappings, &vpage, sizeof(vpage));
			if(off == 0)
				base = map;
			if(!map && off < oldsz) {
				mutex_release(&proc->map_lock);
				return -EFAULT;
			}
			if(off >= oldsz && tmp == 0)
				tmp = virt + off;
			
			if(map) {
				/* undo */
				for(uintptr_t undo = tmp; tmp < virt + off;tmp += arch_mm_page_size(0)) {
					_do_mapping_remove(proc, undo, true);
				}
				return -ENOMEM;
			}

			assert(base != NULL);

			_do_mapping_establish(proc, virt + off, base->prot, base->flags & ~MMAP_MAP_MAPPED, base->file, base->nodepage + vpage - (virt / arch_mm_page_size(0)), true);
		}
	} else {
		virt = ((virt + newsz - 1) & page_mask(0)) + arch_mm_page_size(0);
		for(; virt < virt+oldsz;virt += arch_mm_page_size(0)) {
			_do_mapping_remove(proc, virt, true);
		}
	}
	mutex_release(&proc->map_lock);
	return 0;
}

bool mapping_remove(struct process *proc, uintptr_t virtual)
{
	return _do_mapping_remove(proc, virtual, false);
}

void map_mmap(uintptr_t virtual, struct file *file, int prot, int flags, size_t len, size_t off)
{
	int num = (len - 1) / arch_mm_page_size(0) + 1;
	int nodepage = off / arch_mm_page_size(0);
	//mutex_acquire(&current_thread->process->map_lock);
	for(int i=0;i<num;i++) {
		_do_mapping_remove(current_thread->process, virtual + i * arch_mm_page_size(0), false);
		_do_mapping_establish(current_thread->process, virtual + i * arch_mm_page_size(0),
				prot, flags, file, nodepage + i, false);
	}
	//mutex_release(&current_thread->process->map_lock);
}

int map_change_protect(struct process *proc, uintptr_t virt, size_t len, int prot)
{
	int num = (len - 1) / arch_mm_page_size(0) + 1;
	mutex_acquire(&current_thread->process->map_lock);
	for(int i=0;i<num;i++) {
		uintptr_t vpage = (virt / arch_mm_page_size(0)) + i;
		struct mapping *map = hash_lookup(&proc->mappings, &vpage, sizeof(vpage));
		if(map->file && (((prot & PROT_WRITE) && !(map->file->flags & F_WRITE))
					|| (((prot & PROT_READ) && !(map->file->flags & F_READ))))) {
			mutex_release(&current_thread->process->map_lock);
			return -EACCES;
		}

		map->prot = prot;
	}
	mutex_release(&current_thread->process->map_lock);
	return 0;
}

void map_unmap(uintptr_t virtual, size_t length)
{
	for(unsigned i=0;i<((length - 1) / arch_mm_page_size(0) + 1);i++)
		mapping_remove(current_thread->process, virtual + i * arch_mm_page_size(0));
}

void process_copy_mappings(struct process *from, struct process *to)
{
	mutex_acquire(&from->map_lock);

	struct hashiter iter;
	for(hash_iter_init(&iter, &from->mappings); !hash_iter_done(&iter); hash_iter_next(&iter)) {
		struct mapping *map = hash_iter_get(&iter);
		struct mapping *nm = mapping_establish(to, map->vpage * arch_mm_page_size(0), map->prot, map->flags,
				map->file, map->nodepage);

		if(!(map->flags & MMAP_MAP_ANON)) {
			if(map->page) {
				nm->page = kobj_getref(map->page);
			}
		} else {
			if(map->frame) {
				frame_acquire(map->frame);
				nm->frame = map->frame;
			}
		}
		if(!(map->flags & MMAP_MAP_SHARED)) {
			int flags;
			if(arch_mm_virtual_getmap(from->ctx, map->vpage * arch_mm_page_size(0), NULL, &flags, NULL)) {
				flags &= ~MAP_WRITE;
				int r = arch_mm_virtual_chattr(from->ctx, map->vpage * arch_mm_page_size(0), flags);
				assert(r);
			}
		}
		nm->flags &= ~MMAP_MAP_MAPPED;
	}
	mutex_release(&from->map_lock);
}

void process_remove_mappings(struct process *proc, bool user_tls_too)
{
	mutex_acquire(&proc->map_lock);

	struct hashiter iter;
	for(hash_iter_init(&iter, &proc->mappings); !hash_iter_done(&iter); hash_iter_next(&iter)) {
		struct mapping *map = hash_iter_get(&iter);

		if(map->vpage * arch_mm_page_size(0) < USER_TLS_REGION_START
				|| map->vpage * arch_mm_page_size(0) >= USER_TLS_REGION_END
				|| user_tls_too) {
			_do_mapping_remove(proc, map->vpage * arch_mm_page_size(0), true);
		}
	}
	mutex_release(&proc->map_lock);
}


#endif

