#include <registers.h>
#define ASSEMBLY
#include <mmu-bits.h>

.globl initial_boot_stack
.comm initial_boot_stack, 0x10000

.extern kernel_translation_table
.extern table_l2
.extern table_l2_dev

#define OFFSET (KERNEL_VIRT_BASE - KERNEL_PHYS_BASE)

.section .text.boot
setup_initial_map:
	mov x0, 0
	mov x1, 0
	ldr x2, =table_l2-OFFSET
	ldr x3, =table_l2_dev-OFFSET
	mov x4, 1 | 1 << 10
	ldr x5, =0x20000000
	.Lzero_l2:
		str x1, [x2]
		str x4, [x3]
		add x4, x4, x5
		add x2, x2, 8
		add x3, x3, 8
		add x0, x0, 1
		cmp x0, 8192
		b.ne .Lzero_l2

	ldr x2, =table_l2-OFFSET
	ldr x1, =0x40000000 | 1 | 1 << 10
	str x1, [x2]
	str x1, [x2, #8]
	
	mov x0, 0
	mov x1, 0
	ldr x2, =kernel_translation_table-OFFSET
	.Lzero_l1:
		str x1, [x2]
		add x2, x2, 8
		add x0, x0, 1
		cmp x0, 64
		b.ne .Lzero_l1
	
	ldr x2, =kernel_translation_table-OFFSET
	ldr x1, =table_l2-OFFSET
	orr x1, x1, 3
	str x1, [x2]
	
	ldr x1, =table_l2_dev-OFFSET
	orr x1, x1, 3
	str x1, [x2, #63*8]

load_tables:
	tlbi vmalle1is
	isb
	dsb sy

	ldr x0, =MMU_MAIR_VAL
	msr mair_el1, x0
	ldr     x0, =(0b101 << 32 | 0b11 << 30 | 0b11 << 14 | 16 << 16 | 16)
	msr     tcr_el1, x0
	isb

	ldr x0, =kernel_translation_table-OFFSET
	msr ttbr0_el1, x0
	msr ttbr1_el1, x0
	isb

	mrs x0, sctlr_el1
	orr x0, x0, #1
	msr sctlr_el1, x0

	/* okay, now load a register with a valid higher half address
	 * and jump to it */
	ldr x0, =_higher_half_start
	br x0

	ret

.extern aarch64_cpu_secondary_entry
.extern aarch64_cpu_primary_entry
_higher_half_secondary:
	mrs x0, MPIDR_EL1
	/* pass MPIDR as arg1 */
	bl aarch64_cpu_secondary_entry
	b .

_higher_half_start:
	mrs x0, MPIDR_EL1
	mov x1, #(1 << 31)
	cmp x0, x1
	b.ne _higher_half_secondary
	
	ldr x0, =initial_boot_stack+0x10000 //set up stack
	mov sp, x0
	
	bl aarch64_cpu_primary_entry
	b .

_early_init:
	mrs x0, CurrentEL
	cmp x0, #(2 << 2) // Check if EL2
	b.ne .el1

	mrs x0, sctlr_el2
	bic x0, x0, #(1 << 25)
	msr sctlr_el2, x0
	
	// Set x1 to SPSR bits we want.
	mov x1, #(PSR_A_BIT | PSR_D_BIT | PSR_F_BIT | PSR_I_BIT | PSR_MODE_EL1h)
	ldr x0, =.el1
	msr spsr_el2, x1
	msr elr_el2, x0
	eret /* and return to EL1 */

	.el1:
	/* now we're in EL1 mode */
	mrs x0, sctlr_el1
	bic     x0, x0, #(1 << 25) /* unset EE */
    orr     x0, x0, #(1<<12)   /* Enable icache */
    orr     x0, x0, #(1<<2)    /* Enable dcache/ucache */
	bic     x0, x0, #(1 << 19) /* disable WXN */
	msr sctlr_el1, x0
	ret

.globl _start_secondary
_start_secondary:
	mov sp, x0 /* x0 holds our new stack */
	bl _early_init
	bl load_tables
	b .

.globl _start
_start:
	bl _early_init
	/* okay, set up the initial mapping */
	b setup_initial_map

