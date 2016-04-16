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
sysret_t _do_select(int nfds, fd_set *readfds, fd_set *writefds,
		fd_set *errfds, const struct timespec *timeout, const sigset_t *sigmask, struct timespec *remaining)
{
	(void)remaining;
	(void)sigmask; //TODO
	int ret = 0;
	bool timed_out = false;
	long time = timeout ? timeout->tv_sec * 1000000 + timeout->tv_nsec / 1000 : 0;
	struct arena arena;
	arena_create(&arena);
	
	fd_set ret_readfds;
	fd_set ret_writefds;
	fd_set ret_errfds;
	FD_ZERO(&ret_readfds);
	FD_ZERO(&ret_writefds);
	FD_ZERO(&ret_errfds);

	struct bp_data {
		struct blockpoint bp;
		bool used, blocked;
	} *bps[2];

	bps[0] = arena_allocate(&arena, sizeof(struct bp_data) * nfds);
	bps[1] = arena_allocate(&arena, sizeof(struct bp_data) * nfds);
	struct file **files = arena_allocate(&arena, sizeof(struct file *) * nfds);

	for(int i=0;i<nfds;i++) {
		if((readfds && FD_ISSET(i, readfds))
				|| (writefds && FD_ISSET(i, writefds))
				|| (errfds && FD_ISSET(i, errfds))) {
			files[i] = process_get_file(i);
			if(!files[i]) {
				ret = -EBADF;
				goto out_close_all;
			}
		} else {
			files[i] = NULL;
		}
	}
	while(ret == 0 && !timed_out) {
		int blocked = 0, unblocked = 0;
		for(int i=0;i<nfds;i++) {
			bps[0][i].used = bps[1][i].used = false;
			bps[0][i].blocked = bps[1][i].blocked = false;
			if(readfds && FD_ISSET(i, readfds) && files[i]->ops->select) {
				blockpoint_create(&bps[0][i].bp, timeout ? BLOCK_TIMEOUT : 0, time);
				bps[0][i].used = true;
				bps[0][i].blocked = true;
				blocked++;
				if(files[i]->ops->select(files[i], SEL_READ, &bps[0][i].bp)) {
					FD_SET(i, &ret_readfds);
					ret++;
				}
			} else if(readfds && FD_ISSET(i, readfds)) {
				ret++;
			}
			if(writefds && FD_ISSET(i, writefds) && files[i]->ops->select) {
				blockpoint_create(&bps[1][i].bp, timeout ? BLOCK_TIMEOUT : 0, time);
				bps[1][i].used = true;
				bps[1][i].blocked = true;
				blocked++;
				if(files[i]->ops->select(files[i], SEL_WRITE, &bps[1][i].bp)) {
					FD_SET(i, &ret_writefds);
					ret++;
				}
			} else if(writefds && FD_ISSET(i, writefds)) {
				ret++;
			}

			if(errfds && FD_ISSET(i, errfds) && files[i]->ops->select) {
				if(files[i]->ops->select(files[i], SEL_ERROR, NULL)) {
					FD_SET(i, &ret_errfds);
					ret++;
				}
			}
		}

		if(ret != 0) {
			schedule();
		}

		for(int dir=0;dir<2;dir++) {
			for(int i=0;i<nfds;i++) {
				if(bps[dir][i].blocked) {
					
					enum block_result res = blockpoint_cleanup(&bps[dir][i].bp);
					unblocked++;
					switch(res) {
						case BLOCK_RESULT_TIMEOUT:
							timed_out = true;
							break;
						case BLOCK_RESULT_INTERRUPTED:
							/* TODO */
							break;
						case BLOCK_RESULT_BLOCKED:
							break;
						default:
							if(dir == 0) {
								if(!FD_ISSET(i, &ret_readfds)) {
									FD_SET(i, &ret_readfds);
									ret++;
								}
							} else if(dir == 1) {
								if(!FD_ISSET(i, &ret_writefds)) {
									FD_SET(i, &ret_writefds);
									ret++;
								}
							}
							break;
					}
				}
			}
		}
		assert(current_thread->processor->preempt_disable == 0);
		assert(blocked == unblocked);
	}

	if(readfds) memcpy(readfds, &ret_readfds, sizeof(*readfds));
	if(writefds) memcpy(writefds, &ret_writefds, sizeof(*writefds));
	if(errfds) memcpy(errfds, &ret_errfds, sizeof(*errfds));

out_close_all:
	for(int j=0;j<nfds;j++) {
		if(files[j])
			kobj_putref(files[j]);
	}
	arena_destroy(&arena);
	return ret;
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

