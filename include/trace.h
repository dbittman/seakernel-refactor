#ifndef __TRACE_H
#define __TRACE_H

#if CONFIG_TRACE
#include <lib/hash.h>
#include <thread.h>
struct trace {
	const char *name;
	struct hashelem elem;
};

void __trace(struct trace *trace, const char *message, ...);
void trace_enable(struct trace *trace);
void trace_disable(struct trace *trace);

#define TRACE(t,m,...) __trace(t, "[%s:%d]: " m "\n", (t)->name, current_thread ? current_thread->tid : 0, ##__VA_ARGS__)
#define TRACE_DEFINE(tn,n) static struct trace tn = {.name=n}

#else

#define TRACE(...)
#define TRACE_DEFINE(...)

#define trace_init()
#define trace_enable(x)
#define trace_disable(x)

#endif

#define TRACE_INITIALIZER 1

#endif

