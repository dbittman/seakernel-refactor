#ifndef __SYSTEM_H
#define __SYSTEM_H

#define likely(cond) __builtin_expect(cond, 1)
#define unlikely(cond) __builtin_expect(cond, 0)

#define stringify_define(x) stringify(x)
#define stringify(x) #x

#define __initializer __attribute__((used,constructor))
#define __orderedinitializer(x) __attribute__((used,constructor (x + 3000) ))

#define __orderedafter(x) (x+1)
#define __orderedbefore(x) (x-1)

/* credit to Andrew Kwong for this */
static inline unsigned long long __round_up_pow2(unsigned int a)
{
	return 1ull << (sizeof(a) * 8 - __builtin_clz(a));
}

#endif

