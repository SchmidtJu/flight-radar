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
- Display rotation, backlight and map options in the web panel
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
| Radar sweep, Aircraft Info, Directional Aircraft | Radar overlay options |
| OpenSkyAPI Client ID / Secret | Credentials for the OpenSky Network API |

Saving restarts the device, because position, radius, filter and brightness all change the map that is fetched once at boot.

The finished map is cached in LittleFS as `/map.raw`, so a restart usually shows it after about 40 ms instead of downloading four tiles again. The cache is dropped when the view or the look changes, or once it is older than seven days.

---
### Credits

https://github.com/AnthonySturdy/micro-radar

Many thanks to Anthony Sturdy for creating and open-sourcing the original project that made this fork possible.

### Map data

Map data (c) [OpenStreetMap contributors](https://www.openstreetmap.org/copyright), available under the Open Database Licence. Tiles are fetched from `tile.openstreetmap.org` under the [Tile Usage Policy](https://operations.osmfoundation.org/policies/tiles/): only the tiles for the configured viewport are requested, the result is cached locally with a seven day lifetime, and the firmware identifies itself with its own User-Agent. Spotted a mistake in the map? [Report it here](https://www.openstreetmap.org/fixthemap).
