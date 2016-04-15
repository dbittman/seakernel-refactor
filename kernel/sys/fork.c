#include <stdbool.h>
#include <sys.h>
#include <thread.h>
#include <process.h>
#include <slab.h>
#include <processor.h>
#include <string.h>
#include <printk.h>

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

static void copy_thread(struct thread *parent, struct thread *child)
{
	child->time = parent->time;
	memcpy(&child->arch, &parent->arch, sizeof(child->arch));
}

sysret_t sys_fork(void *frame, size_t framelen)
{
	struct thread *thread = kobj_allocate(&kobj_thread);
	struct process *proc = kobj_allocate(&kobj_process);
	copy_process(current_thread->process, proc);
	copy_thread(current_thread, thread);
	process_copy_mappings(current_thread->process, proc);
	process_copy_files(current_thread->process, proc);
	process_attach_thread(proc, thread);
	if(current_thread->process == kernel_process) {
		thread->user_tls_base = (void *)process_allocate_user_tls(proc);
		arch_thread_create(thread, (uintptr_t)&arch_thread_fork_entry, frame);
	} else {
		memcpy((void *)((uintptr_t)thread->kernel_tls_base + KERNEL_STACK_SIZE/2), frame, framelen);
		thread->user_tls_base = current_thread->user_tls_base;
		arch_thread_create(thread, (uintptr_t)&arch_thread_fork_entry, (void *)((uintptr_t)thread->kernel_tls_base + KERNEL_STACK_SIZE/2));
	}

	thread->state = THREADSTATE_RUNNING;
	processor_add_thread(current_thread->processor, thread);
	sysret_t ret = proc->pid;
	kobj_putref(proc);
	return ret;
}

_Noreturn void sys_exit(int code)
{
	kobj_putref(current_thread->process);
	current_thread->process = NULL;

	(void)code;

	thread_exit(current_thread);
}

long sys_gettid(void)
{
	return current_thread->tid;
}

