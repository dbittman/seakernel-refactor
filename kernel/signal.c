#include <thread.h>
#include <signal.h>
#include <process.h>
#include <sys.h>
#include <printk.h>
#include <interrupt.h>
bool thread_send_signal(struct thread *thread, int signal)
{
	bool ret = false;
	spinlock_acquire(&thread->signal_lock);
	if(signal == SIGKILL || signal == SIGSTOP) {
		thread->signal = SIGKILL;
		if(thread != current_thread)
			thread_unblock(thread);
	} else if(!sigismember(&thread->sigmask, signal)) {
		ret = true;
		sigaddset(&thread->pending_signals, signal);
		if(thread != current_thread)
			thread_unblock(thread);
	}
	thread->flags |= THREAD_RESCHEDULE;
	spinlock_release(&thread->signal_lock);
	return ret;
}

void process_send_signal(struct process *target, int sig)
{
	__linkedlist_lock(&target->threads);
	struct linkedentry *entry;
	for(entry = linkedlist_iter_start(&target->threads); entry != linkedlist_iter_end(&target->threads);
			entry = linkedlist_iter_next(entry)) {
		struct thread *thread = linkedentry_obj(entry);
		if(thread_send_signal(thread, sig))
			break;
	}
	__linkedlist_unlock(&target->threads);
}

bool thread_check_status_retuser(struct thread *thread)
{
	bool ret = false;
	if(current_thread && ((current_thread->flags & THREAD_RESCHEDULE) || thread->signal)) {
		preempt();

		int signal = thread->signal;
		if(signal) {
			struct process *process = thread->process;
			if(signal != SIGKILL && signal != SIGSTOP && signal_is_userspace_handler(process->actions[signal].handler)) {
				/* user-handled */
				ret = true;
			} else if(signal == SIGKILL || signal == SIGSTOP || process->actions[signal].handler == SIG_DFL) {
				/* default actions */
				thread->signal = 0;
				bool core = false;
				switch(signal) {
					case SIGSEGV: case SIGQUIT: case SIGFPE: case SIGABRT: case SIGILL:
						core = true;
						/* fall through */
					case SIGALRM: case SIGBUS: case SIGHUP: 
					case SIGINT: case SIGKILL: case SIGPIPE: 
					case SIGTERM: case SIGUSR1: case SIGUSR2: case SIGPOLL: case SIGPROF:
					case SIGSYS: case SIGTRAP:
						/* kill */
						thread->flags |= THREAD_EXIT;
						thread->process->status = process_make_status(0, signal, false, core);
						thread->process->flags |= PROC_STATUS_CHANGED;
						break;
					case SIGCONT:
						thread->process->status = 0xffff;
						thread->process->flags |= PROC_STATUS_CHANGED;
						/* TODO: stopping and continuing */
						break;
					case SIGSTOP: case SIGTSTP: case SIGTTIN: case SIGTTOU:
						thread->process->status = 0x7f;
						thread->process->flags |= PROC_STATUS_CHANGED;
						break;
				}
				blocklist_unblock_all(&thread->process->wait);
			} else if(process->actions[signal].handler == SIG_IGN) {
				thread->signal = 0;
			}
		}
	}

	if(thread->flags & THREAD_EXIT) {
		arch_interrupt_set(1);
		sys_do_exit(thread->exit_code);
	}

	return ret;
}

