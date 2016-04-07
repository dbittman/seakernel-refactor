#ifndef __DEBUG_H
#define __DEBUG_H

#include <stdint.h>
#include <stdbool.h>

#if FEATURE_SUPPORTED_UNWIND
struct frame {
	uintptr_t pc, fp;
};
void debug_print_backtrace(void);
bool arch_debug_unwind_frame(struct frame *frame);
#endif

#endif

