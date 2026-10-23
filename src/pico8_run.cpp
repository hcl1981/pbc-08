// pico8_run.cpp -- builds fake-08's objects and runs the VM loop forever.
// Shared by the firmware (on core 1) and the PC test build.
#include <string>

#include "vm.h"
#include "host.h"
#include "Audio.h"
#include "PicoRam.h"
#include "logger.h"
#include "pico8_run.h"

extern "C" void plat_mark(const char *what);

void pico8_run(const char *autostart)
{
    plat_mark("start");
    Host *host = new Host();
    PicoRam *memory = new PicoRam();
    memory->Reset();
    Audio *audio = new Audio(memory);
    plat_mark("objects");
    Vm *vm = new Vm(host, memory, nullptr, nullptr, audio);
    plat_mark("vm + lua bios");

    host->setUpPaletteColors();
    host->oneTimeSetup(audio);
    host->setTargetFps(60);

    vm->SetCartList(host->listcarts());

    if (autostart && autostart[0])
        vm->LoadCart(autostart, true);
    else
        vm->LoadBiosCart();

    plat_mark("cart loaded");
    vm->vm_run();
    plat_mark("cart running");
    vm->GameLoop();
}
