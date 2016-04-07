#if CONFIG_TRACE

#include <printk.h>
#include <stdarg.h>
#include <lib/hash.h>
#include <string.h>
#include <trace.h>
#include <system.h>
static struct hash trace_hash;
static bool ready = false;
void __trace(struct trace *trace, const char *message, ...)
{
	if(!ready) return;
	if(hash_lookup(&trace_hash, trace->name, strlen(trace->name))) {
		va_list args;
		va_start(args, message);
		vprintk(message, args);
		va_end(args);
	}
}

__orderedinitializer(TRACE_INITIALIZER) static void trace_init(void)
{
	hash_create(&trace_hash, 0, 128);
	ready = true;
}

void trace_enable(struct trace *trace)
{
	if(!ready) return;
	hash_insert(&trace_hash, trace->name, strlen(trace->name), &trace->elem, trace);
}

void trace_disable(struct trace *trace)
{
	if(!ready) return;
	hash_delete(&trace_hash, trace->name, strlen(trace->name));
}

#endif

