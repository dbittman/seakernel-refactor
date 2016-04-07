#ifndef __INTERRUPT_H
#define __INTERRUPT_H

#define MAX_HANDLERS 128

#define INTERRUPT_INKERNEL 1

#include <arch-interrupt.h>

void interrupt_init(void);
void interrupt_entry(int vector, int flags);
int interrupt_register(int vector, void (*f)(int flags));
void arch_interrupt_mask(int vector);
void arch_interrupt_unmask(int vector);
#endif

