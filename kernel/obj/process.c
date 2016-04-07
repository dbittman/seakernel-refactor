#include <mmu.h>
#include <slab.h>
#include <obj/process.h>
#include <obj/object.h>
#include <printk.h>
void process_attach_thread(struct process *proc, struct thread *thread)
{
	linkedlist_insert(&proc->threads, &thread->proc_entry, kobj_getref(thread));
	thread->process = kobj_getref(proc);
	thread->ctx = kobj_getref(proc->ctx);
}

void process_pagefault_handle(uintptr_t addr, int flags)
{
	int objnr = addr / 0x100000000;
	printk("PROCESS %d %d\n", objnr, flags);
	struct process *proc = current_thread->process;
	struct fot_entry *entry = &proc->user_context_object->fot->entries[objnr];
	if(entry->options & FOT_PRESENT) {
		struct object *obj = object_get_by_guid(entry->guid);
		if(obj) {
			uintptr_t offset = addr & 0xFFFFFFFF;
			printk("Mapping object offset %lx\n", offset);
			
			size_t pagenr = offset / arch_mm_page_size(0);
			uintptr_t phys = (uintptr_t)hash_lookup(&obj->physicals, &pagenr, sizeof(pagenr));
			if(phys != 0) {
				arch_mm_virtual_map(proc->ctx, addr & ~(arch_mm_page_size(0) - 1), phys, 0x1000, MAP_USER | MAP_WRITE | MAP_EXECUTE);
				return;
			} else {
				/* TODO: how to handle this? */
			}
		}
	}
	/* TODO: kill thread */
	printk("Killing thread %ld\n", current_thread->tid);
	for(;;) schedule();
}

static void _process_init(void *obj)
{
	struct process *proc = obj;
	(void)proc;
}

static void _process_create(void *obj)
{
	struct process *proc = obj;
	proc->ctx = kobj_allocate(&kobj_vm_context);
	linkedlist_create(&proc->threads, 0);
	linkedlist_create(&proc->objects, 0);
}

static void _process_destroy(void *obj)
{
	struct process *proc = obj;
	(void)proc;
}

static void _process_put(void *obj)
{
	struct process *proc = obj;
	/* TODO: also clean up the address space */
	kobj_putref(proc->ctx);
	proc->ctx = NULL;
}

struct kobj kobj_process = {
	.name = "process",
	.size = sizeof(struct process),
	.create = _process_create,
	.put = _process_put,
	.init = _process_init,
	.destroy = _process_destroy,
	.initialized = false
};

