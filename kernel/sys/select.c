#include <file.h>
#include <timer.h>
#include <thread.h>
#include <process.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <blocklist.h>

sysret_t _do_select(int nfds, fd_set *readfds, fd_set *writefds,
		fd_set *errfds, const struct timespec *timeout, const sigset_t *sigmask, struct timespec *remaining)
{
	(void)remaining;
	(void)sigmask; //TODO
	int ret = 0;
	bool timed_out = false;
	long time = timeout->tv_sec * 1000000 + timeout->tv_nsec / 1000;
	struct bp_data {
		struct blockpoint bp;
		bool used;
	} bps[3][nfds];
	struct file *files[nfds];
	memset(files, 0, sizeof(files));
	for(int i=0;i<nfds;i++) {
		bps[0][i].used = bps[1][i].used = bps[2][i].used = false;
		if(FD_ISSET(i, readfds) || FD_ISSET(i, writefds) || FD_ISSET(i, errfds)) {
			files[i] = process_get_file(i);
			if(!files[i]) {
				ret = -EBADF;
				goto out_close_all;
			}
		}
	}

	while(ret == 0 && !timed_out) {
		for(int i=0;i<nfds;i++) {
			if(FD_ISSET(i, readfds) && files[i]->ops->select) {
				FD_CLR(i, readfds);
				blockpoint_create(&bps[0][i].bp, timeout ? BLOCK_TIMEOUT : 0, time);
				bps[0][i].used = true;
				if(files[i]->ops->select(files[i], SEL_READ, &bps[0][i].bp)) {
					FD_SET(i, readfds);
					ret++;
				}
			} else if(FD_ISSET(i, readfds)) {
				ret++;
			}
			if(FD_ISSET(i, writefds) && files[i]->ops->select) {
				FD_CLR(i, writefds);
				blockpoint_create(&bps[1][i].bp, timeout ? BLOCK_TIMEOUT : 0, time);
				bps[1][i].used = true;
				if(files[i]->ops->select(files[i], SEL_WRITE, &bps[1][i].bp)) {
					FD_SET(i, writefds);
					ret++;
				}
			} else if(FD_ISSET(i, writefds)) {
				ret++;
			}

			if(FD_ISSET(i, errfds) && files[i]->ops->select) {
				FD_CLR(i, errfds);
				if(files[i]->ops->select(files[i], SEL_ERROR, NULL)) {
					FD_SET(i, errfds);
					ret++;
				}
			}
		}

		if(ret != 0) {
			schedule();
		}

		for(int dir=0;dir<3;dir++) {
			for(int i=0;i<nfds;i++) {
				if(bps[dir][i].used) {
					blockpoint_unblock(&bps[dir][i].bp);
					enum block_result res = blockpoint_cleanup(&bps[dir][i].bp);
					switch(res) {
						case BLOCK_RESULT_TIMEOUT:
							timed_out = true;
							break;
						case BLOCK_RESULT_INTERRUPTED:
							/* TODO */
							break;
						default:
							if(dir == 0) {
								if(!FD_ISSET(i, readfds)) {
									FD_SET(i, readfds);
									ret++;
								}
							} else if(dir == 1) {
								if(!FD_ISSET(i, writefds)) {
									FD_SET(i, writefds);
									ret++;
								}
							} else {
								if(!FD_ISSET(i, writefds)) {
									FD_SET(i, writefds);
									ret++;
								}
							}
							break;
					}
				}
			}
		}
	}

out_close_all:
	for(int j=0;j<nfds;j++) {
		if(files[j])
			kobj_putref(files[j]);
	}
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

