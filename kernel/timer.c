#include <timer.h>
#include <printk.h>
#include <interrupt.h>
#include <thread.h>
#include <system.h>
#include <spinlock.h>
#include <assert.h>
static _Atomic uint64_t counter = 0;

#define TIMER_LEVELS 8
#define TIMER_LEVEL_SIZE 64
static struct spinlock lock;
static struct linkedlist timerlists[TIMER_LEVELS];

static void __add_timer(struct timer *timer, bool locked)
{
	if(!locked)
		spinlock_acquire(&lock);
	int t = 0;
	uint64_t ticks = timer->ticks;
	for(int i=0;i<TIMER_LEVELS && ticks >= TIMER_LEVEL_SIZE;i++) {
		ticks /= TIMER_LEVEL_SIZE;
		t++;
	}
	linkedlist_insert(&timerlists[t], &timer->node, timer);
	timer->level = t;
	if(!locked)
		spinlock_release(&lock);
}

void timer_add(struct timer *timer, enum timer_mode mode, uint64_t ticks,
		void (*fn)(void *), void *data)
{
	timer->ticks = timer->initial_ticks = ticks;
	timer->mode = mode;
	timer->call = fn;
	timer->data = data;
	__add_timer(timer, false);
}

void timer_remove(struct timer *timer)
{
	spinlock_acquire(&lock);
	if(timer->level != -1) {
		linkedlist_remove(&timerlists[timer->level], &timer->node);
	}
	spinlock_release(&lock);
}

static void check_timers(void)
{
	long mult = 1;
	spinlock_acquire(&lock);
	for(int t=0;
			(likely(mult == 1) || counter % mult == 0) && t < TIMER_LEVELS;
			t++, mult *= TIMER_LEVEL_SIZE) {
		struct linkedentry *next;
		if(timerlists[t].count != 0) {
			for(struct linkedentry *entry = linkedlist_iter_start(&timerlists[t]);
					entry != linkedlist_iter_end(&timerlists[t]);
					entry = next) {
				struct timer *timer = linkedentry_obj(entry);
				next = linkedlist_iter_next(entry);

				timer->ticks -= mult;
				if(timer->ticks <= 0) {
					linkedlist_remove(&timerlists[t], entry);
					timer->level = -1;
					timer->call(timer->data);
					if(timer->mode == TIMER_MODE_PERIODIC) {
						timer->ticks = timer->initial_ticks;
						__add_timer(timer, true);
					}
				} else if(timer->ticks < mult) {
					linkedlist_remove(&timerlists[t], entry);
					timer->level = -1;
					__add_timer(timer, true);
				}
			}
		}
	}
	spinlock_release(&lock);
}

void timer_tick(int flags)
{
	counter++;
	check_timers();
	(void)flags;
	if(counter % 10 == 0) {
		if(min_time == current_thread->time)
			min_time++;
		current_thread->time++;
		if(counter % 100 == 0)
			current_thread->flags |= THREAD_RESCHEDULE;
	}
	arch_timer_tick();
}

void timer_init(void)
{
	for(int i=0;i<TIMER_LEVELS;i++)
		linkedlist_create(&timerlists[i], LINKEDLIST_LOCKLESS);
	spinlock_create(&lock);
	int v = arch_timer_init();
	interrupt_register(v, timer_tick);
	arch_interrupt_unmask(v);
}

