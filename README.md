# 📡 Micro Radar
This project is a fork of [Anthony Sturdy's Micro Radar project](https://github.com/AnthonySturdy/micro-radar?tab=readme-ov-file) and adapts it for inexpensive and readily available ESP32 hardware, rather than complex devices.

---
![alt](Images/sample.gif)

### [Watch the full build here](https://www.youtube.com/watch?v=KuVckV0wF9w)

---
### What's Different?

**This fork adds:**

- ESP32-S3 Zero support
- Round GC9A01 display support
- Rotary encoder navigation
- Aircraft selection and details screen
- Enhanced aircraft icons
- Multi-color aircraft rendering
- Onboard RGB status LED
- Simplified hardware requirements
- OpenStreetMap background map, recoloured into a dark theme on the device
- Web Mercator projection, so aircraft and map line up
- Aircraft type as the details-screen heading, plus route when known
- Registration, owner and climb rate on the details screen
- Spelled-out airport towns alongside the IATA codes where they fit
- Display rotation, backlight and map options in the web panel
- Configurable OpenSky refresh rate, with its credit cost shown live
---
## Hardware

- ESP32-S3 Zero
- 240x240 Round GC9A01 Display
- Rotary Encoder
- 3D printed parts

---

## Wiring

### GC9A01 240x240 Round Display
| ESP32-S3 Zero | Display |
|----------|----------|
|GP2|SCL |
|GP3|SDA |   
|GP4|DC |   
|GP5|CS |   
|GP6|RST |   
|GP1|BLK |   
|3V3|VCC |   
|GND|GND |   

> The AZDelivery GC9A01 module is specified for 3.3 V on both logic and supply — do not connect VCC to 5V.
> BLK is driven by PWM, so the brightness can be controlled in software.

### KY-040 Rotary Encoder

| ESP32-S3 Zero | KY-040 |
|---|---|
| GP9 | CLK |
| GP8 | DT |
| GP7 | SW |
| 3V3 | + |
| GND | GND |

---

![Circuit Diagram](./Images/Circuit.png)

![Schematic Diagram](./Images/Schematic.png)
---
## Flashing the Firmware

### Option 1: Flash Prebuilt Firmware (Recommended)

No development environment required.

1. Connect the ESP32 to your computer using a USB cable.

2. Open the Tech Talkies Flasher:

   https://techtalkies.github.io/flash.html

3. Select the firmware.

4. Click **Connect** and choose your ESP32.

5. Click **Install** and wait for the process to complete.

---

### Option 2: Build and Flash from Source

If you'd like to modify the firmware, build it yourself using PlatformIO.

1. Clone or download this repository.
2. Install:

   * Visual Studio Code
   * PlatformIO extension
3. Open the project folder in VS Code.
4. Allow PlatformIO to download the required dependencies.
5. Build and upload the firmware to your ESP32 using the PlatformIO toolbar. And check if it works without modifications first.

After making your changes, rebuild and upload the firmware to your device.


---

## Configuration

The device serves a configuration panel on port 80. Open it at `http://microradar.local/`, or at the IP address the device prints to the serial log after connecting.

| Option | Meaning |
|---|---|
| Latitude, Longitude | Centre of the view |
| Radius | Half of the visible height in degrees. It is snapped to the next whole tile zoom level; the panel shows the resulting zoom and effective radius |
| Map background | Turns the map off. Nothing is downloaded when it is off |
| Dark filter | Recolours the tiles into the dark theme. Off shows the original OpenStreetMap colours, which makes the green aircraft hard to see unless you also lower the brightness |
| Map brightness | Dims the map, so aircraft and the radar sweep stay on top |
| Reload map now | Fetches the tiles again, bypassing the cache, without changing any setting |
| Backlight | Panel brightness over the BLK pin |
| Rotate by 180 degrees | For mounting the display upside down, e.g. to suit the cable routing |
| Radar sweep, Radar circles, Aircraft Info, Directional Aircraft | Radar overlay options |
| Aircraft details | Which rows the details screen shows. Type, route and registration are only requested when at least one of the three is on. Every row costs vertical space inside the round frame, so turning all of them on pushes the last one into the bezel |
| Airport names | Adds the towns the airports serve to the route row, as `Barcelona (BCN) > Munich (MUC)`. Needs Route on. A route too long for one row drops the codes first and then the towns, since a shortened town name is worth less than either |
| Climb rate | Vertical speed, amber climbing and cyan descending |
| OpenSkyAPI Client ID / Secret | Credentials for the OpenSky Network API |
| Aircraft refresh | Seconds between OpenSky requests, or 0 to spread the daily allowance evenly over 24 hours. The panel shows what the chosen rate costs per day of uptime and how long the allowance lasts at that rate |

Saving restarts the device, because position, radius, filter and brightness all change the map that is fetched once at boot.

### OpenSky credits

OpenSky rations its API by daily credits rather than by rate: 4000 with credentials, 400 without, resetting at midnight UTC. Three are held back for the access token, which lasts 29 minutes and costs credits of its own.

The automatic setting divides the day by the remaining allowance, which comes to one request every 22 seconds authenticated and every 217 seconds anonymous. A faster rate is allowed, because a radar that is only switched on in the evening can afford one the clock could not: 5 seconds costs 17280 credits per full day, but only 2160 over four hours. Once the allowance is gone the feed stops answering until the reset, so the panel spells out how many hours the chosen rate lasts. The floor is 5 seconds, below which OpenSky has no new position to give.

The finished map is cached in LittleFS as `/map.raw`, so a restart usually shows it after about 40 ms instead of downloading four tiles again. The cache is dropped when the view or the look changes, or once it is older than seven days.

---
### Credits

https://github.com/AnthonySturdy/micro-radar

Many thanks to Anthony Sturdy for creating and open-sourcing the original project that made this fork possible.

### Aircraft type and route data

OpenSky state vectors carry neither the aircraft type, the registration nor the route, so the details screen resolves the selected aircraft against [adsbdb](https://www.adsbdb.com/). No API key is needed and the requests do not count against the OpenSky budget. Only the aircraft currently shown is looked up, and every answer is cached until the device restarts.

Where adsbdb has no entry for an airframe, the type, registration and owner are looked up at [hexdb.io](https://hexdb.io/), which closes about half of those gaps. Neither database knows every aircraft, and for the remainder the operator derived from the callsign takes the place of the type.

Names from a worldwide database carry accents the display font has no glyphs for, so the accented Latin letters are folded onto their base letter: Zürich reaches the display as Zurich rather than losing the letter altogether.

The flight route data is the work of David Taylor, Edinburgh and Jim Mason, Glasgow, and may not be copied, published, or incorporated into other databases without the explicit permission of David J Taylor, Edinburgh. Aircraft data comes from Planebase.

### Map data

Map data (c) [OpenStreetMap contributors](https://www.openstreetmap.org/copyright), available under the Open Database Licence. Tiles are fetched from `tile.openstreetmap.org` under the [Tile Usage Policy](https://operations.osmfoundation.org/policies/tiles/): only the tiles for the configured viewport are requested, the result is cached locally with a seven day lifetime, and the firmware identifies itself with its own User-Agent. Spotted a mistake in the map? [Report it here](https://www.openstreetmap.org/fixthemap).
