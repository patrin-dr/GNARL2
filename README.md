# RileyLink 2.0 — ESP32 + RFM95 GNARL Firmware

RileyLink-compatible firmware for **TTGO LoRa32 V1.6.1** (868 MHz, onboard SX1276).
Implements the RFSpy/GNARL protocol for communication with Medtronic insulin pumps
via iAPS / Loop / AndroidAPS. Heltec WiFi LoRa 32 V2 and ESP32-C6 + RFM95 are
supported as alternative targets.

**Русская версия: [README_RU.md](README_RU.md)**

## Credits

This project uses code from:
- [GNARL](https://github.com/ecc1/gnarl/) — original GNARL implementation
- [Pickle](https://github.com/d3xr/pickle/) — ESP32 port

## Fork highlights

- **OTA update over Wi-Fi** — double-press RST, no cable or computer needed
- **Automatic rollback** — if a fresh OTA image doesn't confirm itself within
  60 seconds, the bootloader reverts to the previous working firmware
- **OLED status screen** — battery, phone & pump signal bars, link quality, uptime
- **TTGO LoRa32 V1.6.1 as the default board** (selected in `include/module.h`)
- **GitHub Actions CI** producing flashable binaries; pump ID injected at build
  time from workflow input or a secret
- **Power optimizations:** CPU @ 80 MHz, tickless idle, BLE light-sleep when
  disconnected, slow BLE advertising when idle, OLED screensaver
- **Robustness:** NVS corruption auto-recovery, full radio reconfiguration after
  hardware-reset recovery, battery deep-sleep protection at 3.3 V

## Supported Hardware

### TTGO LoRa32 V1.6.1 (default, SX1276 onboard)

- **MCU:** ESP32 Xtensa LX6 dual-core @ 80 MHz
- **Radio:** SX1276 in OOK mode @ 868.25 MHz (onboard, no separate RFM95 needed)
- **Flash:** 4 MB
- **Power:** JST 1.25 mm LiPo connector + TP4054 charger

| Function | GPIO |
|----------|------|
| LED | 25 |
| Button (BOOT) | 0 |
| LORA_SCK | 5 |
| LORA_MOSI | 27 |
| LORA_MISO | 19 |
| LORA_CS | 18 |
| LORA_RST | **23** |
| LORA_DIO0 | 26 |
| LORA_DIO1 | 33 |
| LORA_DIO2 | 32 |
| BATTERY_ADC | 35 (100k/100k divider) |
| OLED SDA / SCL | 21 / 22 |

### ESP32 Heltec WiFi LoRa 32 V2 (alternative)

Comment out `#define BOARD_TTGO_LORA32_V161` in `include/module.h`.

| Function | GPIO |
|----------|------|
| LED | 25 |
| Button | 0 |
| LORA_SCK | 5 |
| LORA_MOSI | 27 |
| LORA_MISO | 19 |
| LORA_CS | 18 |
| LORA_RST | 14 |
| LORA_DIO0 | 26 |
| LORA_DIO2 | 32 |
| BATTERY_ADC | 37 |

### ESP32-C6 Super Mini + RFM95 (alternative)

- **MCU:** ESP32-C6 RISC-V single-core @ 160 MHz
- **LED:** WS2812 RGB

| Function | GPIO |
|----------|------|
| LED (WS2812) | 8 |
| Button | 9 |
| LORA_SCK | 6 |
| LORA_MOSI | 3 |
| LORA_MISO | 2 |
| LORA_CS | 5 |
| LORA_RST | 7 |
| LORA_DIO2 | 14 |
| BATTERY_ADC | 0 |

```bash
idf.py set-target esp32c6
idf.py build
```

## Prebuilt Firmware via GitHub Actions (no ESP-IDF needed)

The workflow (`.github/workflows/firmware.yml`) runs on every push to `main`,
manually with parameters, and publishes a release on `v*` tags.
Each build produces three artifacts:

| Artifact | Contents | Purpose |
|----------|----------|---------|
| `rileylink-ttgo` | `rileylink-ttgo.bin` | **Merged image** (bootloader + partition table + app) — USB flashing at `0x0` only |
| `rileylink-ttgo-ota` | `pickle-stable.bin` | **App image for OTA** — uploaded via the Wi-Fi page in OTA mode |
| `rileylink-ttgo-debug` | ELF + MAP | For decoding backtraces (addr2line) after crashes |

⚠️ **Don't mix them up:** only `pickle-stable.bin` works for OTA. The merged
image won't fit an OTA slot.

### One-time setup

1. **Settings → Secrets and variables → Actions → New repository secret**:
   - `PUMP_ID` — your pump serial (6 digits)
   - `PUMP_FREQUENCY` — optional, defaults to `868250000`
2. Run **Actions → firmware → Run workflow** (optionally overriding `pump_id`,
   `pump_frequency`, `ble_name`) — binaries appear in the run artifacts.
   Or push a tag for a release:
   ```bash
   git tag v0.1 && git push origin v0.1
   ```

`pump_id` is validated (exactly 6 digits) before the build starts, and the commit
SHA is stamped into the firmware (`FW_GIT_SHA`) and printed in the boot log.

### Flashing via USB (first time / recovery)

```bash
pip install esptool
esptool.py --port /dev/ttyUSB0 write_flash 0x0 rileylink-ttgo.bin
```

- Find the port: `ls /dev/ttyUSB* /dev/ttyACM*` (Linux), `ls /dev/cu.wchusbserial*`
  (macOS), `COMx` (Windows — CH9102 driver may be required).
- If flashing fails to start, hold the **BOOT** button while running esptool
  (boards without a BOOT button usually enter download mode on their own).
- **Attach the 868 MHz antenna before powering on!**
- USB flashing is needed only once. All later updates happen over Wi-Fi (below).

## OTA Update over Wi-Fi

The flash is partitioned with two app slots (`ota_0` / `ota_1`) — a new image is
written to the free slot while the old one stays untouched as a fallback.

### How to update

1. **Enter OTA mode — double-press RST:**
   - press RST **once**;
   - **wait for the OLED** to light up with `GNARL2 <build> / 2x RST = OTA mode`
     (~1–2 s);
   - press RST a **second** time (within ~5 s of the first).

   ⚠️ Don't press the second time before the screen lights up: the counter lives
   in the app, and a press within ~0.5 s of the first lands before it starts —
   the second boot then counts as the first.
2. Connect to Wi-Fi **`GNARL-OTA`**, password **`rileylink`**.
3. Open http://192.168.4.1, pick **`pickle-stable.bin`**
   (the `rileylink-ttgo-ota` artifact of the desired build) → **Upload**.
4. Progress shows on the page and on the OLED (`[===   ] NN%`).
   The device reboots automatically when done.

### The first 60 seconds — self-validation

A freshly updated image boots in "pending verification" state. If it survives
60 seconds, it marks itself valid permanently. **If you press RST, cut power or
the firmware crashes within those 60 seconds, the bootloader automatically rolls
back to the previous image.**

That's also the manual rollback method: after an OTA update, press RST once
within the first 60 seconds.

### If an OTA upload fails midway

No harm done: a truncated image won't pass validation, and the device stays on
the old firmware. Just re-enter OTA mode and upload again.

## Building from Source

### 1. Install ESP-IDF

```bash
git clone -b v5.4.1 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf
cd ~/esp/esp-idf
./install.sh esp32
alias get_idf='. ~/esp/esp-idf/export.sh'   # add to ~/.bashrc or ~/.zshrc
```

### 2. Pump settings

The repo contains `include/pump_config.h` with a default `PUMP_ID`.
For CI builds the file is overwritten from the workflow input / secret.
For local builds, edit it:

```c
#ifndef PUMP_CONFIG_H
#define PUMP_CONFIG_H

#define PUMP_ID         "123456"      // Pump serial number
#define PUMP_FREQUENCY  868250000     // Working frequency (Hz)

#endif
```

**If you fork this repo, don't commit your real pump serial** — keep a
placeholder in git and inject the real one via CI secrets.

### 3. Build & flash

```bash
get_idf
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

After changing `sdkconfig.defaults`: `idf.py fullclean && idf.py build`.

## OLED Screen (TTGO)

The onboard SSD1306 128×64 shows (redrawn every second while BLE-connected,
every 5 s when idle):

```
MYGN    BLE:CONNECTED  <- BLE name (left) + state (right)
Batt 4.02V     87%    <- battery
BLE  [==---] -62dBm   <- phone signal bar (BLE RSSI)
Pump [==---] 5s       <- pump signal bar (RSSI) + last response age
Pump ID 123456        <- pump serial compiled into firmware
Link 98% ok           <- successful RX share, last ~100 packets
Up 02:34:17           <- uptime
```

The RSSI behind the pump bar is smoothed with an exponential moving average
(~4 packets). Pump bar scale: -95 dBm = empty, -60 dBm or stronger = full;
BLE bar scale: -90 dBm = empty, -40 dBm or stronger = full
(5 segments ≈ 20% each) — handy for finding a good placement for the device.

`Link` is a sliding window over the last ~100 pump receptions, so it reflects
the current link quality rather than a lifetime average.

**Blink alerts** (inverted line): the top line blinks while BLE is
disconnected, `Pump` blinks when the pump has been silent for over 60 s,
and `Batt` blinks below 20% charge. The screensaver still turns the display
off after 5 minutes without a BLE connection.

For the first ~1.5 s after boot a hint screen shows
`GNARL2 <build> / 2x RST = OTA mode` — it also confirms the device reached the
app (important for OTA entry timing).

### BOOT button (GPIO0)

- **Short press** — display on/off
- **Long press (>1.5 s)** — flip the screen 180° (saved to NVS, survives reboots)

Some boards ship without a physical BOOT/PRG button — then the only entry is
OTA mode via double-RST; normal bridge operation is unaffected.

## Power Management

- **Tickless idle** (`CONFIG_PM_ENABLE`) — CPU sleeps automatically in idle
- **BLE_SLEEP** — light sleep (~0.8 mA) while BLE is disconnected, waking every
  5 s to check for a connection
- **Slow advertising** — 500–800 ms interval when idle (device is discovered once,
  then stays connected)
- **OLED screensaver** — display off after 5 min without BLE connection; wakes on
  connect or button press
- **Battery protection** — deep sleep below 3.3 V (brownout detector intentionally
  disabled to avoid reboot loops on a low battery)
- CPU @ 80 MHz — more than enough for SPI @ 5 MHz + BLE

## Usage with AAPS / iAPS / Loop

1. Flash the firmware
2. Open iAPS / Loop / AndroidAPS
3. Add a new RileyLink device, **picking it from the discovered list**
4. The device auto-connects and syncs with the pump

⚠️ **Don't use "Pair RileyLink" (BLE bonding)** — the phone drops such a
connection and the app hangs at "Initializing RileyLink". Bonding isn't needed:
just pick the device from the list.

⚠️ **After a bridge reboot (RST or OTA) the app may not reconnect on its own.**
Remedies: rescan in the RileyLink settings, toggle Bluetooth, or — worst case —
restart the app. That's app behavior, not a firmware issue.

## Troubleshooting

### Device not found in the app
- Check BLE is enabled on the phone
- Restart the device
- Check the serial monitor for errors (the boot log prints the build SHA)

### Pump not responding
- Verify the `pump_id` used at build time — the OLED `Pump ID …` line shows the
  compiled-in serial
- Check the frequency (usually 868250000 Hz for EU pumps)
- Keep the pump within ~1–2 m; watch the `Pump [bar] Ns` line on the screen —
  the bar shows signal strength, the seconds show the last response age
  (the line blinks when the pump has been silent for over 60 s)

### Double-RST doesn't enter OTA mode
- You're pressing too fast: the second press must come **after the screen lights
  up** (~1–2 s after the first), not earlier
- Check the serial log for `ota: reset count N (reason R)` — the counter only
  increments on button/power resets (reason 1)

### OTA page won't open
- Make sure you're connected to the `GNARL-OTA` Wi-Fi (mobile data may hijack
  the traffic — disable LTE temporarily)
- Use http://192.168.4.1 (http, not https)

### Watchdog timeout
- Check `CONFIG_ESP_TASK_WDT_TIMEOUT_S=30` in sdkconfig.defaults
- Run `idf.py fullclean && idf.py build`

## Files

| File | Description |
|------|-------------|
| `main/main.c` | Entry point, hardware init, NVS recovery, OTA self-validation (60 s) |
| `main/ota.c`, `main/ota.h` | OTA mode: double-reset detector (NVS counter), Wi-Fi AP, HTTP upload |
| `main/gnarl.c` | RFSpy protocol handler |
| `main/ble.c` | NimBLE GATT services |
| `main/adc.c` | Battery monitoring + deep-sleep protection |
| `main/oled.c` | SSD1306 status screen, OTA screens, button |
| `partitions.csv` | Flash layout: two app slots (ota_0/ota_1) + otadata |
| `components/radio/rfm95.c` | RFM95/SX1276 driver |
| `components/medtronic/` | Medtronic protocol |
| `include/module.h` | GPIO pins, board selection (TTGO by default) |
| `include/pump_config.h` | Pump settings (default; overridden by CI) |
| `.github/workflows/firmware.yml` | CI build & release |

## License

Based on the GNARL project. See LICENSE file.
