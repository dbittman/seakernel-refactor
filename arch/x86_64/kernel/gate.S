# x86 exceptions can either push an error code, or not. Here
# we create two macros for entry points for exceptions, and
# we make it so that exceptions that don't push an errorcode
# have a dummy code pushed so that the exception frames are
# the same for both types.

.macro isr_errcode num, handler
.global x86_64_isr\num
.align 8
x86_64_isr\num:
	pushq $\num # push the interrupt number so that the C code knows what to do
	jmp \handler
.endm

.macro isr_noerrcode num, handler
.global x86_64_isr\num
.align 8
x86_64_isr\num:
	pushq $0
	pushq $\num
	jmp \handler
.endm

# these exceptions push error codes
.irp num,8,10,11,12,13,14,17
isr_errcode \num, isr_common
.endr

.irp num,0,1,2,3,4,5,6,7,9,15,16,18,19,20,21,22,23,24,25,26,27,28,29,30,31
isr_noerrcode \num, isr_common
.endr
# these are split into two so it's easier to read
.irp num,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47
isr_noerrcode \num, isr_common
.endr

# the syscall gate is named special
.global x86_64_isr_syscall
x86_64_isr_syscall:
	pushq $0
	pushq $0x80
	jmp syscall_handler

# the syscall gate is named special
.global x86_64_isr_shootdown
.extern x86_64_signal_eoi
x86_64_isr_shootdown:
	pushq $0
	pushq $0x80
	pushq %rax
	movq %cr3, %rax
	movq %rax, %cr3
	popq %rax
	jmp ignore_handler

.global x86_64_isr_halt
x86_64_isr_halt:
	cli
	.halt: hlt
	jmp .halt

.global x86_64_isr_ignore
x86_64_isr_ignore:
	pushq $0
	pushq $0x80
	jmp ignore_handler


.macro handler name, entrypoint
\name:
	# save the frame
	pushq %rdi
	pushq %rsi
	pushq %rdx
	pushq %rcx
	pushq %rax
	pushq %r8
	pushq %r9
	pushq %r10
	pushq %r11
	pushq %rbx
	pushq %rbp
	pushq %r12
	pushq %r13
	pushq %r14
	pushq %r15

	# call C code with the exception frame as an argument
	movq %rsp, %rdi
	call \entrypoint

	# restore the frame
	popq %r15
	popq %r14
	popq %r13
	popq %r12
	popq %rbp
	popq %rbx
	popq %r11
	popq %r10
	popq %r9
	popq %r8
	popq %rax
	popq %rcx
	popq %rdx
	popq %rsi
	popq %rdi
	
	add $16, %rsp # cleanup error code and int number
	iretq # and return from interrupt
.endm

.extern x86_64_exception_entry
.extern x86_64_syscall_entry
handler isr_common, x86_64_exception_entry
handler syscall_handler, x86_64_syscall_entry
handler ignore_handler, x86_64_signal_eoi

.global x86_64_fork_return
x86_64_fork_return:
	cli
	movq %rdi, %rsp
	# restore the frame
	popq %r15
	popq %r14
	popq %r13
	popq %r12
	popq %rbp
	popq %rbx
	popq %r11
	popq %r10
	popq %r9
	popq %r8
	popq %rax
	popq %rcx
	popq %rdx
	popq %rsi
	popq %rdi
	
	addq $16, %rsp # cleanup error code and int number
	xorq %rax, %rax
	iretq # and return from interrupt

