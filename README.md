# ESP32-Cam PIR Sensor

A motion-triggered security camera built on the **AI Thinker ESP32-Cam** board.
An **HC-SR501 PIR sensor** wakes the board from deep sleep the instant motion is
detected, takes a photo, saves it to internal flash (SPIFFS), and then opens a
**WiFi Access Point** so you can browse to a built-in web page and view or
download every stored image — all without unplugging the USB cable.

---

## Features

| Feature | Detail |
|---|---|
| **Deep sleep** | < 1 mA at rest; wakes only on PIR trigger |
| **Auto camera detection** | Identifies OV2640, OV2660, OV3660, OV5640 at runtime |
| **High-res capture** | UXGA (1600×1200) when PSRAM present; SVGA (800×600) otherwise |
| **Rolling image store** | Up to 5 JPEGs on micro SD card (oldest overwritten automatically) |
| **WiFi viewer** | Built-in AP + web server; no app required |
| **On-demand capture** | "Capture now" button on the web page for manual shots |
| **Verbose serial log** | Every step printed at 115200 baud with a clear `[Tag]` prefix |

---

## Hardware required

| Component | Notes |
|---|---|
| AI Thinker ESP32-Cam | Any revision; ships with OV2640 |
| HC-SR501 PIR module | Standard passive infrared motion sensor |
| Micro SD card | Up to 32 GB (exFAT or FAT32 formatted) |
| FTDI / CH340 USB-serial adapter | For programming and serial monitor |
| Jumper wires | |
| 5 V power supply or USB power bank | |

---

## Wiring

### HC-SR501 → ESP32-Cam

```
HC-SR501        AI Thinker ESP32-Cam
─────────       ────────────────────
VCC    ──────►  5 V  (or 3.3 V — check your module)
GND    ──────►  GND
OUT    ──────►  GPIO 13
```

> **Note:** The HC-SR501 prefers a 5 V supply for reliable operation.
> GPIO 13 on the AI Thinker board is a free RTC-capable GPIO when no SD card
> is fitted; it is the same pin used for deep-sleep EXT0 wakeup.

### FTDI adapter → ESP32-Cam (for programming only)

```
FTDI            AI Thinker ESP32-Cam
────            ────────────────────
5 V   ──────►  5 V
GND   ──────►  GND
TX    ──────►  U0R  (GPIO 3)
RX    ──────►  U0T  (GPIO 1)
GND   ──────►  IO0  (hold LOW during upload, release after)
```

> **Tip:** Briefly connect IO0 to GND, press the reset button, then upload.
> Release IO0 and press reset again after flashing.

---

## Software setup

### 1. Install board support

1. Open **Arduino IDE** → *File → Preferences*.
2. Add to "Additional Boards Manager URLs":
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Go to *Tools → Board → Boards Manager*, search **esp32**, install the
   **Espressif Systems** package (version 2.x or later).

### 2. Insert micro SD card

1. Format your micro SD card as **FAT32** or **exFAT** on your computer.
2. Insert it into the SD card slot on the bottom of the AI Thinker ESP32-Cam board.

### 3. Select board and partition

| Setting | Value |
|---|---|
| Board | **AI Thinker ESP32-CAM** |
| Partition Scheme | **Huge APP (3MB No OTA / 1MB SPIFFS)** |
| Upload Speed | 115200 |
| Flash Frequency | 80 MHz |

### 4. Open the sketch

Open `ESP32-Cam-PIR-Sensor.ino` in Arduino IDE.  All required libraries
(`esp_camera`, `SD_MMC`, `WiFi`, `WebServer`) are bundled with the ESP32 board
package — no extra library installs needed.

### 5. (Optional) Customise settings

At the top of `ESP32-Cam-PIR-Sensor.ino` you can adjust:

```cpp
#define PIR_PIN                  GPIO_NUM_13   // GPIO for PIR OUT
#define FLASH_LED_PIN            4             // Onboard flash LED
#define AP_SSID                  "ESP32-Cam-PIR"
#define AP_PASSWORD              "12345678"
#define WIFI_ACTIVE_DURATION_MS  30000UL       // 30 s WiFi window
#define MAX_IMAGES               5             // Rolling store size
#define FLASH_DURATION_MS        150           // Flash LED on-time per capture
#define PIR_SETTLE_TIMEOUT_MS    5000          // Max wait for PIR to go LOW before sleep
```

### 6. Upload

Hold **IO0 to GND**, press **Reset**, click **Upload**.
Release **IO0**, press **Reset** once more after flashing is complete.

---

## Using the device

### Normal operation (standalone)

1. Power the board from a USB power bank or 5 V adapter.
2. The board will enter deep sleep immediately.
3. Walk in front of the PIR sensor — the board wakes, takes a photo, and
   opens the WiFi access point.
4. On your phone or laptop, connect to:
   - **SSID:** `ESP32-Cam-PIR`
   - **Password:** `12345678`
5. Browse to **http://192.168.4.1** to see the latest photo.
6. After 30 seconds of inactivity the AP closes and the board sleeps again.

### Debugging with USB connected

1. Leave the USB cable plugged in and open the serial monitor at **115200 baud**.
2. Trigger the PIR (wave your hand in front of it).
3. Watch the serial output — every step is logged:

```
============================================================
  ESP32-Cam PIR Sensor  —  starting up
============================================================
[Boot]   Wakeup reason: EXT0 — PIR motion detected!
[SD]     OK  total=15876 MB
[Camera] PSRAM detected → UXGA (1600×1200), quality 10
[Camera] Sensor PID: 0x2641  →  OV2640
[Camera] INIT OK
[Setup]  Capturing image...
[Capture] Frame: 92416 bytes  format=4
[Capture] SUCCESS — saved /sd/img_3.jpg (92416 bytes)
[Setup]  Capture: SUCCESS ✓
[WiFi]   Starting AP  SSID='ESP32-Cam-PIR'  password='12345678'
[WiFi]   AP IP: 192.168.4.1
[WiFi]   Web server up → http://192.168.4.1
[WiFi]   Connect your phone/laptop to WiFi 'ESP32-Cam-PIR' (password: '12345678')
[WiFi]   AP will stay active for 30 s then board returns to deep sleep
```

4. Connect to the AP on a second device (phone) and browse images while
   keeping the serial monitor open on your laptop.

### Web interface pages

| URL | Description |
|---|---|
| `http://192.168.4.1/` | Home — latest image + navigation |
| `http://192.168.4.1/gallery` | Grid of all stored images |
| `http://192.168.4.1/image` | Raw JPEG of the latest capture |
| `http://192.168.4.1/file?name=/img_N.jpg` | Specific stored image |
| `http://192.168.4.1/capture` | Trigger an immediate capture |

---

## How it works (architecture overview)

```
Power on / PIR wakes board
        │
        ▼
  Print wakeup reason (Serial)
        │
        ▼
  Mount SD card  ──FAIL──► Serial error, sleep
        │
        ▼
  Init camera
  (detect OV2640 / OV2660 / OV3660 / OV5640)  ──FAIL──► Serial error, sleep
        │
        ▼
  Flash LED ON → grab frame → Flash LED OFF
  Write JPEG to /sd/img_N.jpg  ──FAIL──► Serial error (continue to WiFi)
        │
        ▼
  Start SoftAP  (SSID: ESP32-Cam-PIR)
  Start WebServer on port 80
        │
        ▼
  loop(): serve HTTP clients for WIFI_ACTIVE_DURATION_MS
        │
        ▼
  Wait for PIR OUT to go LOW (max 5 s)
  Deinit camera, stop WiFi
  esp_sleep_enable_ext0_wakeup(GPIO_13, HIGH)
  esp_deep_sleep_start()  ◄─────────────────────────┐
        │                                             │
        └──── deep sleep until next PIR trigger ──────┘
```

---

## Supported camera modules

| Camera | PID | Auto-detected | Notes |
|---|---|---|---|
| OV2640 | 0x2641 | ✓ | Default module on AI Thinker; vflip applied |
| OV2660 | 0x2660 | ✓ | Rare variant |
| OV3660 | 0x3660 | ✓ | Brightness / saturation tweaked |
| OV5640 | 0x5640 | ✓ | vflip applied |

Unknown sensors are accepted and will be identified by their raw PID in the
serial log.

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| Camera init fails (0x20004) | Wrong board selected | Select **AI Thinker ESP32-CAM** |
| `[SD] Mount FAILED` | SD card not inserted or damaged | Insert formatted SD card or try on computer |
| Board doesn't wake on motion | PIR wired to wrong pin | Check OUT → GPIO 13 |
| Images are upside-down | Camera mounted differently | Toggle `s->set_vflip()` in `initCamera()` |
| WiFi AP not visible | Too far, or still sleeping | Move closer; wave in front of PIR |
| `No image available` on web page | Capture failed | Check serial log for reason |

---

## Licence

Released under the [MIT Licence](LICENSE).
