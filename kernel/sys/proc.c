#include <process.h>
#include <thread.h>
#include <errno.h>

sysret_t sys_getpid(void)
{
	return current_thread->process->pid;
}

sysret_t sys_getppid(void)
{
	spinlock_acquire(&current_thread->process->lock);
	sysret_t ret = current_thread->process->parent->pid;
	spinlock_release(&current_thread->process->lock);
	return ret;
}

sysret_t sys_getuid(void)
{
	return current_thread->process->uid;
}

sysret_t sys_getgid(void)
{
	return current_thread->process->gid;
}

sysret_t sys_geteuid(void)
{
	return current_thread->process->euid;
}

sysret_t sys_getegid(void)
{
	return current_thread->process->egid;
}

sysret_t sys_setuid(int id)
{
	if(current_thread->process->euid == 0) {
		current_thread->process->uid = id;
		current_thread->process->euid = id;
		current_thread->process->suid = id;
	} else if(id == current_thread->process->uid
			|| id == current_thread->process->suid) {
		current_thread->process->euid = id;
	} else {
		return -EPERM;
	}
	return 0;
}

sysret_t sys_setgid(int id)
{
	if(current_thread->process->egid == 0) {
		current_thread->process->gid = id;
		current_thread->process->egid = id;
		current_thread->process->sgid = id;
	} else if(id == current_thread->process->gid
			|| id == current_thread->process->sgid) {
		current_thread->process->egid = id;
	} else {
		return -EPERM;
	}
	return 0;
}

static inline bool _is_ok(int id, bool u)
{
	if(id == -1)
		return true;
	if(u) {
		if(id == current_thread->process->uid
				|| id == current_thread->process->suid
				|| current_thread->process->euid == 0) {
			return true;
		}
	} else {
		if(id == current_thread->process->gid
				|| id == current_thread->process->sgid
				|| current_thread->process->egid == 0) {
			return true;
		}
	}
	return false;
}

sysret_t sys_setresuid(int rid, int eid, int sid)
{
	if(_is_ok(rid, true) && _is_ok(eid, true) && _is_ok(sid, true)) {
		if(rid != -1) current_thread->process->uid = rid;
		if(eid != -1) current_thread->process->euid = eid;
		if(sid != -1) current_thread->process->suid = sid;
		return 0;
	}
	return -EPERM;
}

sysret_t sys_setresgid(int rid, int eid, int sid)
{
	if(_is_ok(rid, false) && _is_ok(eid, false) && _is_ok(sid, false)) {
		if(rid != -1) current_thread->process->gid = rid;
		if(eid != -1) current_thread->process->egid = eid;
		if(sid != -1) current_thread->process->sgid = sid;
		return 0;
	}
	return -EPERM;
}

