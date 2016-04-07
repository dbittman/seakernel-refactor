#include <arch-hvc.h>

unsigned long psci_hvc_cmd2(unsigned long func, unsigned long arg1, unsigned long arg2)
{
	return aarch64_hvc2(func, arg1, arg2);
}

unsigned long psci_hvc_cmd3(unsigned long func, unsigned long arg1, unsigned long arg2, unsigned long arg3)
{
	return aarch64_hvc3(func, arg1, arg2, arg3);
}

