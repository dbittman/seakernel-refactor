#ifndef __TIMER_H
#define __TIMER_H

#include <arch-timer.h>
#include <lib/linkedlist.h>
#include <stdint.h>
void timer_init(void);
void arch_timer_tick(void);

enum timer_mode {
	TIMER_MODE_ONESHOT,
	TIMER_MODE_PERIODIC,
};

#define MICROSECONDS_PER_TICK 100

#define ONE_SECOND 100*1000*1000

struct timer {
	uint64_t initial_ticks;
	_Atomic int64_t ticks;
	_Atomic int level;
	enum timer_mode mode;
	void (*call)(void *);
	void *data;
	struct linkedentry node;
};

void timer_add_locked(struct timer *timer, enum timer_mode mode, uint64_t ticks,
		void (*fn)(void *), void *data, bool locked);

#define timer_add(a,b,c,d,e) timer_add_locked(a,b,c,d,e,false)
#define timer_readd(a,b,c,d,e) timer_add_locked(a,b,c,d,e,true)

void timer_remove(struct timer *timer);


typedef long time_t;
typedef long suseconds_t;
struct timespec { time_t tv_sec; long tv_nsec; };
struct timeval { time_t tv_sec; suseconds_t tv_usec; };

time_t time_get_current(void);
int64_t arch_time_getepoch(void);
uint64_t timer_get_counter(void);
#endif

