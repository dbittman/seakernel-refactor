#include <thread.h>
#include <timer.h>
#include <errno.h>
#include <printk.h>
#include <fs/path.h>
#include <fs/inode.h>
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

sysret_t sys_getitimer(int which, struct itimerval *cur)
{
	if(which > 2 || which < 0 || cur == NULL)
		return -EINVAL;
	memcpy(cur, &current_thread->timers[which].timer, sizeof(*cur));
	time_t rem = current_thread->timers[which].timer.ticks * MICROSECONDS_PER_TICK;
	cur->it_value.tv_sec = rem / 1000000;
	cur->it_value.tv_usec = rem % 1000000;
	return 0;
}

static void _itimer_timeout(void *data)
{
	struct thread_timer *timer = data;
	printk("timer out %ld\n", timer->thread->tid);

	time_t value = timer->interval.tv_sec * 1000000 + timer->interval.tv_usec;
	
	if(value > 0) {
		timer_add(&timer->timer, TIMER_MODE_ONESHOT, value / MICROSECONDS_PER_TICK, _itimer_timeout, timer);
	}

	thread_send_signal(timer->thread, timer->sig);
}

static void __setup_timer(int which)
{
	struct thread_timer *timer = &current_thread->timers[which];
	if(atomic_exchange(&timer->sig, 0) != 0)
		timer_remove(&timer->timer);

	time_t value = timer->value.tv_sec * 1000000 + timer->value.tv_usec;
	value /= 4; //TODO: what
	if(value == 0)
		return;
	timer->thread = current_thread;
	timer_add(&timer->timer, TIMER_MODE_ONESHOT, value / MICROSECONDS_PER_TICK, _itimer_timeout, timer);
	timer->sig = SIGALRM;
}

sysret_t sys_setitimer(int which, const struct itimerval *_new, struct itimerval *old)
{
	if(which > 2 || which < 0)
		return -EINVAL;
	/* _new and old can be aliases :( */
	struct itimerval new;
	memcpy(&new, _new, sizeof(new));
	if(old) {
		memcpy(old, &current_thread->timers[which].timer, sizeof(*old));
		time_t rem = current_thread->timers[which].timer.ticks * MICROSECONDS_PER_TICK;
		old->it_value.tv_sec = rem / 1000000;
		old->it_value.tv_usec = rem % 1000000;
	}
	memcpy(&current_thread->timers[which], &new, sizeof(new));
	__setup_timer(which);
	return 0;
}

#define CLOCK_REALTIME           0
sysret_t sys_clock_getres(int id, struct timespec *res)
{
	(void)id;
	res->tv_sec = 1;
	res->tv_nsec = 0;
	return 0;
}

sysret_t sys_clock_gettime(int id, struct timespec *res)
{
	switch(id) {
		case CLOCK_REALTIME:
#if FEATURE_SUPPORTED_GETTIME
			res->tv_sec = arch_time_getepoch();
			res->tv_nsec = 0;
#else
			return -ENOTSUP;
#endif
		default:
			return -ENOTSUP;
	}
	return 0;
}

#include <process.h>
sysret_t sys_utimes(const char *filename, const struct timeval times[2])
{
	struct inode *node;
	int err = fs_path_resolve(filename, NULL, 0, 0, NULL, &node);
	if(err < 0)
		return err;

	err = 0;
	if(times == NULL) {
		if(inode_check_perm(node, PERM_WRITE)) {
			node->atime = arch_time_getepoch();
			node->mtime = arch_time_getepoch();
		} else {
			err = -EPERM;
		}
	} else {
		if(node->uid == current_thread->process->euid || current_thread->process->euid == 0) {
			node->atime = times[0].tv_sec;
			node->mtime = times[1].tv_sec;
		} else {
			err = -EPERM;
		}
	}
	inode_put(node);
	return err;
}

