#!/usr/bin/env bash
#
# Baut PBC-08 fuer den PicoBoy Color Plus.
#
# Aufruf:   ./build.sh                 bauen
#           ./build.sh clean           Bauordner loeschen und neu bauen
#           ./build.sh -DCPU_MHZ=336   CMake-Optionen durchreichen
#
# Ergebnis: dist/pbc08.uf2
#
# Das Pico SDK wird aus PICO_SDK_PATH genommen, sonst aus dem gemeinsamen
# Zwischenspeicher der pbc-plus-Projekte (~/.cache/pbcp-sdks/pico-sdk).
set -euo pipefail
HIER="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${HIER}"

if [ "${1:-}" = clean ]; then
    rm -rf build
    shift
fi

for t in cmake arm-none-eabi-gcc python3; do
    command -v "$t" >/dev/null || { echo "FEHLER: $t fehlt (siehe README.md)" >&2; exit 1; }
done

if [ -z "${PICO_SDK_PATH:-}" ] || [ ! -f "${PICO_SDK_PATH}/pico_sdk_init.cmake" ]; then
    PICO_SDK_PATH="${HOME}/.cache/pbcp-sdks/pico-sdk"
    if [ ! -f "${PICO_SDK_PATH}/pico_sdk_init.cmake" ]; then
        echo "==> Hole Pico SDK 2.1.0 nach ${PICO_SDK_PATH} (einmalig)"
        mkdir -p "$(dirname "${PICO_SDK_PATH}")"
        git clone --branch 2.1.0 --depth 1 https://github.com/raspberrypi/pico-sdk.git "${PICO_SDK_PATH}"
        git -C "${PICO_SDK_PATH}" submodule update --init --depth 1
    fi
fi
export PICO_SDK_PATH
export PICOTOOL_FETCH_FROM_GIT_PATH="${PICOTOOL_FETCH_FROM_GIT_PATH:-${HOME}/.cache/pbc_plus_picotool}"

GEN=()
command -v ninja >/dev/null && GEN=(-G Ninja)
# CMake 4 lehnt das cmake_minimum_required mancher SDK-Teile ab.
case "$(cmake --version | head -1 | awk '{print $3}')" in
    4.*) export CMAKE_POLICY_VERSION_MINIMUM=3.5 ;;
esac

cmake -S . -B build "${GEN[@]}" -DCMAKE_BUILD_TYPE=Release "$@" >build.cmake.log 2>&1 \
    || { tail -20 build.cmake.log; exit 1; }
cmake --build build -j"$(nproc 2>/dev/null || echo 2)" >build.log 2>&1 \
    || { grep -E "error|Error" build.log | head -20; exit 1; }

mkdir -p dist
cp -f build/pbc08.uf2 dist/pbc08.uf2
arm-none-eabi-size -A build/pbc08.elf | awk '
    $1==".data"||$1==".bss" {ram+=$2}
    $1==".text"||$1==".rodata"||$1==".data"||$1==".binary_info" {flash+=$2}
    END {printf "    Firmware %d KB (Platz bis 1024), statisches RAM %d KB von 512\n", flash/1024, ram/1024}'
echo " OK dist/pbc08.uf2"
echo
echo "  Flashen: BOOTSEL gedrueckt halten, USB anstecken, die .uf2 aus"
echo "           dist/ auf das erscheinende Laufwerk kopieren."
