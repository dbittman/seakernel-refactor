#ifndef __ARCH_PSCI_H
#define __ARCH_PSCI_H

unsigned long psci_hvc_cmd2(unsigned long func, unsigned long arg1, unsigned long arg2);
unsigned long psci_hvc_cmd3(unsigned long function, unsigned long arg1, unsigned long arg2, unsigned long arg3);

#endif

