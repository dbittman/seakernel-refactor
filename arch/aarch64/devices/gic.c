#include <machine/gic.h>
#include <machine/machine.h>
#include <stdint.h>

#define GICC_WRITE(reg, value) \
	*(uint32_t *)(GIC_CPU_BASE + reg) = value

#define GICD_WRITE(reg, value) \
	*(uint32_t *)(GIC_DIST_BASE + reg) = value

#define GICC_READ(reg) \
	*(uint32_t *)(GIC_CPU_BASE + reg)

#define GICD_READ(reg) \
	*(uint32_t *)(GIC_DIST_BASE + reg)

#define GICC_CTLR 0
#define GICC_PMR  0x4
#define GICC_IAR  0xc
#define GICC_EOIR 0x10


#define GICD_CTLR               (0x000)
#define GICD_TYPER              (0x004)
#define GICD_IIDR               (0x008)
#define GICD_IGROUPR(n)         (0x080 + (n) * 4)
#define GICD_ISENABLER(n)       (0x100 + (n) * 4)
#define GICD_ICENABLER(n)       (0x180 + (n) * 4)
#define GICD_ISPENDR(n)         (0x200 + (n) * 4)
#define GICD_ICPENDR(n)         (0x280 + (n) * 4)
#define GICD_ISACTIVER(n)       (0x300 + (n) * 4)
#define GICD_ICACTIVER(n)       (0x380 + (n) * 4)
#define GICD_IPRIORITYR(n)      (0x400 + (n) * 4)
#define GICD_ITARGETSR(n)       (0x800 + (n) * 4)
#define GICD_ICFGR(n)           (0xc00 + (n) * 4)
#define GICD_NSACR(n)           (0xe00 + (n) * 4)
#define GICD_SGIR               (0xf00)
#define GICD_CPENDSGIR(n)       (0xf10 + (n) * 4)
#define GICD_SPENDSGIR(n)       (0xf20 + (n) * 4)

void gic_enable_vector(int vector)
{
	int r = vector / 32;
	uint32_t mask = 1ULL << (vector % 32);
	GICD_WRITE(GICD_ISENABLER(r), mask);
}

void gic_disable_vector(int vector)
{
	int r = vector / 32;
	uint32_t mask = 1ULL << (vector % 32);
	GICD_WRITE(GICD_ICENABLER(r), mask);
}

void arch_interrupt_mask(int vector)
{
	gic_disable_vector(vector);
}

void arch_interrupt_unmask(int vector)
{
	gic_enable_vector(vector);
}

uint32_t gic_read_interrupt_number(void)
{
	uint32_t v = GICC_READ(GICC_IAR);
	return v;
}

void gic_signal_eoi(uint32_t vector)
{
	GICC_WRITE(GICC_EOIR, vector);
}

void gic_init(void)
{
	/* disable all interrupts, and clear their pending
	 * status */
	for(int i=0;i<MAX_INT;i+=32) {
		GICD_WRITE(GICD_ICENABLER(i / 32), ~0);
		GICD_WRITE(GICD_ICPENDR(i / 32), ~0);
	}

	/* enable the GIC */
	GICD_WRITE(GICD_CTLR, 1);

}

void gic_init_percpu(void)
{
	GICC_WRITE(GICC_CTLR, 1);
	GICC_WRITE(GICC_PMR, 0xFF);
}

