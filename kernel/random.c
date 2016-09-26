#include <stdint.h>
#include <system.h>
#include <processor.h>
/* right, so this is just a temporary PRNG. We should probably
 * have a real RNG at some point. */

typedef struct { uint64_t state;  uint64_t inc; } pcg32_random_t;

static pcg32_random_t __state;

static uint32_t pcg32_random_r(pcg32_random_t* rng)
{
    uint64_t oldstate = rng->state;
    rng->state = oldstate * 6364136223846793005ULL + (rng->inc|1);
    uint32_t xorshifted = ((oldstate >> 18u) ^ oldstate) >> 27u;
    uint32_t rot = oldstate >> 59u;
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

/* ...and this isn't thread-safe, but whatever */
uint32_t random_u32(void)
{
	return pcg32_random_r(&__state);
}

__initializer static void random_init(void)
{
	/* yeah, I dunno. */
#if FEATURE_SUPPORTED_CYCLE_COUNT
	__state.state = arch_processor_get_cycle_count();;
#endif
	__state.state = arch_processor_get_nanoseconds();
	__state.inc = 64;// arch_processor_get_nanoseconds();
	pcg32_random_r(&__state);
}

