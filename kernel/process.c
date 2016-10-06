#include <process.h>
#include <thread.h>
#include <slab.h>
#include <mmu.h>
#include <system.h>
#include <map.h>
#include <printk.h>
#include <fs/inode.h>
#include <fs/sys.h>
#include <fs/proc.h>
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
	map_mmap(proc, base, USER_TLS_SIZE, PROT_WRITE | PROT_READ, MMAP_MAP_ANON | MMAP_MAP_PRIVATE, NULL, 0);
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
	proc->ctx = kobj_allocate(&kobj_vm_context);
	proc->files = (void *)mm_virtual_allocate(__round_up_pow2(sizeof(struct fildes) * MAX_FD), true);
	_process_init(obj);
	linkedlist_create(&proc->threads, 0);
	for(int i=0;i<MMU_NUM_PAGESIZE_LEVELS;i++) {
		linkedlist_create(&proc->maps[i], LINKEDLIST_LOCKLESS);
	}
	mutex_create(&proc->map_lock);
	spinlock_create(&proc->files_lock);
	spinlock_create(&proc->signal_lock);
	blocklist_create(&proc->wait);
}

static void _process_put(void *obj)
{
	struct process *proc = obj;
	assert(proc != kernel_process);
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

static void __remove_proc_entries(struct process *proc)
{
#define remove_proc_entry(pid, name) do { char str[128]; snprintf(str, 128, "/proc/%d/%s", pid, name); proc_destroy(str); } while(0)
	remove_proc_entry(proc->pid, "status");
	kobj_putref(proc);
	remove_proc_entry(proc->pid, "uid");
	kobj_putref(proc);
	remove_proc_entry(proc->pid, "euid");
	kobj_putref(proc);
	remove_proc_entry(proc->pid, "suid");
	kobj_putref(proc);
	remove_proc_entry(proc->pid, "gid");
	kobj_putref(proc);
	remove_proc_entry(proc->pid, "egid");
	kobj_putref(proc);
	remove_proc_entry(proc->pid, "sgid");
	kobj_putref(proc);
	remove_proc_entry(proc->pid, "sid");
	kobj_putref(proc);
	remove_proc_entry(proc->pid, "pgroupid");
	kobj_putref(proc);
	remove_proc_entry(proc->pid, "cmask");
	kobj_putref(proc);
	remove_proc_entry(proc->pid, "brk");
	kobj_putref(proc);
	remove_proc_entry(proc->pid, "maps");
	kobj_putref(proc);
	char str[128];
	snprintf(str, 128, "/proc/%d/exe", proc->pid);
	sys_unlink(str);
	snprintf(str, 128, "/proc/%d/fd", proc->pid);
	int r = sys_rmdir(str);
	assert(r == 0);
	snprintf(str, 128, "/proc/%d", proc->pid);
	r = sys_rmdir(str);
	assertmsg(r == 0, "%d", r);
}

#include <interrupt.h>
void process_exit(struct process *proc, int code)
{
	proc->exit_code = code;
	process_remove_mappings(proc, true);
	process_close_files(proc, true);
	if(WIFEXITED(proc->status)) {
		proc->status = process_make_status(code, 0, true, false);
	}
	proc->flags |= PROC_EXITED | PROC_STATUS_CHANGED;
	blocklist_unblock_all(&proc->wait);

	__remove_proc_entries(proc);

	struct process *init = process_get_by_pid(1);
	assert(init != NULL);
	struct hashiter iter;
	kobj_idmap_lock(&processids);
	for(kobj_idmap_iter_init(&processids, &iter);
			!kobj_idmap_iter_done(&iter);
			kobj_idmap_iter_next(&iter)) {
		struct process *p = kobj_idmap_iter_get(&iter);
		if(p == proc->parent) {
			process_send_signal(p, SIGCHLD);
		}
		if(p->parent == proc) {
			kobj_putref(proc);
			p->parent = kobj_getref(init);
		}
	}
	kobj_idmap_unlock(&processids);

	kobj_putref(init);
}

