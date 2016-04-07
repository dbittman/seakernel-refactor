.macro entry_point table,name,callback
\table\()_\name\():
	sub     sp, sp, #16*18
	stp x0, x1, [sp, #16 * 0]
	stp x2, x3, [sp, #16 * 1]
	stp x4, x5, [sp, #16 * 2]
	stp x6, x7, [sp, #16 * 3]
	stp x8, x9, [sp, #16 * 4]
	stp x10, x11, [sp, #16 * 5]
	stp x12, x13, [sp, #16 * 6]
	stp x14, x15, [sp, #16 * 7]
	stp x16, x17, [sp, #16 * 8]
	stp x18, x19, [sp, #16 * 9]
	stp x20, x21, [sp, #16 * 11]
	stp x22, x23, [sp, #16 * 12]
	stp x24, x25, [sp, #16 * 13]
	stp x26, x27, [sp, #16 * 14]
	stp x28, x29, [sp, #16 * 15]
	stp x30, x30, [sp, #16 * 16]
	mrs x1, elr_el1
	mrs x2, spsr_el1
	stp x1, x2, [sp, #16 * 17]

	mrs x0, esr_el1
	mov x1, sp
	bl \callback

	ldp x1, x2, [sp, #16 * 17]
	msr elr_el1, x1
	msr spsr_el1, x2

	ldp x30, x0, [sp, #16 * 16]
	ldp x0, x1, [sp, #16 * 0]
	ldp x2, x3, [sp, #16 * 1]
	ldp x4, x5, [sp, #16 * 2]
	ldp x6, x7, [sp, #16 * 3]
	ldp x8, x9, [sp, #16 * 4]
	ldp x10, x11, [sp, #16 * 5]
	ldp x12, x13, [sp, #16 * 6]
	ldp x14, x15, [sp, #16 * 7]
	ldp x16, x17, [sp, #16 * 8]
	ldp x18, x19, [sp, #16 * 9]
	ldp x20, x21, [sp, #16 * 11]
	ldp x22, x23, [sp, #16 * 12]
	ldp x24, x25, [sp, #16 * 13]
	ldp x26, x27, [sp, #16 * 14]
	ldp x28, x29, [sp, #16 * 15]
	add sp, sp, #16 * 18
	eret
.endm

.extern arm_entry_unknown
.extern arm_entry_l64_sync
.extern arm_entry_l64_serror
.extern arm_entry_l64_fiq
.extern arm_entry_l64_irq
.extern arm_entry_spx_sync
.extern arm_entry_spx_serror
.extern arm_entry_spx_fiq
.extern arm_entry_spx_irq

entry_point table0, exception_entry_unknown, arm_entry_unknown

entry_point table0, exception_entry_lower64_sync,   arm_entry_l64_sync
entry_point table0, exception_entry_lower64_serror, arm_entry_l64_serror
entry_point table0, exception_entry_lower64_irq,    arm_entry_l64_irq
entry_point table0, exception_entry_lower64_fiq,    arm_entry_l64_fiq

entry_point table0, exception_entry_spx_sync,   arm_entry_spx_sync
entry_point table0, exception_entry_spx_serror, arm_entry_spx_serror
entry_point table0, exception_entry_spx_irq,    arm_entry_spx_irq
entry_point table0, exception_entry_spx_fiq,    arm_entry_spx_fiq

.macro ventry table,label
	.align 7
	b \table\()_\label
.endm

.align 11
.global table0
table0:
	ventry table0 exception_entry_unknown
	ventry table0 exception_entry_unknown
	ventry table0 exception_entry_unknown
	ventry table0 exception_entry_unknown
	
	ventry table0 exception_entry_spx_sync
	ventry table0 exception_entry_spx_irq
	ventry table0 exception_entry_spx_fiq
	ventry table0 exception_entry_spx_serror

	ventry table0 exception_entry_lower64_sync
	ventry table0 exception_entry_lower64_irq
	ventry table0 exception_entry_lower64_fiq
	ventry table0 exception_entry_lower64_serror

	ventry table0 exception_entry_unknown
	ventry table0 exception_entry_unknown
	ventry table0 exception_entry_unknown
	ventry table0 exception_entry_unknown


