# WardriverC5v2

Clean-room variant of [JCMK's ESP32DualBandWardriver](https://github.com/justcallmekoko/ESP32DualBandWardriver) v2.3.2 for the ESP32-C5.

Two additions on top of stock:

- **wardrift.net ingest.** Solo and Core logs can be uploaded to wardrift.net's device ingest API with a runtime `wdk_...` key. TLS is pinned to the ISRG roots. The key is stored on the device, never compiled in.
- **XIAO node activity LED.** A bare Seeed XIAO ESP32-C5 node build drives the onboard GPIO27 LED with scan activity, an idle heartbeat, and a distinct link-loss beacon.

The stock UI is kept. Additions are the wardrift.net settings section, per-service upload links, `Upload All`, web file delete, and bulk delete.

## Hardware

- ESP32-C5, 8 MB flash, 8 MB PSRAM.
- JCMK carrier / ESP32-C5-DevKitC-1: display, SD, GPS, fuel gauge, LED GPIO28 active-high.
- Seeed XIAO ESP32-C5 node: onboard LED GPIO27 active-low, no SD/GPS/display required.

## Build

Requires arduino-cli 1.5.x, esp32 core 3.3.0, ArduinoJson 6.18.x, NimBLE-Arduino 2.3.0, Adafruit ST7735, LinkedList, and MicroNMEA.

JCMK build:

```sh
arduino-cli compile \
  --fqbn "esp32:esp32:esp32c5:PartitionScheme=huge_app,PSRAM=enabled" \
  --build-property "compiler.cpp.extra_flags=-DC5_WARDRIVER" \
  --output-dir build/jcmk src
```

XIAO node build:

```sh
arduino-cli compile \
  --fqbn "esp32:esp32:esp32c5:PartitionScheme=huge_app,PSRAM=enabled" \
  --build-property "compiler.cpp.extra_flags=-DC5_WARDRIVER -DBOARD_XIAO_C5 -DNODE" \
  --output-dir build/xiao src
```

`huge_app` is the single-app 3 MB layout used by the stock rig. For WiFi OTA, rebuild with `PartitionScheme=default_8MB` and add an OTA handler; the first migration still needs USB.

## Flash

App-only update over an existing `huge_app` layout:

```sh
esptool --chip esp32c5 --port COMx --baud 921600 \
  write_flash -z 0x10000 build/jcmk/src.ino.bin
```

Full three-segment layout:

```sh
esptool --chip esp32c5 --port COMx --baud 921600 \
  write_flash -z \
  0x2000 build/jcmk/src.ino.bootloader.bin \
  0x8000 build/jcmk/src.ino.partitions.bin \
  0x10000 build/jcmk/src.ino.bin
```

Do not write an OTA-data blob at 0xE000 unless it is the exact matching image. A wrong blob at that offset boot-loops the C5.

## Tests

Host-side tests need no hardware:

```sh
cd tools
g++ -std=c++17 -I../src test_led_pattern.cpp ../src/LedPattern.cpp -o test_led && ./test_led
g++ -std=c++17 -I../src test_wardrift_format.cpp ../src/WardriftFormat.cpp -o test_wardrift && ./test_wardrift
g++ -std=c++17 -I../src batch_dump.cpp ../src/WardriftFormat.cpp -o batch_dump
python3 validate_batches.py <wardrive.log>
```

## Security

- Wardrift keys are entered at runtime and stored in device settings. No key is compiled into the firmware.
- wardrift.net ingest uses pinned ISRG roots, not `setInsecure()`.
- WiGLE and WDG credentials are entered at runtime through the web UI.
- The web UI has an optional Basic Auth password. Set one before exposing the device on an untrusted network.
- The `wd_key` query parameter on `/upload` exists for remote recovery. Do not use it over an untrusted network.

## License

MIT. See `LICENSE`. Upstream firmware copyright Just Call Me Koko.
