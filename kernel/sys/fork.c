#include <stdbool.h>
#include <sys.h>
#include <thread.h>
#include <process.h>
#include <slab.h>
#include <processor.h>
#include <string.h>
#include <printk.h>
#include <errno.h>
#include <system.h>
#include <fs/proc.h>
#include <fs/sys.h>
#include <klibc.h>
static void copy_process(struct process *parent, struct process *child)
{
	child->root = kobj_getref(parent->root);
	child->cwd = kobj_getref(parent->cwd);
	if(parent == kernel_process) {
		child->next_user_tls = USER_TLS_REGION_START;
		child->next_mmap_reg = USER_MMAP_REGION_START;
	} else {
		child->next_user_tls = parent->next_user_tls;
		child->next_mmap_reg = parent->next_mmap_reg;
	}
	memcpy(child->actions, parent->actions, sizeof(child->actions));
	child->parent = kobj_getref(parent);
	child->pty = parent->pty ? kobj_getref(parent->pty) : NULL;
	child->brk = parent->brk;
	child->pgroupid = parent->pgroupid;
	child->seshid = parent->seshid;
}

static void copy_thread(struct thread *parent, struct thread *child)
{
	child->time = parent->time;
	memcpy(&child->arch, &parent->arch, sizeof(child->arch));
}

static _Atomic uint64_t next_cpu=0;
static struct processor *select_processor(void)
{
	/* TODO: this is pretty stupid. */
	while(true) {
		struct processor *proc = processor_get_id(next_cpu++ % MAX_PROCESSORS);
		if((proc->flags & PROCESSOR_UP) && (proc->flags & PROCESSOR_PRESENT)) {
			return proc;
		}
	}
}

static int _map_compar(const void *_a, const void *_b)
{
	const struct mapping *a = _a;
	const struct mapping *b = _b;
	if(a->vpage < b->vpage)
		return -1;
	else if(a->vpage > b->vpage)
		return 1;
	return 0;
}

ssize_t _proc_read_maps(void *data, int rw, size_t off, size_t len, char *buf)
{
	if(rw != 0)
		return -EINVAL;
	size_t current = 0;
	struct process *proc = data;
	if(len < arch_mm_page_size(0))
		len = arch_mm_page_size(0);
	void *tmp = (void *)mm_virtual_allocate(__round_up_pow2(len), false);
	mutex_acquire(&proc->map_lock);
	size_t alloc = __round_up_pow2(proc->mappings.count * sizeof(struct mapping));
	if(alloc < arch_mm_page_size(0))
		alloc = arch_mm_page_size(0);
	struct mapping *maps = (void *)mm_virtual_allocate(alloc, true);
	
	struct hashiter iter;
	size_t num = 0;
	for(hash_iter_init(&iter, &proc->mappings);
			!hash_iter_done(&iter); hash_iter_next(&iter)) {
		struct mapping *map = hash_iter_get(&iter);
		memcpy(&maps[num++], map, sizeof(*map));
	}

	qsort(maps, num, sizeof(struct mapping), _map_compar);

	/* now combine neighbors */
	for(size_t i=0;i<num;i++) {
		struct mapping *map = &maps[i];
		size_t pagecount = 1;
		if(map->vpage > 0) {
			for(size_t j=1;j<(num-i);j++) {
				struct mapping *next = &maps[i+j];
				if(next->vpage == map->vpage + j
						&& (map->flags & ~MMAP_MAP_MAPPED) == (map->flags & ~MMAP_MAP_MAPPED)
						&& map->prot == map->prot) {
					if((map->flags & MMAP_MAP_ANON)
							|| (unsigned)next->nodepage == map->nodepage + j) {
						/* merge */
						next->vpage = 0; //mark as not here
						pagecount++;
					}
				}
			}
			map->nodepage = pagecount;
		}
	}
	PROCFS_PRINTF(off, len, tmp, current,
			"         MAP BEGIN              MAP END      FLAGS   EWR (PROT)\n");
	for(size_t i=0;i<num;i++) {
		struct mapping *map = &maps[i];
		if(map->vpage > 0) {
			PROCFS_PRINTF(off, len, tmp, current,
					"%16.16lx - %16.16lx %8.8lx %3.3b\n", map->vpage * arch_mm_page_size(0), (map->vpage + (uintptr_t)map->nodepage) * arch_mm_page_size(0), map->flags, map->prot);
		}
	}

	mutex_release(&proc->map_lock);
	memcpy(buf, tmp, current > len ? len : current);
	mm_virtual_deallocate((uintptr_t)maps);
	mm_virtual_deallocate((uintptr_t)tmp);
	return current;
}

static void __create_proc_entries(struct process *proc)
{
	#define __proc_make(pid,name,call,data) do { char str[128]; snprintf(str, 128, "/proc/%d/%s", pid, name); proc_create(str, call, data); } while(0)
	char dir[128];
	snprintf(dir, 128, "/proc/%d", proc->pid);
	int r = sys_mkdir(dir, 0755);
	assert(r == 0);
	snprintf(dir, 128, "/proc/%d/fd", proc->pid);
	r = sys_mkdir(dir, 0755);
	assert(r == 0);
	kobj_getref(proc);
	__proc_make(proc->pid, "status", _proc_read_int, &proc->status);
	kobj_getref(proc);
	__proc_make(proc->pid, "cmask", _proc_read_int, &proc->cmask);
	kobj_getref(proc);
	__proc_make(proc->pid, "sid", _proc_read_int, &proc->seshid);
	kobj_getref(proc);
	__proc_make(proc->pid, "pgroupid", _proc_read_int, &proc->pgroupid);
	kobj_getref(proc);
	__proc_make(proc->pid, "uid", _proc_read_int, &proc->status);
	kobj_getref(proc);
	__proc_make(proc->pid, "euid", _proc_read_int, &proc->status);
	kobj_getref(proc);
	__proc_make(proc->pid, "suid", _proc_read_int, &proc->status);
	kobj_getref(proc);
	__proc_make(proc->pid, "gid", _proc_read_int, &proc->status);
	kobj_getref(proc);
	__proc_make(proc->pid, "egid", _proc_read_int, &proc->status);
	kobj_getref(proc);
	__proc_make(proc->pid, "sgid", _proc_read_int, &proc->status);
	kobj_getref(proc);
	__proc_make(proc->pid, "brk", _proc_read_int, &proc->brk);
	kobj_getref(proc);
	__proc_make(proc->pid, "maps", _proc_read_maps, proc);

}

sysret_t sys_fork(void *frame)
{
	struct thread *thread = kobj_allocate(&kobj_thread);
	struct process *proc = kobj_allocate(&kobj_process);

	copy_process(current_thread->process, proc);
	copy_thread(current_thread, thread);
	process_copy_mappings(current_thread->process, proc);
	process_attach_thread(proc, thread);
	__create_proc_entries(proc);
	if(current_thread->process == kernel_process) {
		thread->user_tls_base = (void *)process_allocate_user_tls(proc);
		arch_thread_create(thread, (uintptr_t)&arch_thread_fork_entry, frame);
	} else {
		memcpy((void *)((uintptr_t)thread->kernel_tls_base + KERNEL_STACK_SIZE/2), frame, sizeof(struct arch_exception_frame));
		thread->user_tls_base = current_thread->user_tls_base;
		arch_thread_create(thread, (uintptr_t)&arch_thread_fork_entry, (void *)((uintptr_t)thread->kernel_tls_base + KERNEL_STACK_SIZE/2));

		process_copy_files(current_thread->process, proc);
	}

	thread->state = THREADSTATE_RUNNING;
	processor_add_thread(select_processor(), thread);
	sysret_t ret = proc->pid;
	kobj_putref(proc);
	return ret;
}

struct pt_regs {
	int _placeholder;
};
#define CLONE_CHILD_SETTID   0x01000000
#define CLONE_PARENT_SETTID  0x00100000
#define CLONE_CHILD_CLEARTID 0x00200000

long sys_clone(unsigned long flags, void *child_stack, void *ptid, void *ctid, struct pt_regs *regs, void *frame)
{
	struct thread *thread = kobj_allocate(&kobj_thread);
	copy_thread(current_thread, thread);
	process_attach_thread(current_thread->process, thread);
	
	memcpy((void *)((uintptr_t)thread->kernel_tls_base + KERNEL_STACK_SIZE/2), frame, sizeof(struct arch_exception_frame));
	thread->user_tls_base = (void *)((uintptr_t)child_stack - USER_TLS_SIZE);
	thread->arch.fs = (uintptr_t)regs; //TODO: arch-specific
	if(flags & CLONE_CHILD_SETTID) {
		thread->set_child_tid = ctid;
	}
	if(flags & CLONE_PARENT_SETTID) {
		*(int *)ptid = (int)thread->tid;
	}
	if(flags & CLONE_CHILD_CLEARTID) {
		thread->clear_child_tid = ctid;
	}
	arch_thread_create(thread, (uintptr_t)&arch_thread_fork_entry, (void *)((uintptr_t)thread->kernel_tls_base + KERNEL_STACK_SIZE/2));

	thread->state = THREADSTATE_RUNNING;
	processor_add_thread(select_processor(), thread);
	sysret_t ret = thread->tid;
	return ret;
}

long sys_set_tid_address(_Atomic int *addr)
{
	current_thread->clear_child_tid = addr;
	return current_thread->tid;
}

_Noreturn void sys_do_exit(int code)
{
	if(current_thread->clear_child_tid != NULL) {
		*(_Atomic int *)current_thread->clear_child_tid = 0;
		sys_futex(current_thread->clear_child_tid, FUTEX_WAKE, 1, NULL, NULL, 0);
	}

	linkedlist_remove(&current_thread->process->threads, &current_thread->proc_entry);
	kobj_putref(current_thread);
	if(current_thread->process->threads.count == 0) {
		process_exit(current_thread->process, code);
	}
	kobj_putref(current_thread->process);
	current_thread->process = NULL;

	(void)code;

	thread_exit(current_thread);
}

void sys_exit(int code)
{
	current_thread->flags |= THREAD_EXIT;
	current_thread->exit_code = code;
}

void sys_exit_group(int code)
{
	__linkedlist_lock(&current_thread->process->threads);
	struct linkedentry *entry;
	for(entry = linkedlist_iter_start(&current_thread->process->threads);
			entry != linkedlist_iter_end(&current_thread->process->threads);
			entry = linkedlist_iter_next(entry)) {
		struct thread *t = linkedentry_obj(entry);
		t->exit_code = code;
		t->flags |= THREAD_EXIT;
	}
	__linkedlist_unlock(&current_thread->process->threads);
}

long sys_gettid(void)
{
	return current_thread->tid;
}

sysret_t sys_kill(int pid, int sig)
{
	struct process *target = process_get_by_pid(pid);
	if(!target)
		return -ESRCH;
	process_send_signal(target, sig);
	kobj_putref(target);
	return 0;
}

sysret_t sys_sigaction(int sig, const struct sigaction *act, struct sigaction *old)
{
	struct process *proc = current_thread->process;
	if(sig <= 0 || sig > _NSIG)
		return -EINVAL;
	spinlock_acquire(&proc->signal_lock);
	if(old)
		memcpy(old, &proc->actions[sig], sizeof(*old));
	if(act)
		memcpy(&proc->actions[sig], act, sizeof(*act));
	spinlock_release(&proc->signal_lock);
	return 0;
}

sysret_t sys_sigprocmask(int how, const sigset_t *set, sigset_t *oset)
{
	if(how < 0)
		return -EINVAL;
	sigset_t old;
	memcpy(&old, &current_thread->sigmask, sizeof(old));

	if(set) {
		switch(how) {
			case SIG_BLOCK:
				sigorset(&current_thread->sigmask, &current_thread->sigmask, set);
				break;
			case SIG_UNBLOCK:
				for(int i=0;i<=_NSIG;i++) {
					if(sigismember(set, i))
						sigdelset(&current_thread->sigmask, i);
				}
				break;
			default:
				memcpy(&current_thread->sigmask, set, sizeof(*set));
				break;
		}
	}
	if(oset)
		memcpy(oset, &old, sizeof(*oset));
	return 0;
}

