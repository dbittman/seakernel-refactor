#include <thread-bits.h>

#define MULTIBOOT_HEADER_MAGIC 0x1BADB002

/* The flags for the Multiboot header. */
#define MULTIBOOT_HEADER_FLAGS 0x00010002
#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

#define PHYS_LOAD_ADDRESS (KERNEL_PHYS_BASE + KERNEL_LOAD_OFFSET)
#define PHYS_ADDR_DELTA (KERNEL_VIRT_BASE + KERNEL_LOAD_OFFSET - PHYS_LOAD_ADDRESS)
#define PHYS(x) ((x) - PHYS_ADDR_DELTA)


.section .boot
.code32
.global _start
_start:
    jmp real_start

.align 8

.type multiboot_header,STT_OBJECT
multiboot_header:
    /* magic */
    .int MULTIBOOT_HEADER_MAGIC
    /* flags */
    .int MULTIBOOT_HEADER_FLAGS
    /* checksum */
    .int -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS)
    /* header_addr */
    .int PHYS(multiboot_header)
    /* load_addr */
    .int PHYS(_start)
    /* load_end_addr */
    .int PHYS(kernel_data_end)
    /* bss_end_addr */
    .int PHYS(kernel_bss_end)
    /* entry_addr */
    .int PHYS(real_start)

.align 8
gdtdesc:
    .word      0x47
    .long      PHYS(gdtable)
    .long      0

.align 8
gdtdesc_virt:
    .word      0x47
    .quad      gdtable

.align 8
gdtable:
    .word      0, 0                    # null segment
    .byte      0, 0, 0, 0

    .word      0xFFFF, 0               # 64-bit kernel code segment
    .byte      0, 0x9A, 0xAF, 0

    .word      0xFFFF, 0               # 64-bit kernel data segment
    .byte      0, 0x92, 0xAF, 0

	.word      0xFFFF, 0               # 64-bit user code segment
    .byte      0, 0xFA, 0xAF, 0

    .word      0xFFFF, 0               # 64-bit user data segment
    .byte      0, 0xF2, 0xAF, 0

	.quad      0 # For tss entry
	.quad      0

real_start:
	movl %eax, %esi
	movl $PHYS(initial_pml4), %eax
	orl $7, (%eax)
	orl $7, 511*8(%eax)
	orl $7, 256*8(%eax)

	movl $PHYS(boot_pdpt_upper), %eax
	orl $7, 510*8(%eax)
	movl $PHYS(boot_pdpt_lower), %eax
	orl $7, (%eax)

	mov %cr4, %eax
	bts $5, %eax
	mov %eax, %cr4
	mov $0xC0000080, %ecx
	rdmsr
	bts $8, %eax
	bts $11, %eax # NX
	wrmsr

	mov $PHYS(initial_pml4), %eax
	mov %eax, %cr3
	mov %cr0, %eax
	bts $31, %eax
	mov %eax, %cr0

	lgdt (PHYS(gdtdesc))

	ljmp $0x08,$PHYS(start64_low)

.code64
.section .text

start64_low:
	lgdt (gdtdesc_virt)
	movq $start64, %rax
	jmpq *%rax

.type _start, @function
.extern x86_64_init
.type x86_64_cpu_primary_entry, @function
start64:
	movw $0x10, %ax
	movw %ax, %ds
	movw %ax, %ss
	movw %ax, %gs
	movw $0x10, %ax
	movw %ax, %es
	movw %ax, %fs

	movq $__stack_top, %rsp
	xorq %rdi, %rdi
	movl %ebx, %edi
	
	call x86_64_cpu_primary_entry
	cli
.Lhang:
	hlt
	jmp .Lhang

.global arch_processor_reset
death_idt:
	.word 0
	.quad 0
arch_processor_reset:
	lidt (death_idt)
	int $255

.section .data
.align 0x1000
boot_pdpt_lower:
	.quad PHYS(boot_pd)
	.rept 511
		.quad 0
	.endr

.align 0x1000
boot_pdpt_upper:
	.rept 510
		.quad 0
	.endr
	.quad PHYS(boot_pd)
	.quad 0

.align 0x1000
.set i, 0x87
boot_pd:
	.rept 512
	.quad i
	.set i, i + 0x200000
	.endr

.align 0x1000
.global initial_pml4
initial_pml4:
	.quad PHYS(boot_pdpt_lower)
	.rept 255
		.quad 0
	.endr
	.quad PHYS(boot_pdpt_lower)
	.rept 254
		.quad 0
	.endr
	.quad PHYS(boot_pdpt_upper)

.section .bss
.align KERNEL_STACK_SIZE * 16
.global initial_boot_stack
initial_boot_stack:
.skip KERNEL_STACK_SIZE
__stack_top:

