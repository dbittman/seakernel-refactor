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
	child->priority = parent->priority;
	parent->priority = (parent->priority / 2) + 1;
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

ssize_t _proc_read_maps(void *data, int rw, size_t off, size_t len, char *buf)
{
	if(rw != 0)
		return -EINVAL;
	struct process *proc = data;
	size_t current = 0;
	/* we're taking map_lock so we can't afford page faults. Pre-fault on all the memory. */
	for(uintptr_t tmp = (uintptr_t)buf;tmp < (uintptr_t)buf + len; tmp += arch_mm_page_size(0)) {
		mmu_mappings_handle_fault(current_thread->process, tmp, FAULT_ERROR_PERM | FAULT_ERROR_PRES | FAULT_WRITE);
	}
	PROCFS_PRINTF(off, len, buf, current,
			"      REGION START -         REGION END: L   FL   EWR - FILENAME\n");
	mutex_acquire(&proc->map_lock);
	
	for(int level = 0;level < MMU_NUM_PAGESIZE_LEVELS;level++) {
		struct linkedentry *entry;
		for(entry = linkedlist_iter_start(&proc->maps[level]);
				entry != linkedlist_iter_end(&proc->maps[level]);
				entry = linkedlist_iter_next(entry)) {
			struct map_region *reg = linkedentry_obj(entry);
			char name[256];
			memset(name, 0, sizeof(name));
			if(reg->file->dirent)
				strncpy(name, reg->file->dirent->name, reg->file->dirent->namelen);
			else
				strncpy(name, "[zero]", 255);
			PROCFS_PRINTF(off, len, buf, current,
					"%16lx - %16lx: %1.1d %2.2lx %3.3b - %s\n",
					reg->start, reg->start + reg->length, mm_get_pagelevel(reg->psize), reg->flags, reg->prot, name);
		}
	}

	mutex_release(&proc->map_lock);
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

	struct processor *processor = select_processor();
	spinlock_acquire(&processor->schedlock);
	thread->state = THREADSTATE_RUNNING;
	processor_add_thread(processor, thread);
	spinlock_release(&processor->schedlock);
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

	struct processor *processor = select_processor();
	spinlock_acquire(&processor->schedlock);
	thread->state = THREADSTATE_RUNNING;
	processor_add_thread(processor, thread);
	spinlock_release(&processor->schedlock);
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

