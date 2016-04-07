.globl aarch64_hvc2
.globl aarch64_hvc3
aarch64_hvc2:
aarch64_hvc3:
	hvc #0
	ret

.globl aarch64_thread_usermode_jump
/* noreturn void aarch64_thread_usermode_jump(uintptr_t entry, uintptr_t stack_base, uintptr_t usertt, void *arg) */
aarch64_thread_usermode_jump:
	mov x8, #0
	msr spsr_el1, x8
	msr elr_el1, x0
	msr sp_el0, x1
	msr ttbr0_el1, x2
	mov x0, x3
	eret
