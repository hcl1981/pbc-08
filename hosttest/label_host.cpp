// Loads the menu label of each cart given, as the menu does on the device.
//   make label_host && PF_ROOT=/ ./label_host /path/to/*.p8.png
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <string>
#include "cart.h"

extern "C" void *pbc_malloc(size_t n) { return malloc(n); }
extern "C" void pbc_free(void *p) { free(p); }

int main(int argc, char **argv)
{
    static uint8_t px[128 * 128];
    for (int i = 1; i < argc; i++) {
        bool ok = cart_load_label(argv[i], px);
        int nz = 0;
        for (int j = 0; j < 128 * 128; j++) nz += px[j] != 0;
        printf("%-45s ok=%d nonzero=%d\n", argv[i], ok, nz);
    }
}
