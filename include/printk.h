#ifndef __PRINTK_H
#define __PRINTK_H
#include <stdarg.h>
#include <stddef.h>

__attribute__ ((format (printf, 1, 2))) void printk(const char *, ...);
int vprintk(const char *fmt, va_list args);
int snprintf(char *buf, size_t len, const char *fmt, ...);

#endif

