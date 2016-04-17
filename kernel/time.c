#include <processor.h>
#include <timer.h>

time_t time_get_current(void)
{
	return arch_processor_get_nanoseconds();
}

