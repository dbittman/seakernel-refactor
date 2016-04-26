
#define PHYS_LOAD_ADDRESS (KERNEL_PHYS_BASE + KERNEL_LOAD_OFFSET)
#define PHYS_ADDR_DELTA (KERNEL_VIRT_BASE + KERNEL_LOAD_OFFSET - PHYS_LOAD_ADDRESS)
#define PHYS(x) ((x) - PHYS_ADDR_DELTA)

.code16
.global trampoline_start
.global trampoline_end
trampoline_start:
  cli
  xorw %bx, %bx
  movw $0x7100, %si
  lgdt (%bx,%si)
  mov %cr0, %eax
  orb $0x01, %al
  mov %eax, %cr0
  # Jump into protected-mode
  jmpl $0x08,$0x7200
trampoline_end:

.code32

.global pmode_enter
pmode_enter:
	mov $0x10, %ax
	mov %ax, %ds
	mov %ax, %ss
	mov %ax, %fs
	mov %ax, %gs
	mov %ax, %es
	mov %cr0, %eax
	andl $0x1FFFFFFF, %eax
	orl $(1 << 16), %eax
	mov %eax, %cr0

	movl $PHYS(second_start32), %eax
	jmp *%eax

.global rm_gdt
rm_gdt:
  	.int 0x00000000
    .int 0x00000000
    # kernel-code
    .word 0xFFFF
    .word 0x0000
    .byte 0x00
    .byte 0x98
    .byte 0xCF
    .byte 0x00
    # kernel-data
    .word 0xFFFF
    .word 0x0000
    .byte 0x00
    .byte 0x92
    .byte 0xCF
    .byte 0x00
    rm_gdt_end:

.global rm_gdt_pointer
rm_gdt_pointer:
    .word 0x18 - 1
	.int 0x7100+0x6



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

second_start32:
	movl %eax, %esi
	movl $PHYS(ap_initial_pml4), %eax
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

	mov $PHYS(ap_initial_pml4), %eax
	mov %eax, %cr3
	mov %cr0, %eax
	bts $31, %eax
	mov %eax, %cr0

	lgdt (PHYS(gdtdesc))

	ljmp $0x08,$PHYS(second_start64_low)

.code64
.section .text

second_start64_low:
	lgdt (gdtdesc_virt)
	movq $second_start64, %rax
	jmpq *%rax

.extern x86_64_cpu_secondary_entry
.extern initial_pml4
second_start64:
	cli
	movw $0x10, %ax
	movw %ax, %ds
	movw %ax, %ss
	movw %ax, %gs
	movw %ax, %es
	movw %ax, %fs

	movq 0x7300, %rsp

	movq $PHYS(initial_pml4), %rax
	movq %rax, %cr3
	xorq %rax, %rax
	movq %rax, 0x7300
	mfence

	xorq %rdi, %rdi
	movl -4(%rsp), %edi
	
	call x86_64_cpu_secondary_entry
.Lhang:
	hlt
	jmp .Lhang

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
ap_initial_pml4:
	.quad PHYS(boot_pdpt_lower)
	.rept 255
		.quad 0
	.endr
	.quad PHYS(boot_pdpt_lower)
	.rept 254
		.quad 0
	.endr
	.quad PHYS(boot_pdpt_upper)

