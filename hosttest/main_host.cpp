// PC test build: ./pico8_host [cart]   (PF_ROOT = the "drive")
#include <cstdio>
#include <cstdlib>
#include "pico8_run.h"

extern "C" void plat_host_init(void);

int main(int argc, char **argv)
{
    plat_host_init();
    pico8_run(argc > 1 ? argv[1] : nullptr);
    return 0;
}
