# Plane Radar

<img width="800" height="450" alt="plane-radar" src="https://github.com/user-attachments/assets/716d0992-dab8-47ba-8f1a-2aec7f607419" />

**3D printed case (STL + assembly):** [MakerWorld](https://makerworld.com/en/models/2872376-esp32-plane-radar-live-ads-b-on-a-round-display#profileId-3207083) · **Firmware:** [Releases](https://github.com/MatixYo/ESP32-Plane-Radar/releases)

Firmware for an **ESP32-C3 Super Mini** and a **1.28″ round GC9A01** display (240×240). Shows a circular **ADS-B radar** around your configured location, with **WiFiManager** for first-time setup.

---

## What it does

1. **Wi‑Fi setup** (if needed) — captive portal on AP **`PlaneRadar-Setup`** (featuring a QR code for quick scan-to-connect access).
2. **Radar** — live aircraft from [adsb.fi](https://opendata.adsb.fi/) on a sonar-style grid (Retro Green or Multicolor themes).

After Wi‑Fi is saved, the device reconnects automatically; the radar runs in the main loop with periodic ADS-B updates (~5 s).

---

## Controls (BOOT button, active LOW)

| Action | Effect |
|--------|--------|
| **Single click** | Cycle range preset (5 → 10 → 15 → 25 km / mi); saved to NVS |
| **Double click** | Toggle theme (Retro Green Sonar sonar‑style vs. Multicolor) |
| **Hold 3 s** | Clear Wi‑Fi, location, and units; reboot into setup portal |

During setup you can also hold BOOT at power-on to force a credential reset (same as the long press).

---

## Wi‑Fi setup portal

1. Connect to **`PlaneRadar-Setup`** (or scan the QR code displayed on the screen).
2. Open **`http://plane-radar.local`** (preferred) or **`http://192.168.4.1`** — both are shown on the yellow setup screen; captive portal may open automatically.
3. Set home Wi‑Fi, coordinates, units preference (mi vs. km), and save.

**Custom fields** (stored in NVS):

| Field | Purpose |
|-------|---------|
| **Latitude / Longitude** | Radar center and ADS-B query position (defaults in `config.h` until set) |
| **Display distances in miles** | Ring scale label in **mi** instead of **km** (e.g. `6mi` vs `10km`) |

---

## Radar display

### Grid
- Dark background, subdued grid rings, and crosshairs.
- White **N / S / E / W** at the bezel; range label on the **east** spoke (ring 3 = ¾ of outer radius).
- White center dot.

### Range presets

| Ring 3 label | Outer radius (aircraft scale) |
|------------|-------------------------------|
| 5 km / 3 mi | ~6.7 km |
| 10 km / 6 mi | ~13.3 km (default) |
| 15 km / 9 mi | ~20 km |
| 25 km / 16 mi | ~33.3 km |

Preset and miles/km choice persist across reboot (`planeradar` NVS namespace).

### Aircraft
- **Inside the outer ring** — red heading triangle, magenta speed vector (clipped at the ring), callsign / type / altitude tags.
- **Outside the ring** (still within ADS-B fetch) — small **red dot on the screen rim** at the correct bearing (direction cue; not distance-accurate past the ring).
- **Tags** — placed toward the **center**: west (left) → tag on the **right** of the symbol; east (right) → tag on the **left**.

---

## Wiring (GC9A01 Display ↔ ESP32-C3 Super Mini)

| Display Pin | ESP32-C3 GPIO |
|-------------|---------------|
| **VCC**     | 3V3 |
| **GND**     | GND |
| **RST**     | GPIO **0** |
| **CS**      | GPIO **1** |
| **DC**      | GPIO **10** |
| **SDA**     | GPIO **3** (MOSI) |
| **SCL**     | GPIO **4** (SCLK) |
| **BOOT**    | GPIO **9** (Standard Boot Button) |

*(Note: Standard ESP32 dev boards are also supported for testing; pinouts are defined under `[env:esp32dev]` in `platformio.ini`)*

---

## Build

Build and upload the project using PlatformIO. The default environment is configured for the ESP32-C3 Super Mini.

```bash
# Compile and flash to ESP32-C3 Super Mini:
pio run --target upload

# (Optional) Compile and flash for standard ESP32 testing:
pio run -e esp32dev --target upload
```

---

## Memory & Performance Optimizations
* **Bluetooth Memory Release:** Reclaims ~70KB of contiguous internal RAM at startup by releasing the Bluetooth controller memory (`esp_bt_controller_mem_release`).

---

## Dependencies
- [LovyanGFX](https://github.com/lovyan03/LovyanGFX)
- [WiFiManager](https://github.com/tzapu/WiFiManager)
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson)
- [QRCode](https://github.com/ricmoo/QRCode)
