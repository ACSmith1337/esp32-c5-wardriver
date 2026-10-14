WARDRIVER C5 (stock JCMK rig, ESP32-C5HR8 v1.2) — verified flash set
Built: 2026-10-14 (wardrift.net ingest fixes; neutral build path)
FQBN:  esp32:esp32:esp32c5:PartitionScheme=min_spiffs,PSRAM=enabled
Flags: -DC5_WARDRIVER, -Wl,-zmuldefs (see ../BUILD-ESP32-C5.md)
SHA256 (app): bebc21b2ab608058b669eeec0d6bbabd1e603923b36a27d4ffcb597a4f7bf38a

Flash — 3 segments only:
  esptool --chip esp32c5 --port COMx --baud 921600 write_flash -z \
    0x2000   wardriver-c5-wardrift-bootloader.bin \
    0x8000   wardriver-c5-wardrift-partitions.bin \
    0x10000  wardriver-c5-wardrift-app.bin

NO image at 0xE000 (no boot_app0 / ota_data). Stock layout:
nvs@0x9000, otadata@0xE000 (managed by the bootloader), app0@0x10000.

Post-flash setup:
1. Join your WiFi (Web UI on the device's IP).
2. Settings page -> Wardrift.net -> paste your wardrift.net API key
   (Dashboard -> API Keys, device type wardrive). Key is stored on-device;
   it is never compiled into firmware.
3. Per-file "Upload Wardrift" links appear on the SD file list, or use the
   LCD menu -> Upload Wardrift.
