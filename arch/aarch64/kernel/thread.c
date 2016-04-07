#include <thread.h>
#include <processor.h>
#include <mmu.h>
#include <thread-bits.h>
extern void aarch64_do_context_switch(void **oldstack, void *newstack);
extern void aarch64_do_thread_create(void *, void **, uintptr_t, void *arg);
extern void aarch64_thread_usermode_jump(uintptr_t entry, uintptr_t stack_base, uintptr_t usertt, void *arg);

static void __save_fpu_state(struct thread *thr)
{
	asm volatile("mrs %0, fpcr" : "=r"(thr->arch.fpcr));
	asm volatile("mrs %0, fpsr" : "=r"(thr->arch.fpsr));

	asm volatile("stp q0, q1, [%0, #(0 * 32)]\n"
			"stp q2, q3, [%0, #(1 * 32)]\n"
			"stp q4, q5, [%0, #(2 * 32)]\n"
			"stp q6, q7, [%0, #(3 * 32)]\n"
			"stp q8, q9, [%0, #(4 * 32)]\n"
			"stp q10, q11, [%0, #(5 * 32)]\n"
			"stp q12, q13, [%0, #(6 * 32)]\n"
			"stp q14, q15, [%0, #(7 * 32)]\n"
			"stp q16, q17, [%0, #(8 * 32)]\n"
			"stp q18, q19, [%0, #(9 * 32)]\n"
			"stp q20, q21, [%0, #(10 * 32)]\n"
			"stp q22, q23, [%0, #(11 * 32)]\n"
			"stp q24, q25, [%0, #(12 * 32)]\n"
			"stp q26, q27, [%0, #(13 * 32)]\n"
			"stp q28, q29, [%0, #(14 * 32)]\n"
			"stp q30, q31, [%0, #(15 * 32)]\n"
			:: "r"(thr->arch.fpu_data));
}

static void __restore_fpu_state(struct thread *thr)
{
	asm volatile("msr fpcr, %0" :: "r"(thr->arch.fpcr));
	asm volatile("msr fpsr, %0" :: "r"(thr->arch.fpsr));

	asm volatile("ldp q0, q1, [%0, #(0 * 32)]\n"
			"ldp q2, q3, [%0, #(1 * 32)]\n"
			"ldp q4, q5, [%0, #(2 * 32)]\n"
			"ldp q6, q7, [%0, #(3 * 32)]\n"
			"ldp q8, q9, [%0, #(4 * 32)]\n"
			"ldp q10, q11, [%0, #(5 * 32)]\n"
			"ldp q12, q13, [%0, #(6 * 32)]\n"
			"ldp q14, q15, [%0, #(7 * 32)]\n"
			"ldp q16, q17, [%0, #(8 * 32)]\n"
			"ldp q18, q19, [%0, #(9 * 32)]\n"
			"ldp q20, q21, [%0, #(10 * 32)]\n"
			"ldp q22, q23, [%0, #(11 * 32)]\n"
			"ldp q24, q25, [%0, #(12 * 32)]\n"
			"ldp q26, q27, [%0, #(13 * 32)]\n"
			"ldp q28, q29, [%0, #(14 * 32)]\n"
			"ldp q30, q31, [%0, #(15 * 32)]\n"
			:: "r"(thr->arch.fpu_data));
}

/* TODO (major) [dbittman]: support floating-point and SIMD instructions */
static int df = 0;

void arch_thread_context_switch(struct thread *old, struct thread *next)
{
	if(df)
		__save_fpu_state(old);
	asm volatile("msr TPIDR_EL1, %0" :: "r"(next));
	asm volatile("msr TPIDRRO_EL0, %0" :: "r"(next->user_tls_base));
	if(old->ctx != next->ctx) {
		arch_mm_context_switch(next->ctx);
	}
	aarch64_do_context_switch(&old->stackpointer, next->stackpointer);
	/* NOTE: when we get back to this point, we're in the state from
	 * where we originally called this function, so 'old' is what we
	 * want to restore FPU state from. */
	if(df)
		__restore_fpu_state(old);
}

void arch_thread_create(struct thread *next, uintptr_t entry, void *arg)
{
	aarch64_do_thread_create(next->kernel_tls_base, &next->stackpointer, entry, arg);
}

void arch_thread_init(struct thread *us)
{
	asm volatile("msr TPIDR_EL1, %0" :: "r"(us));
}

void arch_thread_usermode_jump(uintptr_t entry, void *arg)
{
	aarch64_thread_usermode_jump(entry, ((uintptr_t)current_thread->user_tls_base) + USER_TLS_SIZE, current_thread->ctx->arch.user_tt - PHYS_MAP_START, arg);
}

