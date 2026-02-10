# Cat Pet USB Drive – Project Report

**Board:** Seeed Studio XIAO RP2040  
**Date:** February 2025  
**Sketch:** `sketch_feb9a.ino` (Cat Pet v2.4 baseline)

---

## 1. Project goal

Build a “Cat Pet” device that:

- Exposes the onboard SD card as a **real USB mass-storage drive** (e.g. **G:**) so the PC can browse and manage files.
- Shows **animated cat frames** on a TFT display, with behavior tied to SD usage:
  - **Idle moods** by storage %: 0–25% → normal (`n00.rgb`…), 26–75% → happy (`h00.rgb`…), 76–100% → annoyed (`a00.rgb`…).
  - **Drink** animation when files are **added** (usage goes up).
  - **Poop** animation when files are **deleted** (usage goes down).

---

## 2. What we achieved

### Working (when hardware cooperates)

- **USB mass storage (MSC)**  
  - The XIAO + Adafruit TinyUSB + SdFat expose the SD card as a removable drive (e.g. **G:**).  
  - Windows sees it as **FAT32**, with folders `/cat_idle`, `/cat_drink`, `/cat_poop` visible and usable.  
  - Drive is stable for normal copy/paste/delete when the board has a “good” boot.

- **TFT + cat animation**  
  - TFT_eSPI display shows the cat.  
  - Frames are loaded from SD into RAM at boot; animation runs from RAM (idle/drink/poop sequences).  
  - FPS is configurable (e.g. 1 FPS or 4 FPS via `IDLE_FPS`, `DRINK_FPS`, `POOP_FPS`).

- **Boot sequence**  
  - SD init (trying 24 → 18 → 12 → 8 MHz until one works).  
  - Load all frames from `/cat_idle` (n/h/a), `/cat_drink`, `/cat_poop`.  
  - Compute initial “used %” from FAT.  
  - Start USB MSC (setID, callbacks, capacity, setUnitReady).  
  - Cat animates; `G:` appears on the PC after (optional) unplug/replug.

- **Current stable behavior (v2.4)**  
  - After a few uploads or unplug/replug cycles, you can get: **SD init OK** (often at 18 MHz) → frames load → **cat animates smoothly** and **G: shows up stable** on the PC.

---

## 3. Intended logic (documented, partially implemented)

### Idle mood by storage %

| Storage used | Idle frames used        | Folder / files        |
|-------------|-------------------------|------------------------|
| 0–25%      | Normal                  | `/cat_idle/n00.rgb` … `n03.rgb` |
| 26–75%     | Happy                   | `/cat_idle/h00.rgb` … `h03.rgb` |
| 76–100%    | Annoyed                 | `/cat_idle/a00.rgb` … `a03.rgb` |

So: **at 26% storage used, the idle animation should switch to the happy set (`h00.rgb`, etc.); at 76%, to the annoyed set (`a00.rgb`, etc.).**  
This logic **is in the code** (thresholds `T0 = 25.0f`, `T1 = 75.0f` and selection of `frames_idle_normal` / `frames_idle_happy` / `frames_idle_annoyed`). It **does work** on the **initial** usage value read at boot. What we did **not** get working reliably is **updating** that value while the drive is mounted.

### Drink / poop reactions

- **Drink:** When **more files are added** to the SD (usage goes **up** by ≥ `DELTA_TRIGGER`, e.g. 1%), play `/cat_drink/d00.rgb` … `d03.rgb` once, then return to idle.  
- **Poop:** When **files are deleted** (usage goes **down** by ≥ `DELTA_TRIGGER`), play `/cat_poop/p00.rgb` … `p05.rgb` once, then return to idle.

We **did implement** this in code (polling `getUsedPercent()` every 2 s and comparing to `lastUsedPct` to trigger `startDrink()` / `startPoop()`).  
However, **re-reading the FAT from the MCU while Windows is using the same SD over USB** caused **instability**: drive flickering, SD init failures on next boot, white screen, “G: not usable.” So we **backed off**: in the stable v2.4 flow we effectively **freeze** usage after MSC starts (`mscStarted` / no live FAT scan), so **drink/poop reactions to file changes on G: are not reliably active** in the current “stable” configuration.  
Summary: **logic for drink/poop and for h00/a00 by % is written; live reaction to file changes on G: is not enabled in the stable setup** because it destabilizes the USB drive.

---

## 4. What we tried and what failed or was reverted

- **TinyUSB MSC setup**  
  - Tried different orders (setUnitReady before/after begin, setCapacity before/after).  
  - Added re-enumeration (detach/attach) so Windows sees MSC after upload.  
  - **Result:** Correct sequence is: configure MSC, setUnitReady(false), begin(), then setCapacity + setUnitReady(true). Re-enumeration helps when the device was already mounted.

- **“USB MSC only” mode**  
  - Added `USB_MSC_ONLY` (skip frame load and animation, no SD access after boot).  
  - **Result:** Useful to confirm that **G:** can be stable when the MCU doesn’t touch the card; confirms USB + SD hardware can work.

- **Loading frames before starting MSC**  
  - Load all cat frames and measure usage **before** calling USB MSC begin/setCapacity/setUnitReady.  
  - **Result:** Fewer races during init; cat and drive both come up when SD init succeeds.

- **Live usage updates (drink/poop)**  
  - Re-enabled `getUsedPercent()` after MSC start (removed the “freeze” when `mscStarted` is true) so the cat could react to file add/delete on **G:**.  
  - **Result:** Instability: **G:** flickering, SD init failing on next boot, white TFT. **Reverted** for the stable v2.4; drink/poop reactions are effectively off in the stable build.

- **SD init speed tuning**  
  - Tried only low speeds (12, 8, 4 MHz) to improve reliability.  
  - **Result:** On your hardware, sometimes **all** speeds failed (different SD cards, same result). Reverted to full list (24, 18, 12, 8 MHz); 18 MHz often works when the card/socket are in a good state.

- **Different SD cards**  
  - Tried a 64 GB card; still saw SD init FAIL at all speeds and/or unstable **G:**.  
  - **Result:** Suggests wiring/socket/power sensitivity rather than card size alone; 32 GB FAT32 (original) has given the best results when it does work.

---

## 5. What actually happens (hardware / environment)

- **SD init is flaky:** Same sketch and card sometimes give **SD init OK at 18 MHz** and sometimes **SD init FAIL** at 24, 18, 12, 8 MHz. No code change explains that; it points to **marginal SD hardware** (socket, wiring, 3.3 V, or card contact).
- **When SD fails:** The sketch stops at “SD card failed!” → no USB MSC, no cat, no **G:**.
- **When SD succeeds:** Frames load, MSC starts, cat animates, and **G:** appears (often after unplug/replug) and can stay stable.
- **RPI-RP2 vs COM port:** If you see **RPI-RP2** (bootloader), the application isn’t running; a single reset or clean replug usually brings back COM and then the app (and, if SD init passes, **G:** and the cat).
- **Power/cable:** Changing USB port/cable has sometimes improved stability, suggesting power or signal quality matters.

---

## 6. Current status and recommendation

- **Stable baseline:** **Cat Pet v2.4** with original 32 GB SD, speeds `{ 24, 18, 12, 8 }`, and **no live FAT scanning after MSC start**.  
  - When you get a good boot: **cat animation runs**, **G:** is stable and usable, idle mood is based on **initial** storage % (so at 26% full you’d get `h00.rgb`… in idle; at 76% full, `a00.rgb`…).  
  - **Drink/poop reactions to later file changes on G: are not enabled** in this stable setup.

- **Recommendation:**  
  - Treat this as the “shipping” behavior until hardware is more reliable (e.g. better SD module, shorter wires, stable 3.3 V).  
  - Revisit **live drink/poop** (and/or live mood updates) when using a more robust SD path or a design that only reads the FAT when the drive is safely ejected.

---

## 7. File layout on SD (for reference)

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

*Report generated from the Cat Pet USB Drive development session. Baseline sketch: `sketch_feb9a.ino` (v2.4).*
