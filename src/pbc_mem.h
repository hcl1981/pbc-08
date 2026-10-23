#ifndef PBC_MEM_H
#define PBC_MEM_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* malloc that runs a full Lua collection and retries once on failure;
 * counted as C++ memory, release with pbc_free */
void *pbc_malloc(size_t n);
void  pbc_free(void *p);

/* set by the platform: heap available to the VM, and what stays free */
extern size_t pbc_heap_total, pbc_heap_reserve, pbc_cpp_used;
/* never returns: tells the user and restarts into the cart menu */
void pbc_out_of_memory(size_t n) __attribute__((noreturn));
#ifdef __cplusplus
}
#endif
#endif
