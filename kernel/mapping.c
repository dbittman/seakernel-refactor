#include <map.h>
#include <frame.h>
#include <fs/inode.h>
#include <thread.h>
#include <process.h>
#include <assert.h>
#include <printk.h>
#include <string.h>

static void _mapping_init(void *obj)
{
	struct mapping *map = obj;
	map->node = NULL;
	map->frame = 0;
	map->flags = 0;
}

static void _mapping_create(void *obj)
{
	_mapping_init(obj);
}

struct kobj kobj_mapping = {
	.initialized = false,
	.name = "mapping",
	.size = sizeof(struct mapping),
	.create = _mapping_create,
	.init = _mapping_init,
	.put = NULL,
	.destroy = NULL,
};

struct mapping *mapping_establish(struct process *proc, uintptr_t virtual, int prot,
		int flags, struct inode *node, int nodepage)
{
	uintptr_t vpage = virtual / arch_mm_page_size(0);
	spinlock_acquire(&proc->map_lock);
	struct mapping *map = hash_lookup(&proc->mappings, &vpage, sizeof(vpage));
	if(map) {
		spinlock_release(&proc->map_lock);
		return NULL;
	}

	assert((flags & MMAP_MAP_SHARED) || (flags & MMAP_MAP_PRIVATE));
	assert(!((flags & MMAP_MAP_SHARED) && (flags & MMAP_MAP_PRIVATE)));

	map = kobj_allocate(&kobj_mapping);
	map->node = node ? kobj_getref(node) : NULL;
	map->flags = flags;
	map->prot = prot;
	map->vpage = vpage;
	map->nodepage = nodepage;
	hash_insert(&proc->mappings, &map->vpage, sizeof(map->vpage), &map->elem, map);
	spinlock_release(&proc->map_lock);
	return map;
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
	if(map->flags & MMAP_MAP_ANON) {
		if(map->frame != 0) {
			frame_release(map->frame);
		}
	} else {
		assert(map->node != NULL);
		if(map->flags & MMAP_MAP_MAPPED)
			inode_release_page(map->node, map->page);
		inode_put(map->node);
	}
	map->node = NULL;
	kobj_putref(map);
	if(!locked)
		spinlock_release(&proc->map_lock);
	return true;
}

bool mapping_remove(struct process *proc, uintptr_t virtual)
{
	return _do_mapping_remove(proc, virtual, false);
}

void map_mmap(uintptr_t virtual, struct inode *node, int prot, int flags, size_t len, size_t off)
{
	int num = len / arch_mm_page_size(0);
	int nodepage = off / arch_mm_page_size(0);
	for(int i=0;i<num;i++) {
		mapping_establish(current_thread->process, virtual + i * arch_mm_page_size(0),
				prot, flags, node, nodepage + i);
	}
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
				map->node, map->nodepage);

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
			arch_mm_virtual_unmap(proc->ctx, map->vpage * arch_mm_page_size(0));
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
			if(!map->page)
				map->page = inode_get_page(map->node, map->nodepage);
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
					int r = arch_mm_virtual_unmap(proc->ctx, addr & page_mask(0));
					assert(r > 0);
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
				map->frame = frame_allocate();
				memcpy((void *)(map->frame + PHYS_MAP_START), (void *)(page->frame + PHYS_MAP_START), arch_mm_page_size(0));

				arch_mm_virtual_unmap(proc->ctx, addr & page_mask(0));
				arch_mm_virtual_map(proc->ctx, addr & page_mask(0), map->frame, arch_mm_page_size(0), setflags);
				inode_release_page(map->node, page);
			}
		} else if(flags & FAULT_WRITE) {
			if(!(map->flags & MMAP_MAP_ANON)) {
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

