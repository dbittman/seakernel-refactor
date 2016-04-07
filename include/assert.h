#ifndef __ASSERT_H
#define __ASSERT_H

#include <panic.h>

#define assert(cond) do { if(!__builtin_expect(cond, 0)) panic(0, "assertion failure (%s:%d) %s", __FILE__, __LINE__, #cond); } while(0)

#endif

