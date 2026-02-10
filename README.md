# Cat Pet – USB Drive + TFT Animation

A small desktop “pet” that shows an animated cat on a TFT display and exposes its SD card as a **USB mass-storage drive** (e.g. **G:**) so you can manage files from your PC. The cat’s idle mood is based on how full the SD card is; drink/poop animations are triggered by adding or removing files (when live tracking is enabled).

**Status:** Works with current hardware when SD init succeeds. See [CAT_PET_PROJECT_REPORT.md](CAT_PET_PROJECT_REPORT.md) for what’s implemented, what’s stable, and known limitations.

---

## Hardware

### Microcontroller

| Item | Details |
|------|--------|
| **Board** | [Seeed Studio XIAO RP2040](https://wiki.seeedstudio.com/XIAO-RP2040/) |
| **MCU** | Raspberry Pi RP2040 (dual-core ARM Cortex-M0+ @ up to 133 MHz) |
| **Flash** | 2 MB |
| **SRAM** | 264 KB |
| **USB** | Native USB (used for Serial + Mass Storage) |

### Display + SD breakout

| Item | Details |
|------|--------|
| **Product** | [Adafruit 0.96" 160×80 Color TFT Display w/ MicroSD Card Breakout - ST7735](https://www.adafruit.com/product/2088) |
| **Display** | 1.44" diagonal, 180x128 RGB, ST7735R driver |
| **SD slot** | MicroSD card slot on same breakout (shared SPI) |
| **Interface** | 4-wire SPI (shared between TFT and SD; separate chip-select pins) |

---

## Wiring (XIAO RP2040 ↔ Adafruit TFT + SD Breakout)

The sketch uses **software SPI** and the following pins. Wire the breakout to the XIAO as follows:

| Breakout label | XIAO RP2040 pin | Notes |
|----------------|-----------------|------|
| **3V or Vin** | **3.3V** | 3.3 V recommended (XIAO is 3.3 V logic). |
| **GND** | **GND** | |
| **CLK / SCK** | **GPIO 2** | SPI clock. |
| **MOSI** | **GPIO 3** | SPI data (MCU → display/SD). |
| **MISO** | **GPIO 4** | SPI data (SD → MCU); not used by TFT. |
| **TFT CS** | **GPIO 27** | TFT chip select. |
| **SD CS / SDCS** | **GPIO 26** | SD card chip select. |
| **D/C** | *(config in TFT_eSPI)* | Data/Command. Set in TFT_eSPI `User_Setup.h` (e.g. GPIO 28 or another free pin). |
| **RST** | *(config in TFT_eSPI)* | Display reset. Set in TFT_eSPI `User_Setup.h` (e.g. GPIO 29 or -1 if tied to MCU RST). |
| **Lite** | *(optional)* | Backlight. Can tie to 3.3V for always-on, or a PWM pin if you want dimming. |

**Important:** The sketch only sets **SPI pins and the two CS pins** (27 = TFT, 26 = SD) in code. **D/C** and **RST** (and optionally **Lite**) must be set in the **TFT_eSPI** library configuration so they match your wiring:

- Edit `User_Setup.h` (or a board-specific setup) inside your TFT_eSPI library folder.
- Choose a driver for **ST7735** and **180x128** (or the correct resolution for your revision).
- Set `TFT_CS = 27`, `TFT_DC` and `TFT_RST` to the GPIO numbers you actually use for D/C and RST.

---

## Software requirements

### Arduino IDE

- **Arduino IDE** 2.x (or 1.8.x)  
- **Board support:** Raspberry Pi Pico / RP2040 core that includes **Seeed XIAO RP2040**  
  - Install via **Tools → Board → Boards Manager** (search for “**rp2040**” or “**Raspberry Pi Pico**” and install the **Earle Philhower** or official **Raspberry Pi Pico** package that provides the XIAO RP2040 board).

### Board settings (for this sketch)

| Setting | Value |
|--------|--------|
| **Board** | **Seeed XIAO RP2040** (from the RP2040 board package). |
| **USB Stack** | **Adafruit TinyUSB** (required for USB mass storage). |
| **Port** | Select the COM port that appears when the XIAO is plugged in (e.g. COM9). |

### Libraries (install via Sketch → Include Library → Manage Libraries)

| Library | Purpose | Notes |
|---------|--------|--------|
| **SdFat** (by Bill Greiman) | SD card read/write, FAT, and raw sector access for USB MSC. | Used for both file access (frames) and `readSectors`/`writeSectors` in MSC callbacks. |
| **Adafruit TinyUSB** (by Adafruit) | USB device stack; provides `Adafruit_USBD_MSC` for mass storage. | Must be selected as **USB Stack** in board settings so `USE_TINYUSB` is defined. |
| **TFT_eSPI** (by Bodmer) | SPI TFT driver and sprites for the ST7735 display. | Requires editing `User_Setup.h` (or equivalent) for ST7735, resolution, and DC/RST/CS pins. |

No other libraries are required for the core sketch.

---

## SD card and frame files

- **Format:** FAT32 (recommended). exFAT may work depending on SdFat build.
- **Layout:** Place the animation folders in the **root** of the SD card:

```
/cat_idle/
  n00.rgb … n03.rgb   (normal idle, 0–25% full)
  h00.rgb … h03.rgb   (happy idle, 26–75% full)
  a00.rgb … a03.rgb   (annoyed idle, 76–100% full)
/cat_drink/
  d00.rgb … d03.rgb
/cat_poop/
  p00.rgb … p05.rgb
```

- **Frame format:** 64×64 pixels, **16-bit RGB** (little-endian), **8192 bytes** per file. Transparent color `0xF81F` is not drawn. Export from Piskel (or similar) as raw 16-bit RGB to match.

---

## Build and run

1. Install the **RP2040** board package and the **SdFat**, **Adafruit TinyUSB**, and **TFT_eSPI** libraries.
2. Select board **Seeed XIAO RP2040** and **USB Stack: Adafruit TinyUSB**.
3. Configure **TFT_eSPI** so driver, resolution, and DC/RST/CS match your wiring (see above).
4. Open `sketch_feb9a.ino`, compile, and upload.
5. Open **Serial Monitor** at **115200 baud** to see boot messages (SD init, frame load, USB MSC).
6. After a successful boot, **unplug and replug** the XIAO if the USB drive (e.g. **G:**) does not appear. The cat animation runs from RAM; the SD is exposed as a removable disk.

---

## Optional: USB-only mode (no cat, no SD access after boot)

In the sketch, set:

```c
#define USB_MSC_ONLY 1
```

Then upload. The board will only initialize the SD and start USB mass storage; it will not load frames or run the cat animation. Use this to verify that the USB drive (G:) is stable or to format/repair the SD from the PC.

---

## License and open source

This project is open source. You can use, modify, and distribute it under the terms of the license you choose (e.g. MIT, Apache 2.0). The following are used under their own licenses:

- **SdFat** – Bill Greiman (various permissive terms; see SdFat repository).
- **Adafruit TinyUSB** – Adafruit / Ha Thach (MIT).
- **TFT_eSPI** – Bodmer (see TFT_eSPI repository).

If you redistribute this sketch or a derivative, please keep attribution and, where applicable, the same open-source spirit for your changes.

---

## References

- [Seeed XIAO RP2040 wiki](https://wiki.seeedstudio.com/XIAO-RP2040/)
- [Adafruit 1.44" 180x128 TFT + MicroSD Breakout (ST7735)](https://www.adafruit.com/product/2088)
- [Adafruit TinyUSB (Arduino)](https://github.com/adafruit/Adafruit_TinyUSB_Arduino)
- [SdFat](https://github.com/greiman/SdFat)
- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI)

For implementation details, known issues, and what’s stable vs. experimental, see **[CAT_PET_PROJECT_REPORT.md](CAT_PET_PROJECT_REPORT.md)**.
