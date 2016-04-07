#ifndef __MACHINE_GIC_H
#define __MACHINE_GIC_H

#include <machine/memmap.h>
#include <stdint.h>

#define GIC_DIST_BASE (0x8000000ULL + PHYS_MAP_START)
#define GIC_CPU_BASE  (0x8010000ULL + PHYS_MAP_START)

void gic_init_percpu(void);
void gic_init(void);
uint32_t gic_read_interrupt_number(void);
void gic_signal_eoi(uint32_t vector);
void gic_enable_vector(int vector);
void gic_disable_vector(int vector);

#endif

