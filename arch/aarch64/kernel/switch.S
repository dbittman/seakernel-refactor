#include <thread-bits.h>

.globl aarch64_do_context_switch
/* void aarch64_do_context_switch(void **save_stack_pointer, void *new_stack_pointer) */
aarch64_do_context_switch:
	sub     sp, sp, #16*8
	stp x16, x17, [sp, #16 * 0]
	stp x18, x19, [sp, #16 * 1]
	stp x20, x21, [sp, #16 * 2]
	stp x22, x23, [sp, #16 * 3]
	stp x24, x25, [sp, #16 * 4]
	stp x26, x27, [sp, #16 * 5]
	stp x28, x29, [sp, #16 * 6]
	str x30, [sp, #16 * 7 + 8]

	mov x15, sp
	str x15, [x0]
	mov sp, x1

	ldp x16, x17, [sp, #16 * 0]
	ldp x18, x19, [sp, #16 * 1]
	ldp x20, x21, [sp, #16 * 2]
	ldp x22, x23, [sp, #16 * 3]
	ldp x24, x25, [sp, #16 * 4]
	ldp x26, x27, [sp, #16 * 5]
	ldp x28, x29, [sp, #16 * 6]
	ldr x30, [sp, #16 * 7 + 8]
	add sp, sp, #16*8
	ret

aarch64_thread_initial_entry:
	ldp x1, x0, [sp, #0]
	add sp, sp, #16
	mov x2, #0
	msr DAIF, x2
	br x1

.globl aarch64_do_thread_create
/* void aarch64_do_thread_create(void *stack, void **stack_pointer, uintptr_t entry, void *arg); */
aarch64_do_thread_create:
	add x0, x0, KERNEL_STACK_SIZE

	sub x0, x0, #16
	stp x2, x3, [x0, #0]

	sub x0, x0, #16*8
	ldr x4, =aarch64_thread_initial_entry
	str x4, [x0, #16*7 + 8]

	str x0, [x1]

	ret
