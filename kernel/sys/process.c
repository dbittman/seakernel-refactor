#include <sys.h>
#include <process.h>
#include <thread.h>
#include <errno.h>

int sys_setsid(void)
{
	if(current_thread->process->pgroupid == current_thread->process->pid)
		return -EPERM;

	current_thread->process->seshid = current_thread->process->seshid;
	current_thread->process->pgroupid = current_thread->process->pid;
	return 0;
}

sysret_t sys_setgroups(size_t size, const int *list)
{
	(void)size;
	(void)list;
	// TODO
	return 0;
}

int sys_setpgid(int pid, int pg)
{
	struct process *proc;
	if(pid == 0) {
		proc = kobj_getref(current_thread->process);
	} else {
		proc = process_get_by_pid(pid);
	}
	if(!proc)
		return -ESRCH;
	if(proc->pid == proc->seshid) {
		kobj_putref(proc);
		return -EPERM;
	}
	/* TODO: security checks */
	if(pg == 0) {
		proc->pgroupid = proc->pid;
	} else {
		proc->pgroupid = pg;
	}
	kobj_putref(proc);
	return 0;
}

