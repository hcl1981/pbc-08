// runs the VM on a painted 4 MB thread stack; prints the deepest use at exit
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include "pico8_run.h"

extern "C" void plat_host_init(void);
static char *stk;
static const size_t SZ = 4 << 20;
static const char *cart;

extern "C" void report_stack(void)
{
    size_t i = 0;
    while (i < SZ && stk[i] == 0x5a) i++;
    printf("stack used %zu bytes\n", SZ - i);
}

static void *run(void *) { pico8_run(cart); return nullptr; }

int main(int argc, char **argv)
{
    plat_host_init();
    cart = argc > 1 ? argv[1] : nullptr;
    stk = (char *)aligned_alloc(4096, SZ);
    memset(stk, 0x5a, SZ);
    pthread_attr_t a;
    pthread_attr_init(&a);
    pthread_attr_setstack(&a, stk, SZ);
    pthread_t t;
    pthread_create(&t, &a, run, nullptr);
    pthread_join(t, nullptr);
}
