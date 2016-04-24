#include <map.h>
#include <frame.h>
#include <file.h>
#include <thread.h>
#include <process.h>
#include <assert.h>
#include <printk.h>
#include <string.h>
#include <errno.h>
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
		spinlock_acquire(&proc->map_lock);
	struct mapping *map = hash_lookup(&proc->mappings, &vpage, sizeof(vpage));
	if(map) {
		spinlock_release(&proc->map_lock);
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
		spinlock_release(&proc->map_lock);
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
		spinlock_acquire(&proc->map_lock);

	struct mapping *map = hash_lookup(&proc->mappings, &vpage, sizeof(vpage));
	if(!map) {
		spinlock_release(&proc->map_lock);
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
		spinlock_release(&proc->map_lock);
	return true;
}

int mapping_move(uintptr_t virt, size_t oldsz, size_t newsz, uintptr_t new)
{
	struct process *proc = current_thread->process;
	spinlock_acquire(&proc->map_lock);
	struct mapping *base = NULL;
	for(uintptr_t off = 0;off < oldsz;off += arch_mm_page_size(0)) {
		uintptr_t vpage = (off + virt) / arch_mm_page_size(0);
		struct mapping *map = hash_lookup(&proc->mappings, &vpage, sizeof(vpage));
		if(!map) {
			spinlock_release(&proc->map_lock);
			return -EFAULT;
		}
		if(!base)
			base = map;
		int cpage = base->nodepage + vpage - virt / arch_mm_page_size(0);
		if(map->prot != base->prot || (map->flags & (MMAP_MAP_SHARED | MMAP_MAP_ANON | MMAP_MAP_PRIVATE)) != (base->flags & (MMAP_MAP_SHARED | MMAP_MAP_ANON | MMAP_MAP_PRIVATE))
				|| (map->file != base->file) || (map->nodepage != cpage)) {
			spinlock_release(&proc->map_lock);
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
	spinlock_release(&proc->map_lock);
	return 0;
}

int mapping_try_expand(uintptr_t virt, size_t oldsz, size_t newsz)
{
	struct process *proc = current_thread->process;
	spinlock_acquire(&proc->map_lock);
	uintptr_t tmp = 0;
	struct mapping *base = 0;
	if(oldsz < newsz) {
		for(uintptr_t off = 0;off < newsz;off += arch_mm_page_size(0)) {
			uintptr_t vpage = (off + virt) / arch_mm_page_size(0);
			struct mapping *map = hash_lookup(&proc->mappings, &vpage, sizeof(vpage));
			if(off == 0)
				base = map;
			if(!map && off < oldsz) {
				spinlock_release(&proc->map_lock);
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
	spinlock_release(&proc->map_lock);
	return 0;
}

bool mapping_remove(struct process *proc, uintptr_t virtual)
{
	return _do_mapping_remove(proc, virtual, false);
}

void map_mmap(uintptr_t virtual, struct file *file, int prot, int flags, size_t len, size_t off)
{
	int num = len / arch_mm_page_size(0);
	int nodepage = off / arch_mm_page_size(0);
	//spinlock_acquire(&current_thread->process->map_lock);
	for(int i=0;i<num;i++) {
		_do_mapping_remove(current_thread->process, virtual + i * arch_mm_page_size(0), false);
		_do_mapping_establish(current_thread->process, virtual + i * arch_mm_page_size(0),
				prot, flags, file, nodepage + i, false);
	}
	//spinlock_release(&current_thread->process->map_lock);
}

void map_unmap(uintptr_t virtual, size_t length)
{
	for(unsigned i=0;i<(length / arch_mm_page_size(0));i++)
		mapping_remove(current_thread->process, virtual + i * arch_mm_page_size(0));
}

void process_copy_mappings(struct process *from, struct process *to)
{
	spinlock_acquire(&from->map_lock);

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
			if(arch_mm_virtual_getmap(from->ctx, map->vpage * arch_mm_page_size(0), NULL, &flags)) {
				flags &= ~MAP_WRITE;
				int r = arch_mm_virtual_chattr(from->ctx, map->vpage * arch_mm_page_size(0), flags);
				assert(r);
			}
		}
		nm->flags &= ~MMAP_MAP_MAPPED;
	}
	spinlock_release(&from->map_lock);
}

void process_remove_mappings(struct process *proc, bool user_tls_too)
{
	spinlock_acquire(&proc->map_lock);

	struct hashiter iter;
	for(hash_iter_init(&iter, &proc->mappings); !hash_iter_done(&iter); hash_iter_next(&iter)) {
		struct mapping *map = hash_iter_get(&iter);

		if(map->vpage * arch_mm_page_size(0) < USER_TLS_REGION_START
				|| map->vpage * arch_mm_page_size(0) >= USER_TLS_REGION_END
				|| user_tls_too) {
			_do_mapping_remove(proc, map->vpage * arch_mm_page_size(0), true);
		}
	}
	spinlock_release(&proc->map_lock);
}

int mmu_mappings_handle_fault(uintptr_t addr, int flags)
{
	(void)flags;
	struct process *proc = current_thread->process;
	int success = false;
	uintptr_t vpage = addr / arch_mm_page_size(0);
	spinlock_acquire(&proc->map_lock);
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
		assert(!(map->flags & MMAP_MAP_MAPPED));
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
		assert(map->flags & MMAP_MAP_MAPPED);
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
	success = true;

out:
	spinlock_release(&proc->map_lock);
	return success;
}

