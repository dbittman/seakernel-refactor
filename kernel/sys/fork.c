#include <stdbool.h>
#include <sys.h>
#include <thread.h>
#include <process.h>
#include <slab.h>
#include <processor.h>
#include <string.h>
#include <printk.h>
#include <errno.h>

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

_Noreturn void sys_do_exit(int code)
{
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

long sys_gettid(void)
{
	return current_thread->tid;
}

sysret_t sys_kill(int pid, int sig)
{
	struct process *target = process_get_by_pid(pid);
	if(!target)
		return -ESRCH;
	(void)sig;
	__linkedlist_lock(&target->threads);
	struct linkedentry *entry;
	for(entry = linkedlist_iter_start(&target->threads); entry != linkedlist_iter_end(&target->threads);
			entry = linkedlist_iter_next(entry)) {
		struct thread *thread = linkedentry_obj(entry);
		if(thread_send_signal(thread, sig))
			break;
	}
	__linkedlist_unlock(&target->threads);
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
	memcpy(&proc->actions[sig], act, sizeof(*act));
	(void)act;
	(void)old;
	spinlock_release(&proc->signal_lock);
	return 0;
}

sysret_t sys_sigprocmask(int how, const sigset_t *set, sigset_t *oset)
{
	if(how < 0)
		return -EINVAL;
	sigset_t old;
	memcpy(&old, &current_thread->sigmask, sizeof(old));

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
	if(oset)
		memcpy(oset, &old, sizeof(*oset));
	return 0;
}

