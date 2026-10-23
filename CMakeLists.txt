# PBC PICO-8 -- PICO-8 player for the PicoBoy Color Plus
#
# VM: fake-08 (MIT) with z8lua, in fake08/. Platform layer in src/,
# flash FatFs + USB stick from PicoBoyGB / PBC Mega Drive. See README.md.
#
#   ./build.sh                 or by hand:
#   cmake -S . -B build -DPICO_SDK_PATH=... && cmake --build build -j
cmake_minimum_required(VERSION 3.13)

if(NOT DEFINED PICO_PLATFORM)
    set(PICO_PLATFORM rp2350-arm-s)
endif()
if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD pico2)
endif()

# ---- tuning knobs ----------------------------------------------------------
set(CPU_MHZ "250" CACHE STRING "System clock in MHz")
set(CPU_VREG "VREG_VOLTAGE_1_25" CACHE STRING "Core voltage (250 MHz at 1.25 V as in PicoBoyGB)")
set(FLASH_MAX_FREQ_MHZ "88" CACHE STRING "Upper limit for the flash (XIP) clock in MHz")
set(PBC_SPI_HZ "75000000" CACHE STRING "ST7789 SPI clock request in Hz (clk_peri is 150 MHz)")
set(SHOW_FPS "0" CACHE STRING "fps and Lua memory under the picture: 0/1")
set(LUA_RESERVE_KB "32" CACHE STRING "Heap kept free when Lua grows, KB")
# Debug build: a USB serial console with a running log instead of the USB
# stick (stick mode is not available then).
option(PBC_SERIAL_LOG "USB serial log instead of the USB stick" OFF)

include(pico_sdk_import.cmake)

project(pbc_pico8 C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# The pico2 board file says 4 MB; the PicoBoy Color Plus has 16 MB.
set(PICO_FLASH_SIZE_BYTES 16777216)

pico_sdk_init()

set(F8  ${CMAKE_CURRENT_LIST_DIR}/fake08/source)
set(LUA ${CMAKE_CURRENT_LIST_DIR}/fake08/libs/z8lua)
set(MZ  ${CMAKE_CURRENT_LIST_DIR}/fake08/libs/miniz)

set(F8_SOURCES
    ${F8}/Audio.cpp ${F8}/cart.cpp ${F8}/emojiconversion.cpp ${F8}/filehelpers.cpp
    ${F8}/filter.cpp ${F8}/fontdata.cpp ${F8}/graphics.cpp ${F8}/Input.cpp
    ${F8}/logger.cpp ${F8}/mathhelpers.cpp ${F8}/nibblehelpers.cpp
    ${F8}/picoluaapi.cpp ${F8}/printHelper.cpp ${F8}/stringToDataHelpers.cpp
    ${F8}/synth.cpp ${F8}/vm.cpp)

# z8lua is C++ (fix32 numbers)
set(LUA_SOURCES
    ${LUA}/eris.c ${LUA}/lapi.c ${LUA}/lauxlib.c ${LUA}/lbaselib.c ${LUA}/lbitlib.c
    ${LUA}/lcode.c ${LUA}/lcorolib.c ${LUA}/lctype.c ${LUA}/ldblib.c ${LUA}/ldebug.c
    ${LUA}/ldo.c ${LUA}/ldump.c ${LUA}/lfunc.c ${LUA}/lgc.c ${LUA}/linit.c
    ${LUA}/llex.c ${LUA}/lmem.c ${LUA}/lobject.c ${LUA}/lopcodes.c ${LUA}/lparser.c
    ${LUA}/lpico8lib.c ${LUA}/lstate.c ${LUA}/lstring.c ${LUA}/lstrlib.c
    ${LUA}/ltable.c ${LUA}/ltablib.c ${LUA}/ltm.c ${LUA}/lundump.c ${LUA}/lvm.c
    ${LUA}/lzio.c)
set_source_files_properties(${LUA_SOURCES} PROPERTIES LANGUAGE CXX)

add_executable(pbc_pico8
    src/main_pbc.cpp
    src/menu.cpp
    src/gfx.c
    src/assets.c
    src/settings.c
    src/pbc_host.cpp
    src/pbc_mem.cpp
    src/pico8_run.cpp
    src/video.c
    src/display.c
    src/input.c
    src/audio.c
    src/pfile_fatfs.c
    src/flash_fs.c
    fatfs/ff.c
    fatfs/ffunicode.c
    fatfs/diskio.c
    ${MZ}/miniz.c
    ${F8_SOURCES}
    ${LUA_SOURCES}
)

target_include_directories(pbc_pico8 PRIVATE src fatfs ${F8} ${LUA} ${MZ})

if(PBC_SERIAL_LOG)
    target_compile_definitions(pbc_pico8 PRIVATE PBC_SERIAL_LOG=1 PBC_LOG_STDOUT=1)
    pico_enable_stdio_usb(pbc_pico8 1)
else()
    target_sources(pbc_pico8 PRIVATE src/usb_msc.c src/usb_descriptors.c)
    target_include_directories(pbc_pico8 PRIVATE src/usbmsc)
    target_link_libraries(pbc_pico8 tinyusb_device tinyusb_board)
    pico_enable_stdio_usb(pbc_pico8 0)
endif()

target_compile_options(pbc_pico8 PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti -fno-exceptions -Wno-deprecated -Wno-register>
    -O2 -fsingle-precision-constant -Wno-unused-parameter -Wno-sign-compare -fstack-usage)

target_compile_definitions(pbc_pico8 PRIVATE
    PICO_FLASH_SIZE_BYTES=16777216
    PICO_STACK_SIZE=0x1000
    PICO_USE_STACK_GUARDS=1
    PICO_MALLOC_PANIC=0           # Lua handles NULL; the rest goes through pbc_mem.cpp
    PICO_CXX_DISABLE_ALLOCATION_OVERRIDES=1
    CPU_MHZ=${CPU_MHZ}
    CPU_VREG=${CPU_VREG}
    FLASH_MAX_FREQ_MHZ=${FLASH_MAX_FREQ_MHZ}
    PBC_SPI_HZ=${PBC_SPI_HZ}u
    SHOW_FPS=${SHOW_FPS}
    LUA_RESERVE_KB=${LUA_RESERVE_KB}
    LUA_USE_LONGJMP=1
    PBC_NATIVE_MENU=1             # cart menu in menu.cpp, not fake-08's Lua one
    PBC_STRIP_DEBUG=1             # no line numbers / local names: saves Lua memory
    LUAI_MAXCCALLS=120
    MINIZ_NO_STDIO MINIZ_NO_TIME MINIZ_NO_ARCHIVE_APIS MINIZ_NO_ZLIB_COMPATIBLE_NAMES
    VER_STR="pbc"
)

target_link_libraries(pbc_pico8
    pico_stdlib
    pico_multicore
    pico_unique_id
    hardware_spi
    hardware_dma
    hardware_pwm
    hardware_flash
    hardware_sync
    hardware_clocks
    hardware_pll
    hardware_vreg
    hardware_watchdog
)

# USB belongs to the mass-storage stick, and UART0's default pins are
# joystick CENTER/RIGHT -- no stdio at all.
pico_enable_stdio_uart(pbc_pico8 0)

# hot code in RAM, see src/memmap_pbc.ld
pico_set_linker_script(pbc_pico8 ${CMAKE_CURRENT_LIST_DIR}/src/memmap_pbc.ld)

pico_add_extra_outputs(pbc_pico8)
