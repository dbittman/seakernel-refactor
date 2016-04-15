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

void timer_add(struct timer *timer, enum timer_mode mode, uint64_t ticks,
		void (*fn)(void *), void *data);
void timer_remove(struct timer *timer);
#endif

