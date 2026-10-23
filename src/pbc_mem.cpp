// pbc_mem.cpp -- sharing the heap between Lua and the C++ side.
//
// Lua may grow as long as Lua + C++ together stay pbc_heap_reserve below
// pbc_heap_total (set by the platform to the free heap at start). C++
// allocations are counted here (operator new, pbc_malloc). If one still
// fails, a full Lua collection is tried once before pbc_out_of_memory().
#include <cstdlib>
#include <new>
#include <malloc.h>

#include "lua.h"
#include "pbc_mem.h"

extern lua_State *pbc_vm_lua;
extern size_t pbc_lua_used;

size_t pbc_heap_total;                  // 0: no limit (PC build default)
size_t pbc_heap_reserve = 32 * 1024;
size_t pbc_cpp_used;
static bool in_gc;

bool pbc_lua_may_grow(size_t delta)
{
    return !pbc_heap_total ||
           pbc_lua_used + pbc_cpp_used + delta + pbc_heap_reserve <= pbc_heap_total;
}

void *pbc_malloc(size_t n)
{
    void *p = malloc(n);
    if (!p && pbc_vm_lua && !in_gc) {
        in_gc = true;
        lua_gc(pbc_vm_lua, LUA_GCCOLLECT, 0);
        in_gc = false;
        p = malloc(n);
    }
    if (p)
        pbc_cpp_used += malloc_usable_size(p);
    return p;
}

void pbc_free(void *p)
{
    if (p) {
        pbc_cpp_used -= malloc_usable_size(p);
        free(p);
    }
}

static void *must_alloc(size_t n)
{
    void *p = pbc_malloc(n ? n : 1);
    if (!p)
        pbc_out_of_memory(n);
    return p;
}

void *operator new(size_t n) { return must_alloc(n); }
void *operator new[](size_t n) { return must_alloc(n); }
void *operator new(size_t n, const std::nothrow_t &) noexcept { return pbc_malloc(n); }
void *operator new[](size_t n, const std::nothrow_t &) noexcept { return pbc_malloc(n); }
void operator delete(void *p) noexcept { pbc_free(p); }
void operator delete[](void *p) noexcept { pbc_free(p); }
void operator delete(void *p, size_t) noexcept { pbc_free(p); }
void operator delete[](void *p, size_t) noexcept { pbc_free(p); }
