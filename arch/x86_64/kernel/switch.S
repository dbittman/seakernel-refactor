#include <thread-bits.h>
.code64
.global x86_64_do_context_switch
x86_64_do_context_switch:
	pushfq
	pushq %rbp
	pushq %rbx
	pushq %r12
	pushq %r13
	pushq %r14
	pushq %r15

	movq %rsp, (%rdi)
	movq (%rsi), %rsp
	
	popq %r15
	popq %r14
	popq %r13
	popq %r12
	popq %rbx
	popq %rbp
	popfq
	ret

_x86_64_thread_initial_entry:
	movq %r15, %rdi
	sti
	jmp *%r14

# void x86_64_do_thread_create(
#	void *stack_base, RDI
#	void **stackpointer, RSI
#	struct thread *thread, RDX
#	void *fn, RCX
#	void *arg) R8
.global x86_64_do_thread_create
x86_64_do_thread_create:
	# write the thread pointer into the bottom of the stack
	movq %rdx, (%rdi)

	# move to the top of the stack to write the initial frame
	addq $KERNEL_STACK_SIZE, %rdi
	movq $_x86_64_thread_initial_entry, -0x08(%rdi) # return address

	# initial frame
	pushfq
	popq %rax
	movq %rax, -0x10(%rdi)

	movq $0, -0x18(%rdi)
	movq $0, -0x20(%rdi)
	movq $0, -0x28(%rdi)
	movq $0, -0x30(%rdi)
	movq %rcx, -0x38(%rdi)
	movq %r8, -0x40(%rdi)

	# write the initial stack pointer
	subq $0x40, %rdi
	movq %rdi, (%rsi)

	ret

