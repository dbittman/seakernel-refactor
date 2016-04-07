#include <ksymbol.h>
/* right, so, here's some linking magic.
 * basically, the kernel references these two symbols in places.
 * However, due to the 2-stage linking process, these symbols aren't
 * actually defined until the second link, when it links with the generated
 * symbol table file. So we need to define them as weak symbols here so that
 * our code can link for stage 1. */
#pragma weak kernel_symbol_table
#pragma weak kernel_symbol_table_length
__attribute__((weak,used)) size_t kernel_symbol_table_length = 0;
const struct ksymbol kernel_symbol_table[] __attribute__ ((weak,used)) = {{0,0,0}};


