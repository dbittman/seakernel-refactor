#include <thread.h>
#include <signal.h>
#include <process.h>
#include <sys.h>
#include <printk.h>
bool thread_send_signal(struct thread *thread, int signal)
{
	bool ret = false;
	spinlock_acquire(&thread->signal_lock);
	if(signal == SIGKILL) {
		thread->signal = SIGKILL;
		printk("Adding signal %d to thread %ld\n", signal, thread->tid);
		if(thread != current_thread)
			thread_unblock(thread);
	} else if(!sigismember(&thread->sigmask, signal)) {
		ret = true;
		sigaddset(&thread->pending_signals, signal);
		printk("Adding signal %d to thread %ld\n", signal, thread->tid);
		if(thread != current_thread)
			thread_unblock(thread);
	}
	spinlock_release(&thread->signal_lock);
	return ret;
}

#include <printk.h>
bool thread_check_status_retuser(struct thread *thread)
{
	if(thread->flags & THREAD_EXIT) {
		sys_do_exit(thread->exit_code);
	}

	bool ret = false;
	if(current_thread && (current_thread->flags & THREAD_RESCHEDULE)) {
		preempt();

		int signal = thread->signal;
		struct process *process = thread->process;
		if(signal != SIGKILL && signal_is_userspace_handler(process->actions[signal].handler)) {
			/* user-handled */
			ret = true;
		} else if(signal == SIGKILL || process->actions[signal].handler == SIG_DFL) {
			/* default actions */
			thread->signal = 0;
			switch(signal) {
				case SIGABRT: case SIGALRM: case SIGBUS: case SIGFPE: case SIGHUP: case SIGILL:
				case SIGINT: case SIGKILL: case SIGPIPE: case SIGQUIT: case SIGSEGV:
				case SIGTERM: case SIGUSR1: case SIGUSR2: case SIGPOLL: case SIGPROF:
				case SIGSYS: case SIGTRAP:
					/* kill */
					thread->flags |= THREAD_EXIT;
					break;
				case SIGCONT:
					/* TODO: stopping and continuing */
					break;
				case SIGSTOP: case SIGTSTP: case SIGTTIN: case SIGTTOU:

					break;
			}
		}
	}

	return ret;
}

