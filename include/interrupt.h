#ifndef __INTERRUPT_H
#define __INTERRUPT_H

#define MAX_HANDLERS 128

#define INTERRUPT_INKERNEL 1

#include <arch-interrupt.h>
#include <slab.h>
#include <lib/linkedlist.h>
#include <signal.h>

struct exception_frame {
	struct kobj_header _header;
	struct arch_exception_frame arch;
	struct linkedentry node;
	sigset_t mask;
};

void interrupt_init(void);
void interrupt_entry(int vector, int flags);
int interrupt_register(int vector, void (*f)(int flags));
void arch_interrupt_mask(int vector);
void arch_interrupt_unmask(int vector);
struct exception_frame *interrupt_pop_frame(void);
void interrupt_push_frame(struct arch_exception_frame *af, struct sigaction *action);
#endif

