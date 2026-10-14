# Building & flashing for ESP32-C5 (stock JCMK rig)

Verified working 2026-10-13 on ESP32-C5HR8 v1.2 (8 MB flash), stock JCMK board.

## Build (arduino-cli, esp32 core 3.3.0)

```
arduino-cli compile \
  --fqbn "esp32:esp32:esp32c5:PartitionScheme=min_spiffs,PSRAM=enabled" \
  --build-property "compiler.cpp.extra_flags=-DC5_WARDRIVER" \
  --output-dir <out> src
```

Plus this linker flag in the esp32 core `platform.txt`
(`~/.arduino15/packages/esp32/hardware/esp32/3.3.0/platform.txt`):

```
compiler.c.elf.extra_flags=-Wl,-zmuldefs
```

(C5-compat flag from the stock CI. Required or the app faults at first `Serial.println`.)

Result: ~1.84 MB app (93 % of the 1.875 MB app0 partition).

## Flash — 3 segments only

```
esptool --chip esp32c5 --port COMx --baud 921600 \
  write_flash -z 0x2000 bootloader.bin 0x8000 partitions.bin 0x10000 src.ino.bin
```

esptool v5: no `--before`/`--after` flags; the reset is built in.

**Do NOT flash anything at 0xE000** (no boot_app0 / ota_data). Stock layout is
nvs@0x9000, otadata@0xE000 (managed by the bootloader, not written), app0@0x10000.
A phantom 4th image at 0xE000 caused a CPU_LOCKUP boot-loop.

Bootloader/partitions: use the stock stock release pair for this board — they are
byte-identical to what arduino-cli emits for this FQBN.

## Verify

1. `esptool read_flash` + sha256 of 0x10000 region == local `src.ino.bin`.
2. Serial 115200: ESP-IDF banner, settings OK, SD mounted, no Guru Meditation.
3. Web UI at the device's IP (shown on boot / your DHCP lease): **Wardrift.net**
   section with `wd_key` field, plus per-file "Upload Wardrift" links on the SD file list.

## Notes

- Board pins in `configs.h` are the stock JCMK rig (C_BTN 1, HAS_BATTERY,
  LED_PIN 28 active-high). `LED_ON`/`LED_OFF` are defined to HIGH/LOW to match.
  The bare-DevKitC-1 board gate from the upstream fork does NOT fit this rig and
  must not be used here.
- Wardrift.net ingest is the non-TLS variant (HTTP/1.1 + manual handshake to
  `wardrift.net:443 /v1/ingest/signals`), per batch of 100 rows, sequence persisted
  in SPIFFS `/wd_seq`. Key is entered at runtime in the web UI — never hardcoded.
