# TheFlightWall

TheFlightWall is an ESP32-powered HUB75 LED display that shows live flight information for aircraft passing over your location — airline, route, aircraft type, registration, altitude, speed, and a bearing arrow pointing toward the plane.

Check out the build video: [instagram.com/p/DLIbAtbJxPl](https://www.instagram.com/p/DLIbAtbJxPl)

**Don't want to build one? Grab the official kit at [theflightwall.com](https://theflightwall.com)**

![Main Image](images/main-image.png)

---

## What's on screen

Each flight card shows:

```
┌──────────────────────────────────────────┐
│ [logo]  EasyJet                          │
│         LGW–AMS                          │
│         Airbus A320                      │
│         G-EUPX                           │
│ 36,000ft  494mph                    ↑    │
│ 332°  +0fpm                      7mi    │
└──────────────────────────────────────────┘
```

When multiple flights are in range the display cycles through them. Every field is individually configurable via the built-in web UI — no reflashing required.

---

## Hardware

### Component list

| Component | Notes |
|---|---|
| [HUB75 LED panels × 20](https://www.aliexpress.us/item/2255800358269772.html) | Arranged 10 wide × 2 tall |
| ESP32 dev board | We used the [R32 D1](https://www.amazon.com/HiLetgo-ESP-32-Development-Bluetooth-Arduino/dp/B07WFZCBH8); most ESP32 boards work |
| 3D-printed brackets | STL files in `hardware/` |
| 2× 63-inch wooden trim pieces | Horizontal support rails |
| [5 V 20 A+ power supply](https://www.amazon.com/dp/B07KC55TJF) | Sized for 20 panels at full brightness |
| [3.3 V–5 V level shifter](https://www.amazon.com/dp/B07F7W91LC) | Required between ESP32 data pin and panel |

### Dimensions

20 panels in a 10 × 2 arrangement — approximately 63 × 12.6 inches.

### Wiring

![Wiring Diagram](images/wiring-diagram.png)

All panels are driven by a single HUB75 data chain from the ESP32 via the level shifter. The 5 V supply powers the panels directly; the ESP32 is powered from USB or a separate 5 V rail.

![Panel Wiring and Brackets](images/led-panel-wiring-and-brackets.jpg)

The 3D-printed brackets slot the panels together edge-to-edge. MDF or cardboard works equally well for a first build.

---

## Data sources

| Source | What it provides | Required? |
|---|---|---|
| [OpenSky Network](https://opensky-network.org) | Live ADS-B positions and callsigns | Yes (fallback) |
| [FlightAware AeroAPI](https://flightaware.com/aeroapi) | Route, airline, aircraft metadata | Yes (for route info) |
| Local ADS-B receiver (tar1090 / dump1090 / readsb) | Same as OpenSky but from your own antenna | Optional |

When a local receiver is configured it is used as the **primary** source; OpenSky acts as a transparent fallback. Fetch intervals are configured independently — a local receiver can refresh every few seconds without touching your API quota.

### Setting up OpenSky

1. Register at [opensky-network.org](https://opensky-network.org)
2. Go to **My Account → API Clients** and create a new client
3. Copy the `client_id` and `client_secret`

### Setting up AeroAPI

1. Sign up at [flightaware.com/aeroapi](https://flightaware.com/aeroapi)
2. Open **API Keys → Create API Key** in the dashboard
3. Copy the generated key

Both can be entered in `firmware/config/APIConfiguration.h` before flashing, or at any time via the web UI.

---

## Software setup

### 1. Configure before flashing

Edit the files in `firmware/config/` — defaults are already in place for most settings.

| File | What to set |
|---|---|
| `WiFiConfiguration.h` | `WIFI_SSID` and `WIFI_PASSWORD` (or leave blank for captive-portal setup) |
| `APIConfiguration.h` | OpenSky credentials, AeroAPI key, optional local ADS-B host |
| `UserConfiguration.h` | Your location (`CENTER_LAT`, `CENTER_LON`, `RADIUS_KM`) and display preferences |
| `TimingConfiguration.h` | Fetch intervals, display cycle time, API cache TTLs |
| `HardwareConfiguration.h` | Panel pixel dimensions and tile arrangement |

### 2. Build and flash

1. Install [VS Code](https://code.visualstudio.com/) and the [PlatformIO IDE extension](https://platformio.org/install/ide?install=vscode)
2. Open the `firmware/` folder in PlatformIO
3. Connect the ESP32 via USB
4. Click **Upload** (→) in the PlatformIO toolbar

### 3. WiFi setup

If no WiFi credentials are baked in, the device launches a captive-portal access point named **FlightWall-Setup** on first boot. Connect from any phone or laptop, enter your network details, and the device restarts and connects automatically.

---

## Web configuration UI

Once on WiFi, the device's IP address is shown briefly on the display at startup. Open it in a browser for the live configuration page — changes apply immediately without reflashing.

### Display
- **Brightness** (0–255) and **text colour** (RGB)
- **Screen facing** — sets the compass direction the top of your screen faces, so the bearing arrow points the right way relative to your wall
- **Flip 180°** — for installations where the USB port faces up
- **Nearest flight only** — show only the closest aircraft instead of cycling
- **Show border** — draws a 1 px frame around each card
- **Flight number with logo** — when a logo is available, show the IATA flight number (e.g. EZY73CA) on line 1 instead of the airline name
- **Show aircraft type / registration** — each can be toggled independently; when one is off the other is promoted to line 3
- **Swap type and registration** — puts the registration on line 3 (larger) and the type on line 3b

### Night mode
- Scheduled brightness reduction based on local time
- Configurable start/end time and nighttime brightness level (0 = screen off, API calls suppressed)
- UTC offset for correct local time

### Local ADS-B
- Hostname or IP of a tar1090 / dump1090 / readsb receiver
- Leave blank to use OpenSky only

### Timing
- Fetch interval — local ADS-B and OpenSky fallback set independently
- Display cycle time per card
- AeroAPI cache TTL (default 30 min) and fail-cache TTL for flights with no route data

### API keys
- OpenSky client ID and secret, AeroAPI key
- Secret fields are masked — the current value is never echoed back to the browser

---

## Airline logos

Logos are stored on the ESP32's own flash (LittleFS) as pre-dithered 32 × 32 px RGB565 bitmaps, one file per airline ICAO code. Flights without a matching logo fall back to a generic aircraft icon. See `firmware/adapters/LocalLogoStore` for the file format.

---

## Thanks

Thanks to everyone who has followed along and built their own!

If you'd rather buy than build: **[theflightwall.com](https://theflightwall.com)**

Tag us in your builds — **@theflightwall** on Instagram ✈️
