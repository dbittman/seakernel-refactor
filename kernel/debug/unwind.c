#include <printk.h>
#include <stdint.h>
#include <stdbool.h>
#include <debug.h>
#include <system.h>
#include <ksymbol.h>
#if FEATURE_SUPPORTED_UNWIND
static void __print_frame(struct frame *frame)
{
	const struct ksymbol *sym = ksymbol_find_by_value((void *)frame->pc, true);
	if(sym) {
		uintptr_t offset = frame->pc - sym->value;
		printk("  %lx < %s + %lx >\n", frame->pc, sym ? sym->name : "???", offset);
	}
}
#endif
void debug_print_backtrace(void)
{
#if FEATURE_SUPPORTED_UNWIND
	struct frame frame;
	frame.pc = (uintptr_t)__builtin_extract_return_addr(__builtin_return_address(0));
	frame.fp = (uintptr_t)__builtin_frame_address(0);
	printk("STACK TRACE:\n");
	while(arch_debug_unwind_frame(&frame)) {
		__print_frame(&frame);
	}
#else
	printk("Arch '%s' does not support unwinding.\n", stringify_define(CONFIG_ARCH));
#endif
}

