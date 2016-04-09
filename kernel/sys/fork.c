#include <stdbool.h>
#include <sys.h>
#include <thread.h>
#include <process.h>
#include <slab.h>
#include <processor.h>

static void _fork_entry(void *caller)
{
	arch_thread_usermode_jump((uintptr_t)caller, 0);
}

int sys_fork(uintptr_t caller)
{
	struct thread *thread = kobj_allocate(&kobj_thread);
	struct process *proc = kobj_allocate(&kobj_process);
	process_attach_thread(proc, thread);
	arch_thread_create(thread, (uintptr_t)&_fork_entry, (void *)caller);
	thread->user_tls_base = (void *)process_allocate_user_tls(proc);
	thread->state = THREADSTATE_RUNNING;
	processor_add_thread(current_thread->processor, thread);
	return proc->pid;
}

