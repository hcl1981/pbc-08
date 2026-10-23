// Prints a cart's Lua source (e.g. to grep for load() or mouse use).
//   make luadump_host && PF_ROOT=. ./luadump_host cart.p8.png > cart.lua
#include <cstdio>
#include <cstdlib>
#include <string>
#include "cart.h"

extern "C" void *pbc_malloc(size_t n) { return malloc(n); }
extern "C" void pbc_free(void *p) { free(p); }

int main(int argc, char **argv)
{
    if (argc < 2) return 1;
    Cart c(argv[1], "", false, nullptr);
    fwrite(c.LuaString.data(), 1, c.LuaString.size(), stdout);
}
