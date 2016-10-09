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

/* TODO: this implementation will block if given a bad FD */
sysret_t sys_pselect(int nfds, fd_set *readfds, fd_set *writefds,
		fd_set *errfds, const struct timespec *timeout, const sigset_t *sigmask)
{
	size_t alloc = __round_up_pow2(nfds * sizeof(struct pollfd));
	struct pollfd *pollfds = (void *)mm_virtual_allocate(alloc < MM_BUDDY_MIN_SIZE ? MM_BUDDY_MIN_SIZE : alloc, true);

	for(int i=0;i<nfds;i++) {
		struct pollfd *p = &pollfds[i];
		bool r = readfds ? FD_ISSET(i, readfds) : false;
		bool w = writefds ? FD_ISSET(i, writefds) : false;
		bool e = errfds ? FD_ISSET(i, errfds) : false;
		if(r || w || e) {
			p->fd = i;
			if(r) p->events |= POLLIN;
			if(w) p->events |= POLLOUT;
			if(e) p->events |= POLLERR;
		} else {
			p->fd = -1;
		}
	}

	sysret_t ret = sys_ppoll(pollfds, nfds, timeout, sigmask);

	if(ret != -EINTR) {
		for(int i=0;i<nfds;i++) {
			struct pollfd *p = &pollfds[i];

			if(p->fd >= 0) {
				if(p->revents & POLLNVAL) {
					ret = -EBADF;
				} else {
					if(readfds) {
						FD_CLR(i, readfds);
						if((p->revents & POLLIN) || (p->revents & POLLHUP))
							FD_SET(i, readfds);
					}
					if(writefds) {
						FD_CLR(i, writefds);
						if((p->revents & POLLOUT) || (p->revents & POLLHUP))
							FD_SET(i, writefds);
					}
					if(errfds) {
						FD_CLR(i, errfds);
						if(p->revents & POLLERR)
							FD_SET(i, errfds);
					}
				}
			}
		}
	}

	mm_virtual_deallocate((uintptr_t)pollfds);
	return ret;
}

sysret_t sys_select(int nfds, fd_set *readfds, fd_set *writefds,
		fd_set *errfds, struct timeval *timeout)
{
	struct timespec time;
	time.tv_sec = timeout ? timeout->tv_sec : 0;
	time.tv_nsec = timeout ? timeout->tv_usec * 1000 : 0;

	time_t __time = timeout ? timeout->tv_sec * 1000000000 + timeout->tv_usec * 1000 : 0;
	time_t end_time = __time + time_get_current();
	sysret_t ret = sys_pselect(nfds, readfds, writefds, errfds, timeout ? &time : NULL, NULL);
	time_t rem_time = end_time - time_get_current();
	if(rem_time < 0) rem_time = 0;

	if(timeout) {
		timeout->tv_sec = rem_time / 1000000000;
		timeout->tv_usec = rem_time % 1000000000;
	}
	return ret;
}

