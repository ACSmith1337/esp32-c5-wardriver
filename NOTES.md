# Wardriver Cluster (ESP32-C5)

## Purpose
12-node ESP32-C5 wardriver cluster. JCMK ESP32DualBandWarddriver fork on XIAO ESP32-C5.

## Hardware / build
- 8MB flash: FQBN FlashSize=8M, PartitionScheme=huge_app.
- Board gate in src/configs.h (commit 2f1e7e6):
  - DEVKITC1 (default, manager/core): ESP32-C5-DevKitC-1 + JCMK 160x80 display.
    JCMK_HOST_BOARD on (non-inverted INITR_MINI160x80). User LED GPIO28 active-high.
    SELECT btn GPIO15. No fuel gauge.
  - BOARD_XIAO_C5 (nodes): onboard LED GPIO27 active-low (scan-activity blink),
    PLUGIN display init.
- Pins (shared): TFT_CS=23 DC=24 MOSI=7 SCK=6 BL=27; SD_CS=10 MISO=2.
- Flash set for DevKit: release-devkit/ (4-file C5 layout, boot_app0 at 0xE000).

## Current state
- Scan-blink LED: kept active-low on XIAO nodes to spot unpaired/working nodes in the field.
- Cluster of 12 planned — pairing/coordination design open.
- Wardrift.net batch upload feature present (per-batch TLS client, wd_seq,
  idempotency). FIXED 2026-10-14 and verified end-to-end: sequence_no is now
  millisecond-scaled (the server's space; seconds-scale sequences were all
  rejected as sequence_replay) and the JSON item builder no longer emits a
  stray quote after source_ts that invalidated every SSID row. A field C5
  upload produced a real wardrift.net route (source: API key, APs counted).
