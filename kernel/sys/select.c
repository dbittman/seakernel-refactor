#include <file.h>
#include <timer.h>
#include <thread.h>
#include <process.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <blocklist.h>
#include <arena.h>
#include <printk.h>
#include <processor.h>
#include <fs/sys.h>

struct select_blockpoint {
	struct blockpoint bp;
	bool blocked, active, ready, interrupted;
	int type;
};

static void _select_closeall(struct file **files, int num)
{
	for(int i=0;i<num;i++) {
		if(files[i])
			kobj_putref(files[i]);
	}
}

static struct file **_select_openall(struct arena *arena, int nfds, fd_set *readfds, fd_set *writefds, fd_set *errfds)
{
	struct file **files = arena_allocate(arena, nfds * sizeof(struct file *));
	for(int fd = 0;fd < nfds;fd++) {
		if((readfds && FD_ISSET(fd, readfds))
				|| (writefds && FD_ISSET(fd, writefds))
				|| (errfds && FD_ISSET(fd, errfds))) {
			if(!(files[fd] = process_get_file(fd))) {
				_select_closeall(files, fd);
				return NULL;
			}
		} else {
			files[fd] = NULL;
		}
	}
	return files;
}

struct select_blockpoint *init_blockpoints(struct arena *arena, int nfds, fd_set *rset, fd_set *wset, fd_set *eset)
{
	struct select_blockpoint *bps = arena_allocate(arena, 3 * nfds * sizeof(struct select_blockpoint));
	memset(bps, 0, 3 * nfds * sizeof(struct select_blockpoint));
	for(int i=0;i<nfds;i++) {
		if(rset && FD_ISSET(i, rset)) {
			bps[i * 3].active = true;
			bps[i * 3].type = SEL_READ;
		}
		if(wset && FD_ISSET(i, wset)) {
			bps[i * 3 + 1].active = true;
			bps[i * 3 + 1].type = SEL_WRITE;
		}
		if(eset && FD_ISSET(i, eset)) {
			bps[i * 3 + 2].active = true;
			bps[i * 3 + 2].type = SEL_ERROR;
		}
	}
	return bps;
}

bool _select_start_blocking(struct file **files, int nfds, struct select_blockpoint *blocks, time_t timeout_nsec, bool do_timeout)
{
	bool any = false;
	for(int fd=0;fd < nfds * 3;fd++) {
		struct select_blockpoint *bp = &blocks[fd];
		if(!bp->active)
			continue;

		int sel = 1;
		bp->interrupted = false;
		blockpoint_create(&bp->bp, do_timeout ? BLOCK_TIMEOUT : 0, timeout_nsec / 1000);
		bp->blocked = !(do_timeout && timeout_nsec == 0);
		if(files[fd / 3]->ops->select)
			sel = files[fd / 3]->ops->select(files[fd / 3], bp->type, (do_timeout && timeout_nsec == 0) ? NULL : &bp->bp);
		else
			bp->blocked = false;

		bp->ready = false;
		if(sel > 0) {
			bp->ready = true;
			any = true;
		} else if(sel < 0) {
			bp->blocked = false;
		}
	}
	return any;
}

bool _select_cleanup_blocking(int nfds, struct select_blockpoint *blocks)
{
	bool any = false;
	for(int fd=0;fd<nfds * 3;fd++) {
		struct select_blockpoint *bp = &blocks[fd];
		if(!bp->active || !bp->blocked)
			continue;

		enum block_result res = blockpoint_cleanup(&bp->bp);

		switch(res) {
			case BLOCK_RESULT_BLOCKED:
			case BLOCK_RESULT_TIMEOUT:
				break;
			case BLOCK_RESULT_INTERRUPTED:
				bp->interrupted = true;
				break;
			case BLOCK_RESULT_UNBLOCKED:
				bp->ready = true;
				any = true;
				break;
		}
	}
	assert(current_thread->state == THREADSTATE_RUNNING);
	assert(current_thread->processor->preempt_disable == 0);
	return any;
}

int _select_update_fds(struct select_blockpoint *blocks, int nfds, fd_set *read, fd_set *write, fd_set *err)
{
	int ret = 0;
	fd_set *sets[3] = { read, write, err };
	for(int fd=0;fd<nfds*3;fd++) {
		struct select_blockpoint *bp = &blocks[fd];
		if(!bp->active)
			continue;

		FD_CLR(fd / 3, sets[fd % 3]);
		if(bp->ready) {
			FD_SET(fd / 3, sets[fd % 3]);
			if(ret >= 0)
				ret++;
		} else if(bp->interrupted) {
			ret = -EINTR;
		}
	}
	return ret;
}

sysret_t _do_select(int nfds, fd_set *readfds, fd_set *writefds,
		fd_set *errfds, const struct timespec *timeout, const sigset_t *sigmask, struct timespec *remaining)
{
	(void)sigmask; //TODO:
	time_t time = timeout ? timeout->tv_sec * 1000000000 + timeout->tv_nsec : 0;
	time_t end_time = time + time_get_current();
	int ret = 0;
	struct arena arena;
	bool timed_out = false, any_ready = false;
	arena_create(&arena);
	struct file **files = _select_openall(&arena, nfds, readfds, writefds, errfds);
	if(files == NULL) {
		goto out_arena;
	}

	struct select_blockpoint *blocks;

	blocks = init_blockpoints(&arena, nfds, readfds, writefds, errfds);

	do {
		time_t rem_time = end_time - time_get_current();
		if(rem_time < 0) rem_time = 0;
		any_ready = _select_start_blocking(files, nfds, blocks, rem_time, timeout != NULL);

		if(!any_ready)
			schedule();

		any_ready |= _select_cleanup_blocking(nfds, blocks);

		if(timeout && time_get_current() >= end_time) {
			timed_out = true;
		}
	} while(!any_ready && !timed_out);

	if(remaining) {
		time_t rem_time = end_time - time_get_current();
		if(rem_time < 0) rem_time = 0;
		remaining->tv_sec = rem_time / 1000000000;
		remaining->tv_nsec = rem_time % 1000000000;
	}

	ret = _select_update_fds(blocks, nfds, readfds, writefds, errfds);

	_select_closeall(files, nfds);

out_arena:
	arena_destroy(&arena);
	return ret;
}

#define POLLIN      0x001
#define POLLOUT     0x004
sysret_t sys_ppoll(struct pollfd *fds, int nfds, const struct timespec *timeout, const sigset_t *sigmask)
{
	(void)fds;
	(void)nfds;
	(void)timeout;
	(void)sigmask;
	for(int i=0;i<nfds;i++) {
		fds[i].revents = POLLIN | POLLOUT;
	}
	return 1;
}

sysret_t sys_poll(struct pollfd *fds, int nfds, int timeout)
{
	struct timespec ts;
	if(timeout >= 0) {
		ts.tv_nsec = (timeout % 1000) * 1000000;
		ts.tv_sec = timeout / 1000;
	}
	return sys_ppoll(fds, nfds, timeout >= 0 ? &ts : NULL, NULL);
}

sysret_t sys_pselect(int nfds, fd_set *readfds, fd_set *writefds,
		fd_set *errfds, const struct timespec *timeout, const sigset_t *sigmask)
{
	return _do_select(nfds, readfds, writefds, errfds, timeout, sigmask, NULL);
}

sysret_t sys_select(int nfds, fd_set *readfds, fd_set *writefds,
		fd_set *errfds, struct timeval *timeout)
{
	struct timespec time;
	time.tv_sec = timeout ? timeout->tv_sec : 0;
	time.tv_nsec = timeout ? timeout->tv_usec * 1000 : 0;
	sysret_t ret = _do_select(nfds, readfds, writefds, errfds, timeout ? &time : NULL, NULL, &time);
	if(timeout) {
		timeout->tv_sec = time.tv_sec;
		timeout->tv_usec = time.tv_nsec / 1000;
	}
	return ret;
}

