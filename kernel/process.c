#include <process.h>
#include <thread.h>
#include <slab.h>
#include <mmu.h>
#include <system.h>
#include <map.h>
#include <printk.h>
#include <fs/inode.h>
struct process *kernel_process;

struct kobj_idmap processids;

struct process *process_get_by_pid(int pid)
{
	return kobj_idmap_lookup(&processids, &pid);
}

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

static _Atomic int next_pid = 0;
static void _process_init(void *obj)
{
	struct process *proc = obj;
	proc->ctx = kobj_allocate(&kobj_vm_context);
	proc->next_user_tls = USER_TLS_REGION_START;
	proc->next_mmap_reg = USER_MMAP_REGION_START;
	proc->pid = next_pid++;
	proc->root = NULL;
	proc->cwd = NULL;
	proc->pty = NULL;
	proc->status = 0;
	proc->flags = 0;
	kobj_idmap_insert(&processids, obj, &proc->pid);
	for(int i=0;i<MAX_FD;i++)
		proc->files[i].file = NULL;
	memset(proc->actions, 0, sizeof(proc->actions));
}

static void _process_create(void *obj)
{
	struct process *proc = obj;
	_process_init(obj);
	linkedlist_create(&proc->threads, 0);
	hash_create(&proc->mappings, HASH_LOCKLESS, 4096);
	spinlock_create(&proc->map_lock);
	spinlock_create(&proc->files_lock);
	spinlock_create(&proc->signal_lock);
	blocklist_create(&proc->wait);
}

static void _process_put(void *obj)
{
	struct process *proc = obj;
	assert(proc != kernel_process);
	printk("Process put %d\n", proc->pid);
	kobj_putref(proc->ctx);
	process_close_files(proc, true);
	process_remove_mappings(proc, true);
	inode_put(proc->cwd);
	inode_put(proc->root);
	if(proc->pty)
		kobj_putref(proc->pty);
}

struct kobj kobj_process = {
	KOBJ_DEFAULT_ELEM(process),
	.init = _process_init,
	.create = _process_create,
	.put = _process_put,
	.destroy = NULL,
};

void process_exit(struct process *proc, int code)
{
	proc->exit_code = code;
	process_close_files(proc, true);
	if(WIFEXITED(proc->status)) {
		proc->status = process_make_status(code, 0, true, false);
	}
	proc->flags |= PROC_EXITED | PROC_STATUS_CHANGED;
	blocklist_unblock_all(&proc->wait);
}

