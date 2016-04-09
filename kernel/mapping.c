#include <map.h>
#include <frame.h>
#include <fs/inode.h>
#include <thread.h>
#include <process.h>
#include <assert.h>

static void _mapping_init(void *obj)
{
	struct mapping *map = obj;
	map->node = NULL;
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

bool mapping_establish(struct process *proc, uintptr_t virtual, int prot,
		int flags, struct inode *node, int nodepage)
{
	uintptr_t vpage = virtual / arch_mm_page_size(0);
	spinlock_acquire(&proc->map_lock);
	struct mapping *map = hash_lookup(&proc->mappings, &vpage, sizeof(vpage));
	if(map) {
		spinlock_release(&proc->map_lock);
		return false;
	}

	map = kobj_allocate(&kobj_mapping);
	map->node = node ? kobj_getref(node) : NULL;
	map->flags = flags;
	map->prot = prot;
	map->vpage = vpage;
	map->nodepage = nodepage;
	hash_insert(&proc->mappings, &map->vpage, sizeof(map->vpage), &map->elem, map);
	spinlock_release(&proc->map_lock);
	return true;
}

bool mapping_remove(struct process *proc, uintptr_t virtual)
{
	uintptr_t vpage = virtual / arch_mm_page_size(0);
	spinlock_acquire(&proc->map_lock);

	struct mapping *map = hash_lookup(&proc->mappings, &vpage, sizeof(vpage));
	if(!map) {
		spinlock_release(&proc->map_lock);
		return false;
	}
	hash_delete(&proc->mappings, &vpage, sizeof(vpage));
	if(map->flags & MAP_ANON) {
		if(map->frame != 0) {
			frame_release(map->frame);
		}
	} else {
		assert(map->node != NULL);
		if(map->flags & MAP_MAPPED)
			inode_release_page(map->node, map->nodepage);
		kobj_putref(map->node);
	}
	map->node = NULL;
	kobj_putref(map);
	spinlock_release(&proc->map_lock);
	return true;
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
		mapping_establish(to, map->vpage * arch_mm_page_size(0), map->prot, map->flags,
				map->node, map->nodepage);

	}
	spinlock_release(&from->map_lock);
}

void process_remove_mappings(struct process *proc)
{
	spinlock_acquire(&proc->map_lock);

	struct hashiter iter;
	for(hash_iter_init(&iter, &proc->mappings); !hash_iter_done(&iter); hash_iter_next(&iter)) {
		struct mapping *map = hash_iter_get(&iter);

		mapping_remove(proc, map->vpage * arch_mm_page_size(0));
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

	if((flags & FAULT_WRITE) && !(map->prot & PROT_WRITE)) {
		goto out;
	}

	uintptr_t frame = 0;
	if(map->flags & MAP_ANON) {
		frame = map->frame = frame_allocate();
		map->flags |= MAP_MAPPED;
	} else {
		frame = inode_get_page(map->node, map->nodepage);
	}

	arch_mm_virtual_map(proc->ctx, addr & ~(arch_mm_page_size(0) - 1),
			frame,
			arch_mm_page_size(0), MAP_USER | MAP_WRITE | MAP_PRIVATE);

	success = true;

out:
	spinlock_release(&proc->map_lock);
	return success;
}

