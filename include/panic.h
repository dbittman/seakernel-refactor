#ifndef __PANIC_H
#define __PANIC_H

#include <stdnoreturn.h>

__attribute__((format(printf, 2, 3)))
noreturn void panic(int flags, const char *fmt, ...);

void arch_panic_begin(void);
#endif

