#ifndef __WORKER_H
#define __WORKER_H

#include <thread.h>

#define WORKER_JOIN 1
#define WORKER_EXIT 2

struct worker {
	struct thread *thread;
	_Atomic int flags;
	void *arg;
	_Atomic int exitcode;
};

void worker_start(struct worker *worker, void (*fn)(struct worker *), void *data);
void worker_exit(struct worker *w, int code);
bool worker_join(struct worker *w);

#define worker_arg(worker) worker->arg

static inline bool worker_notjoining(struct worker *w)
{
	return !(atomic_load(&w->flags) & WORKER_JOIN);
}

/* example worker thread entry function:
 * void thingy_entry(struct worker *worker)
 * {
 *     void *arg = worker_arg(worker);
 *     int exit_code = 0;
 *     while(worker_notjoining(worker)) {
 *         ... do work ...
 *     }
 *     worker_exit(worker, exit_code);
 * } */

#endif

