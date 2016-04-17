#include <thread.h>
#include <timer.h>
#include <errno.h>
#include <printk.h>
static void __timeout(void *data)
{
	struct thread *thread = data;
	thread_unblock(thread);
}

sysret_t sys_nanosleep(const struct timespec *req, struct timespec *rem)
{
	if(!req)
		return -EINVAL;
	time_t time = req->tv_sec * 1000000 + req->tv_nsec / 1000;
	time_t oldtime = time_get_current();
	if(time < 0)
		return -EINVAL;
	struct timer timer;
	thread_prepare_sleep();
	time /= 4; //TODO: what
	timer_add(&timer, TIMER_MODE_ONESHOT, time / MICROSECONDS_PER_TICK, __timeout, current_thread);
	schedule();
	timer_remove(&timer);
	thread_wakeup();
	time_t elapsed = time_get_current() - oldtime;

	if(rem) {
		rem->tv_sec = (time - elapsed) / 1000000000;
		rem->tv_nsec = (time - elapsed) % 1000000000;
		if(rem->tv_sec < 0 || rem->tv_nsec < 0)
			rem->tv_sec = rem->tv_nsec = 0;
	}
	return 0;
}

