#ifndef __MACHINE_MACHINE_H
#define __MACHINE_MACHINE_H
#include <stddef.h>
#define MAX_INT 128
size_t machine_get_memlen(void);

#define TIMER_INTERRUPT 30
#define UART_INT(n) (33+n)

void machine_init(void);

#endif

