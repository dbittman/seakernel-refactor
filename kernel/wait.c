#include <thread.h>
#include <process.h>
#include <arena.h>
#include <blocklist.h>
#include <errno.h>
struct rusage
{
	struct timeval ru_utime;
	struct timeval ru_stime;
	/* linux extentions, but useful */
	long    ru_maxrss;
	long    ru_ixrss;
	long    ru_idrss;
	long    ru_isrss;
	long    ru_minflt;
	long    ru_majflt;
	long    ru_nswap;
	long    ru_inblock;
	long    ru_oublock;
	long    ru_msgsnd;
	long    ru_msgrcv;
	long    ru_nsignals;
	long    ru_nvcsw;
	long    ru_nivcsw;
	/* room for more... */
	long    __reserved[16];
};
struct waiter {
	struct blockpoint bp;
	struct process *proc;
	struct waiter *next;
};

static struct waiter *__wait_start(struct arena *arena, struct process *proc, int *num)
{
	struct waiter *w = arena_allocate(arena, sizeof(struct waiter));
	w->proc = proc;
	w->next = NULL;
	blockpoint_create(&w->bp, 0, 0);
	blockpoint_startblock(&proc->wait, &w->bp);
	if(proc->flags & PROC_STATUS_CHANGED) {
		(*num)++;
	}
	return w;
}

static struct waiter *__init_waiters(struct arena *arena, int pid,
		int *err, int *num)
{
	*num = 0;
	*err = 0;
	struct waiter *root = NULL;
	if(pid > 0) {
		struct process *proc = process_get_by_pid(pid);
		if(proc)
			root = __wait_start(arena, proc, num);
	} else {
		struct hashiter iter;
		kobj_idmap_lock(&processids);
		for(kobj_idmap_iter_init(&processids, &iter);
				!kobj_idmap_iter_done(&iter);
				kobj_idmap_iter_next(&iter)) {
			struct process *proc = kobj_idmap_iter_get(&iter);

			if((pid < -1 && proc->pgroupid == -pid)
					|| (pid == -1 && proc->parent == current_thread->process)
					|| (pid == 0 && proc->pgroupid == current_thread->process->pgroupid)) {
				struct waiter *w = __wait_start(arena, kobj_getref(proc), num);
				if(root) {
					w->next = root;
				}
				root = w;
			}
		}
		kobj_idmap_unlock(&processids);
	}
	if(!root)
		*err = -ECHILD;
	return root;
}

static int __read_status(struct process *proc, int options, int *status)
{
	if(atomic_fetch_and(&proc->flags, ~PROC_EXITED) & PROC_EXITED) {
		kobj_idmap_delete(&processids, proc, &proc->pid);
	}

	if(!(atomic_fetch_and(&proc->flags, ~PROC_STATUS_CHANGED) & PROC_STATUS_CHANGED))
		return 0;

	int st = proc->status;

	if(WIFSTOPPED(st) && !(options & WUNTRACED))
		return 0;
	if(WIFCONTINUED(st) && !(options & WCONTINUED))
		return 0;

	*status = st;
	proc->status = 0;
	return proc->pid;
}

static int __cleanup_waiters(struct waiter *root, int options, int *status)
{
	int ret = 0;
	for(struct waiter *w = root;w != NULL;w = w->next) {
		enum block_result res = blockpoint_cleanup(&w->bp);
		if(ret == 0) {
			switch(res) {
				case BLOCK_RESULT_INTERRUPTED:
					ret = -EINTR;
					break;
				case BLOCK_RESULT_UNBLOCKED:
					ret = __read_status(w->proc, options, status);
					break;
				default: break;
			}
		}
		kobj_putref(w->proc);
	}
	return ret;
}

sysret_t wait4(int pid, int *status, int options, struct rusage *usage)
{
	/* TODO: fill out usage */
	(void)usage;
	struct arena arena;
	int err;
	int num; // number of children ready.
restart:
	arena_create(&arena);
	struct waiter *waiters = __init_waiters(&arena, pid, &err, &num);
	if(err) {
		arena_destroy(&arena);
		return err;
	}

	if(num == 0 && !(options & WNOHANG)) {
		schedule();
	}

	int ret = __cleanup_waiters(waiters, options, status);
	arena_destroy(&arena);
	if(ret == 0 && !(options & WNOHANG))
		goto restart;
	return ret;
}

