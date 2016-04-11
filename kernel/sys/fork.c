#include <stdbool.h>
#include <sys.h>
#include <thread.h>
#include <process.h>
#include <slab.h>
#include <processor.h>

static void _fork_entry(void *a)
{
	(void)a;
	if(current_thread->fork_entry >= USER_REGION_END)
		((void (*)(void))current_thread->fork_entry)();
	else
		arch_thread_usermode_jump(current_thread->fork_entry, current_thread->fork_sp);
}

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
}

int sys_fork(uintptr_t caller, uintptr_t ustack)
{
	struct thread *thread = kobj_allocate(&kobj_thread);
	struct process *proc = kobj_allocate(&kobj_process);
	copy_process(current_thread->process, proc);
	process_copy_mappings(current_thread->process, proc);
	process_attach_thread(proc, thread);
	thread->fork_sp = ustack;
	thread->fork_entry = caller;
	arch_thread_create(thread, (uintptr_t)&_fork_entry, NULL);
	if(current_thread->process == kernel_process)
		thread->user_tls_base = (void *)process_allocate_user_tls(proc);
	else
		thread->user_tls_base = current_thread->user_tls_base;
	thread->state = THREADSTATE_RUNNING;
	processor_add_thread(current_thread->processor, thread);
	return proc->pid;
}

