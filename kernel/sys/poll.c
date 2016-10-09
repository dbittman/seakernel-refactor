#include <syscall.h>
#include <timer.h>
#include <signal.h>
#include <thread.h>
#include <blocklist.h>
#include <arena.h>
#include <system.h>
#include <errno.h>
#include <fs/sys.h>
#include <sys.h>
#include <printk.h>
#include <file.h>
static int __cleanup_points(struct pollpoint *points, int num)
{
	int ready = 0;
	for(int i=0;i<num;i++) {
		if(points[i].file) {
			if(*points[i].revents != 0)
				ready++;
			kobj_putref(points[i].file);
		}
	}
	mm_virtual_deallocate((uintptr_t)points);
	return ready;
}

static bool __poll_init(struct pollpoint *points, int nfds, time_t timeout)
{
	bool ready = false;
	for(int i=0;i<nfds;i++) {
		if(points[i].file) {
			*points[i].revents = 0;
			if(points[i].file->ops->poll) {
				for(int b=0;b<NUM_POLL_BLOCKS;b++) {
					blockpoint_create(&points[i].bps[b], timeout > 0 ? BLOCK_TIMEOUT : 0, timeout);
				}
				ready |= points[i].file->ops->poll(points[i].file, &points[i]);
			} else {
				*points[i].revents = points[i].events & (POLLIN | POLLOUT);
				points[i].events = 0;
				ready = true;
			}
		}
	}
	return ready;
}

#define POLL_READY 1
#define POLL_INTR  2
static int __poll_cleanup(struct pollpoint *points, int nfds)
{
	bool ready = false;
	bool intr = false;
	for(int i=0;i<nfds;i++) {
		if(points[i].file) {
			int b = POLL_BLOCK_READ;
			int event = POLLIN;
			for(;b < NUM_POLL_BLOCKS;b++, event <<= 1) {
				if(points[i].events & event) {
					printk(":: %x %d %d\n", event, b, points[i].file->devtype);
					enum block_result res = blockpoint_cleanup(&points[i].bps[b]);
					switch(res) {
						case BLOCK_RESULT_UNBLOCKED:
							*points[i].revents |= event;
							ready = true;
							break;
						case BLOCK_RESULT_INTERRUPTED:
							intr = true;
							break;
						default: break;
					}
				}
			}
		}
	}

	return intr ? POLL_INTR : (ready ? POLL_READY : 0);
}

sysret_t sys_ppoll(struct pollfd *fds, int nfds, const struct timespec *timeout, const sigset_t *sigmask)
{
	(void)sigmask;
	time_t time = timeout ? timeout->tv_sec * 1000000000 + timeout->tv_nsec : 0; //TODO: don't hardcode this
	time_t end_time = time + time_get_current();
	
	size_t alloc = __round_up_pow2(nfds * sizeof(struct pollpoint));
	struct pollpoint *points = (void *)mm_virtual_allocate(alloc < MM_BUDDY_MIN_SIZE ? MM_BUDDY_MIN_SIZE : alloc, true);

	int count=0;
	for(int i=0;i<nfds;i++) {
		if(fds[i].fd >= 0) {
			struct file *file = process_get_file(fds[i].fd);
			if(file) {
				points[i].file = file;
				points[i].events = fds[i].events;
				points[i].revents = &fds[i].revents;
				count++;
			} else {
				fds[i].revents = POLLNVAL;
			}
		}
	}

	if(count == 0) {
		if(timeout)
			sys_nanosleep(timeout, NULL);
	} else {
		bool ready = false;
		while(!ready) {
			time_t rem_time = end_time - time_get_current();
			if(rem_time < 0) rem_time = 0;

			ready = __poll_init(points, nfds, rem_time);

			if(!ready && (time > 0 || !timeout))
				schedule();

			switch(__poll_cleanup(points, nfds)) {
				case POLL_READY:
					ready = true;
					break;
				case POLL_INTR:
					if(!ready) {
						__cleanup_points(points, nfds);
						return -EINTR;
					}
			}
			if(timeout && time_get_current() >= end_time) {
				ready = true;
			}
		}
	}

	return __cleanup_points(points, nfds);
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


