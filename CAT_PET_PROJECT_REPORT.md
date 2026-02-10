# Cat Pet USB Drive – Project Report (v2.5 Stable)

**Board:** Seeed Studio XIAO RP2040  
**Date:** February 2026  
**Status:** v2.5 stable build

---

## 1. Project goal

Build a USB “Cat Pet” device that:

- Exposes its SD card as a real USB mass-storage drive (G:)
- Plays animated cat moods on a TFT display
- Reacts to user file activity:
  - Copy → drink animation
  - Delete → poop animation
- Changes idle mood based on storage fullness

This device behaves like a normal USB flash drive with a personality.

---

## 2. What was achieved


### USB + SD stability

- USB MSC and TFT now coexist without corruption
- Drive mounts reliably after plug-in
- Windows browsing does not break animation
- No SD FAT scanning while mounted (prevents instability)

Key fix: strict SPI bus discipline + USB activity timing guard.

---

### Animation engine

- Frames loaded into RAM at boot
- Sprite scaling 64×64 → 136×136
- Stable animation timing
- No tearing or green stripe artifacts

---

### Behavior logic

#### Idle mood by storage %

| Storage used | Mood | Frames |
|-------------|------|--------|
| 0–25% | Normal | n*.rgb |
| 26–75% | Happy | h*.rgb |
| 76–100% | Annoyed | a*.rgb |

Storage is measured at boot for stability.

---

#### File reaction logic (v2.5)

The system classifies USB write sessions:

- Large write session → **Drink**
- Small metadata session → **Poop**
- Tiny background writes → ignored

This avoids FAT scanning and still reacts to real user actions.

Behavior confirmed stable across:

- small text files
- PDFs
- images
- folders
- zip archives
- multi-MB files

---

## 3. Boot sequence

1. Initialize TFT
2. Initialize SD (12 MHz stable)
3. Load all frames into RAM
4. Measure storage fullness
5. Start USB MSC
6. Cat begins idle animation
7. G: drive appears on host

No Arduino IDE required after flashing.

---

## 4. Current stability notes

- Device runs standalone after upload
- Works on any PC with USB mass storage support
- No driver required
- Animation never blocks USB
- USB never corrupts display

This is now safe for daily use.

---

## 5. Known intentional design choices

- Storage % is frozen after boot (no live FAT scanning)
- Tiny metadata actions may trigger poop (harmless + funny)
- USB bandwidth is limited by shared SPI bus (~350–500 KB/s)

These are acceptable tradeoffs for stability.

---

## 6. File layout on SD (for reference)

```
/cat_idle/
  n00.rgb … n03.rgb   (normal idle, 0–25% full)
  h00.rgb … h03.rgb   (happy idle, 26–75% full)
  a00.rgb … a03.rgb   (annoyed idle, 76–100% full)
/cat_drink/
  d00.rgb … d03.rgb   (play when usage goes up)
/cat_poop/
  p00.rgb … p05.rgb   (play when usage goes down)
```

Frame format: 64×64 pixels, 16-bit RGB, 8192 bytes per file. Transparent color `0xF81F`.

---

*Report generated from the Cat Pet USB Drive development session. Baseline sketch: `cat_pet_v2.5.ino` (v2.5).*
