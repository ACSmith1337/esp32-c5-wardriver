# ESP32-C5 Wardriver — with wardrift.net upload

Wardriver firmware for the ESP32-C5 (8 MB flash), based on
[JCMK's ESP32DualBandWardriver](https://github.com/justcallmekoko/ESP32DualBandWardriver),
built for the stock JCMK C5 rig (160x80 display, SD card, GPS).

It scans 2.4 GHz + 5 GHz and Bluetooth while you drive, writes the usual
KISS logs to the SD card, and uploads your data to the services you set up
in the settings page — including **wardrift.net**.

## What's new compared to the original

- **wardrift.net uploads.** The firmware now uploads your wardrive logs to
  wardrift.net, alongside WiGLE and WDG Wars. You enter your wardrift.net
  API key in the settings page of the web UI (create one under
  **API Keys** in your wardrift.net dashboard, device type `wardrive`).
  Keys are stored on the device; nothing is built into the firmware.
- **One action, three services.** The LCD menu's **Upload Wardrift** (and
  the per-file upload links in the web UI) sends the same log to all three
  services and shows a pass/fail result for each.
- **Fixed for the stock C5 board.** Display orientation, the LED/backlight
  pin conflict, and the exact flash procedure for this board were verified
  on the real hardware.
- **Ready-to-flash binaries.** `release/` contains the tested firmware for
  this board, with checksums and flash instructions.

## Getting started

1. Flash the firmware — see [`release/README-flash.txt`](release/README-flash.txt).
2. Open the web UI at the device's IP (shown on boot, or check your router's
   DHCP list).
3. On the settings page, enter your WiGLE, WDG Wars, and wardrift.net
   credentials.
4. Drive. Then upload from the LCD menu or the web UI's file list. Your
   routes show up in the wardrift.net portal.

## Building from source

`BUILD-ESP32-C5.md` has the full, tested build and flash recipe
(arduino-cli, esp32 core 3.3.0). The source is standard Arduino code in `src/`.

## Repository layout

- `src/` — firmware source
- `release/` — tested binaries, checksums, flash notes
- `BUILD-ESP32-C5.md` — build & flash recipe
- `NOTES.md` — hardware and design notes
