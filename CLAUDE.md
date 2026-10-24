# PBC-08 – Arbeitsnotizen

PICO-8-kompatibler Player (fake-08 + z8lua) für den PicoBoy Color Plus.
Repo: https://github.com/hcl1981/pbc-08 (öffentlich, Release v1.0 mit UF2).
Nutzer schreibt Deutsch; Antworten auf Deutsch. README.md beschreibt Bedienung und Aufbau.

## Arbeitsweise mit dem Nutzer

- Ist das Gerät im BOOTSEL-Modus („ist im boot modus“), selbst flashen:
  `tools/flash.sh` (wartet bis 5 min auf `/run/media/$USER/RP2350`).
- Im USB-Stick-Modus liegt das Laufwerk unter `/run/media/$USER/PICO8`
  (Carts, `pico8.cfg`, `cdata/` mit Spielständen und ROM-Caches).
- Nach Firmware-Änderungen: bauen, flashen, dann um einen Test am Gerät bitten.
- Name ist „PBC-08“ (Verweis auf fake-08; nie „PICO“ im Namen, Marke von Lexaloffle).
- Commits als hcl1981 mit GitHub-noreply-Adresse; BBS-Carts nie committen.

## Hardware (PBC+)

- RP2350A, 520 KB SRAM, kein PSRAM, 16 MB Flash, 16 KB XIP-Cache.
- ST7789 240×280 an SPI0: SCK 18, MOSI 19, CS 10, DC 8, RST 9, Backlight 26 (PWM);
  Y-Offset 20, MADCTL 0xC8, BGR565, INVON.
- Tasten aktiv low: CENTER 0, RIGHT 1, DOWN 2, LEFT 3, UP 4, A 27, B 28.
- Ton: PWM GP15 + RC-Glied. LEDs 12/13/14.
- Takt 250 MHz bei 1,25 V. Ein Reboot direkt aus dem übertakteten Zustand legt
  das Gerät tot → `restart_to_menu` taktet erst auf 150 MHz/Standard-VREG zurück.
- Pico SDK 2.1.0 unter `~/.cache/pbcp-sdks/pico-sdk`.
- Referenzen: `~/Documents/pbc_megadrive` (Display/Tasten/Ton),
  `~/Documents/micropython_berühren/pbc-plus/docs/HARDWARE.md`.

## Aufbau

- Kern 1: VM (`src/pico8_run.cpp`, 32-KB-Stack, Bedarf ~14 KB).
  Kern 0: Bildausgabe (zwei Frame-Puffer, Spinlock) und `audio_service`.
- `src/menu.cpp`: natives Cart-Menü vor der VM (läuft rund; Ordner `/x/x.p8.png`
  = eine Spielkarte). Verlassen eines Spiels → Neustart ins Menü.
- `src/main_pbc.cpp`: Boot, Takte, Watchdog (3 s), VM-Hänger (8 s), HardFault,
  `boot_note` im uninitialisierten RAM → Fehlerkarte im Menü.
- Speicher: `pbc_lua_may_grow` begrenzt Lua (Lua + C++ + Reserve ≤ Heap),
  Notfall-GC in `pbc_malloc`; OOM → „not enough memory“ und zurück ins Menü.
- `src/memmap_pbc.ld`: Lua-Kern, Synth, Skalierer, Display, Audio im RAM.
- Flash: 0–2 MB Firmware, 2 MB–0xFF0000 FatFs „PICO8“.
- fake-08-Änderungen (in `fake08/`, Original in `third_party/` – gitignored):
  kein regex/iostream, Streaming-PNG (tinfl), zeilenweiser .p8-Parser, float-Synth
  ohne Hall, schlankes Bios, ROM-Cache `/cdata/<fnv>.rom`, Debug-Info verworfen,
  Lua-Quelltext nach dem Übersetzen freigegeben, `load()` wie PICO-8
  (Wechsel vormerken + Coroutine per Yield verlassen; fehlende Datei → false).

## Bauen und testen

    ./build.sh                       # -> dist/pbc08.uf2
    ./build.sh -DPBC_SERIAL_LOG=ON   # Debug-UF2 mit USB-CDC-Log ([hb]-Zeilen), ohne USB-Stick
    tools/serial_log.sh              # Log mitschreiben (FLASH=1 flasht vorher die Debug-UF2)
    stty -F /dev/ttyACM0 1200        # Debug-Firmware → BOOTSEL

PC-Testumgebung `hosttest/` (Testcarts in `hosttest/bbs`, nur lokal):

    make -C hosttest pico8_host menu_host label_host luadump_host
    cd hosttest && HEAP_TOTAL=380000 N=1800 ./runall.sh   # ≈ Speicher des Geräts
    PF_ROOT=. SCREEN=menu FRAMES=60 SHOTS=59 ../menu_host  # Menü-Screenshot (.ppm)

Umgebungsvariablen von `pico8_host`: FRAMES, KEYS (frame:bits,…), SHOTS, WAV,
HEAP_TOTAL, MARK_GC, PBC_NO_ROMCACHE, PBC_FIXED_SEED. Die Zahlen sind 64-Bit;
512px Under (live ~227 KB auf dem PC) ist die größte Cart, die am Gerät stabil läuft.

## Stand (2026-10-23)

- Am Gerät getestet: Celeste, Pico Valley, Crimson Night, Dust Bunny, 512px Under.
- Auf dem Gerät: zusätzlich Carrot Kingdom, Peak of Empires, Loop, Molemole, Rubyeyes.
- Zu groß: Touhou Unmei no Hoshi, Pico Backrooms (Level > 500 KB Lua), Tad.
- Offen: Firmware mit „PBC-08“-Titel noch nicht geflasht; Advent 2025 stürzt am PC
  bei knappem Heap ab (rc=139) – ansehen; mögliche Einsparungen: Lua-Reserve 16 KB,
  ein einzelner Frame-Puffer, weniger Code im RAM.
