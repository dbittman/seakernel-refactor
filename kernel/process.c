#include <process.h>
#include <thread.h>
#include <slab.h>
#include <mmu.h>
#include <system.h>
#include <map.h>
#include <printk.h>
struct process *kernel_process;

static struct kobj_idmap processids;

uintptr_t process_allocate_user_tls(struct process *proc)
{
	uintptr_t base = atomic_fetch_add(&proc->next_user_tls, USER_TLS_SIZE);
	if(base >= USER_TLS_REGION_END) {
		/* TODO: kill process or something */
	}
	for(uintptr_t virt = base;virt < base + USER_TLS_SIZE;virt += arch_mm_page_size(0)) {
		mapping_establish(proc, virt, PROT_WRITE, MMAP_MAP_ANON | MMAP_MAP_PRIVATE, NULL, 0);
	}
	return base;
}

uintptr_t process_allocate_mmap_region(struct process *proc, size_t len)
{
	uintptr_t base = atomic_fetch_add(&proc->next_mmap_reg, len);
	if(base > USER_MMAP_REGION_END) {
		/* TODO: kill */
	}
	return base;
}

void process_attach_thread(struct process *proc, struct thread *thread)
{
	linkedlist_insert(&proc->threads, &thread->proc_entry, kobj_getref(thread));
	thread->process = kobj_getref(proc);
	thread->ctx = kobj_getref(proc->ctx);
}

__initializer static void __process_idmap_init(void)
{
	kobj_idmap_create(&processids, sizeof(int));
	kernel_process = kobj_allocate(&kobj_process);
}

static _Atomic int next_pid = 1;
static void _process_init(void *obj)
{
	struct process *proc = obj;
	proc->ctx = kobj_allocate(&kobj_vm_context);
	proc->next_user_tls = USER_TLS_REGION_START;
	proc->next_mmap_reg = USER_MMAP_REGION_START;
	proc->pid = next_pid++;
	proc->root = NULL;
	proc->cwd = NULL;
	kobj_idmap_insert(&processids, obj, &proc->pid);
	for(int i=0;i<MAX_FD;i++)
		proc->files[i].file = NULL;
}

static void _process_create(void *obj)
{
	struct process *proc = obj;
	_process_init(obj);
	linkedlist_create(&proc->threads, 0);
	hash_create(&proc->mappings, HASH_LOCKLESS, 4096);
	spinlock_create(&proc->map_lock);
	spinlock_create(&proc->files_lock);
}

static void _process_put(void *obj)
{
	struct process *proc = obj;
	kobj_putref(proc->ctx);
	process_close_files(proc, true);
	process_remove_mappings(proc, true);
	kobj_idmap_delete(&processids, obj, &proc->pid);
	kobj_putref(proc->cwd);
	kobj_putref(proc->root);
}

struct kobj kobj_process = {
	KOBJ_DEFAULT_ELEM(process),
	.init = _process_init,
	.create = _process_create,
	.put = _process_put,
	.destroy = NULL,
};

