# Engineering notes

## Current build

- Two variants: `jcmk` (JCMK carrier, LED GPIO28 active-high, runtime Solo/Core/Node) and `xiao` (bare XIAO ESP32-C5, LED GPIO27 active-low, forced NODE).
- Partition scheme: `huge_app`, single 3 MB app at 0x10000. The image is about 1.86 MB.
- `huge_app` has no OTA slot. WiFi updates require rebuilding with `default_8MB` and adding an OTA handler; the first migration is still USB.
- App-only updates go to 0x10000. Full layout uses bootloader 0x2000, partitions 0x8000, app 0x10000. No OTA-data write at 0xE000.

## wardrift.net

- Two paths. Account route: sign in at `/v1/auth/sign-in/username` with
  `{"username","password","remember_me":true}`, cache the returned
  `session.token` in `/wd_session`, then POST the raw WiGLE CSV to
  `/v1/wardrive/logs` as multipart (`file`, `public_route`) with
  `Authorization: Bearer`. This creates the archived route the browser upload
  creates. A 401 drops the cached token and signs in once more; 409
  `duplicate_route` counts as already uploaded.
- Device path: `POST https://wardrift.net/v1/ingest/signals` with `X-API-Key`.
  Individual signals, no archived route. Used when no account is configured,
  or as a fallback when account sign-in is rejected and a key exists.
- The account username and password live in settings `wd_user` / `wd_pass`.
  `Clear account` on the config page removes both and the cached session.
- TLS uses pinned ISRG roots. No `setInsecure()`.
- NTP must be synced before the first upload.
- Batches are 250 rows. The parser accepts both 11-column legacy and 14-column current WiGLE CSV. Empty SSIDs are preserved.
- The idempotency key is derived from the batch content and API key, so a retry after a lost response is recognized as a duplicate instead of being counted twice.
- A 4xx payload rejection (400/413/415/422) splits the batch in half and retries. A single row that still fails is dropped and logged; the file fails only when nothing in the batch is accepted, and a run of 10 drops with no acceptance aborts as a global rejection.
- Sequence numbers persist in `/wd_seq`. A 409 `sequence_replay` advances the sequence and retries.
- Resume checkpoints live in `/wd_resume`, keyed by leaf filename and file size.
- Headers-only logs return `no ingestable rows` and do not write a sidecar.
- The device key must be an active `wdk_...` device key. Rotating it invalidates the old value immediately. Device ingest data appears in the signals and map feeds, not in the web account's manual log upload list.

## Web UI

- `Upload All` runs WiGLE, WDG Wars, and wardrift.net. Per-service links remain.
- WiGLE and WDG responses are read to Content-Length and checked for a body verdict. A 200 is not assumed to be success when the body says otherwise.
- `Delete` removes a log and its `.wigle`, `.wdg`, and `.wardrift` markers. The active log is refused.
- `Delete All Logs` removes every `.log` except the active log and `debug.log`.
- `Delete Uploaded Only` removes only logs that have all three service markers.
- Settings JSON is 8192 bytes. At 4096 a long wardrift key could be saved truncated.
- A wardrift key can be supplied as `wd_key` on `/upload` to install it without the `/save` handler stopping the web server.

## XIAO node

- LED engine is pure `LedPattern` plus an Arduino binding.
- Scan yield produces one, two, or three short pulses. A quiet node emits a heartbeat. A bound node that loses its Core emits a distinct two-flash fault beacon.
- The XIAO build forces NODE at boot. The admin web UI has an idle timeout so the node resumes scanning.

## Known constraints

- Wardrift keys expire or can be rotated. A 401 from a host probe means the key is no longer valid.
- Many short `wardrive_*.log` files are header-only because no GPS fix was present. They cannot be uploaded.
- The SD mount path retries at 4 MHz and then 1 MHz. A card that fails all retries needs a power cycle and reseat; a hard reset does not power-cycle the card.
- The device joins the saved WiFi at boot and serves the admin page until the link drops or Save is pressed. Holding SELECT during boot skips the admin page.

## Tests

Host tests, no hardware:

```sh
g++ -std=c++17 -I../src test_led_pattern.cpp ../src/LedPattern.cpp -o test_led
g++ -std=c++17 -I../src test_wardrift_format.cpp ../src/WardriftFormat.cpp -o test_wardrift
```

`batch_dump` plus `validate_batches.py` checks the real parser and envelope against field logs.
