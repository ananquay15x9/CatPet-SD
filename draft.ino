/*
 * Cat Pet - USB DIAGNOSTIC Version (2.4v stable BASE)
 * 2.5v base: - Updated to use SdFat's SdCard interface for more direct sector access, improving USB MSC stability. 1:58AM
 */ 

#include <SPI.h>
#include <TFT_eSPI.h>
#include <SdFat.h>

// Set to 1 to test USB Mass Storage ONLY (no cat animation, no SD usage from MCU after boot)
#define USB_MSC_ONLY 0

// Check if TinyUSB is available
#ifdef USE_TINYUSB
    #include <Adafruit_TinyUSB.h>
    #define USB_AVAILABLE 1
#else
    #define USB_AVAILABLE 0
#endif

// -------------------- PINS --------------------
static const uint8_t SPI_SCK_PIN  = 2;
static const uint8_t SPI_MOSI_PIN = 3;
static const uint8_t SPI_MISO_PIN = 4;
static const uint8_t TFT_CS_PIN   = 27;
static const uint8_t SD_CS_PIN    = 26;

// -------------------- DISPLAY + SPRITE --------------------
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);

static const int16_t FRAME_W = 64;
static const int16_t FRAME_H = 64;
static const float   SCALE   = 2.125f;   // 64 -> 136
static const int16_t SPRITE_W = 136;
static const int16_t SPRITE_H = 136;

static const uint16_t BG_COLOR = TFT_BLACK;
static const uint16_t TRANSPARENT_COLOR = 0xF81F;

int16_t spriteX = 0;
int16_t spriteY = 0;

// -------------------- SD CARD --------------------
SdFat  sd;
File32 file;

#if USB_AVAILABLE
volatile bool usbActive = false;
volatile uint32_t lastUsbMs = 0;         // NEW: tracks recent USB activity
volatile uint32_t lastWriteSeenMs = 0;

volatile uint32_t sessMinLBA = 0xFFFFFFFF;
volatile uint32_t sessMaxLBA = 0;
volatile uint32_t sessWriteCalls = 0;
volatile uint32_t sessStartMs = 0;

uint32_t lastSessionHandledMs = 0;

// session ends after no writes for this long
static const uint32_t WRITE_IDLE_END_MS = 1200;

// --- tune thresholds ---
static const uint32_t DRINK_CALLS_THRESHOLD = 50;
static const uint32_t DRINK_MIN_DURATION_MS = 600;  // NEW: must be writing for >=0.6s to drink
static const uint32_t IGNORE_CALLS_MAX = 6;         // NEW: tiny sessions -> ignore


Adafruit_USBD_MSC usb_msc;
bool mscStarted = false;

// IMPORTANT: Prevent TFT from being selected during SD sector IO.
// This avoids the "green stripes / tearing" when Windows touches G:.
static inline void forceTFTDeselected() {
    digitalWrite(TFT_CS_PIN, HIGH);
}

// MSC callbacks
int32_t msc_read_callback(uint32_t lba, void* buffer, uint32_t bufsize) {
    usbActive = true;
    lastUsbMs = millis();

  forceTFTDeselected();  // NEW: ensure TFT is not selected
    bool result = sd.card()->readSectors(lba, (uint8_t*)buffer, bufsize / 512);

    lastUsbMs = millis();
    usbActive = false;
    return result ? (int32_t)bufsize : -1;
}

int32_t msc_write_callback(uint32_t lba, uint8_t* buffer, uint32_t bufsize) {
    usbActive = true;
    lastUsbMs = millis();

    lastWriteSeenMs = millis();
    sessWriteCalls++;
    if (sessWriteCalls == 1) sessStartMs = millis();


    uint32_t sectors = bufsize / 512;
    uint32_t endLBA  = lba + (sectors ? (sectors - 1) : 0);

    if (lba < sessMinLBA) sessMinLBA = lba;
    if (endLBA > sessMaxLBA) sessMaxLBA = endLBA;

  forceTFTDeselected();  // NEW
    bool result = sd.card()->writeSectors(lba, buffer, bufsize / 512);

    lastUsbMs = millis();
    usbActive = false;
    return result ? (int32_t)bufsize : -1;
}

void msc_flush_callback(void) {
    usbActive = true;
    lastUsbMs = millis();

  forceTFTDeselected();  // NEW
    sd.card()->syncDevice();

    lastUsbMs = millis();
    usbActive = false;
}
#endif

// -------------------- ANIMATION --------------------
static const uint16_t IDLE_FPS  = 1;
static const uint16_t DRINK_FPS = 1;
static const uint16_t POOP_FPS  = 1;

static const uint32_t IDLE_DT  = 1000 / IDLE_FPS;
static const uint32_t DRINK_DT = 1000 / DRINK_FPS;
static const uint32_t POOP_DT  = 1000 / POOP_FPS;

static const float T0 = 25.0f;
static const float T1 = 75.0f;
static const float DELTA_TRIGGER = 1.0f;

enum Mode { MODE_IDLE, MODE_DRINK, MODE_POOP };
Mode mode = MODE_IDLE;

float lastUsedPct = -1.0f;
uint32_t lastFrameMs = 0;
uint16_t frameIndex = 0;

static const uint8_t NUM_IDLE_NORMAL  = 4;
static const uint8_t NUM_IDLE_HAPPY   = 4;
static const uint8_t NUM_IDLE_ANNOYED = 4;
static const uint8_t NUM_DRINK        = 4;
static const uint8_t NUM_POOP         = 6;

uint16_t* frames_idle_normal[NUM_IDLE_NORMAL];
uint16_t* frames_idle_happy[NUM_IDLE_HAPPY];
uint16_t* frames_idle_annoyed[NUM_IDLE_ANNOYED];
uint16_t* frames_drink[NUM_DRINK];
uint16_t* frames_poop[NUM_POOP];

// -------------------- BUS DISCIPLINE --------------------
static inline void deselectAll() {
    digitalWrite(TFT_CS_PIN, HIGH);
    digitalWrite(SD_CS_PIN, HIGH);
}

static inline void selectSD() {
    digitalWrite(TFT_CS_PIN, HIGH);
    digitalWrite(SD_CS_PIN, LOW);
}

static inline void deselectSD() {
    digitalWrite(SD_CS_PIN, HIGH);
}

static inline void selectTFT() {
    digitalWrite(SD_CS_PIN, HIGH);
    digitalWrite(TFT_CS_PIN, LOW);
}

static inline void deselectTFT() {
    digitalWrite(TFT_CS_PIN, HIGH);
}

// -------------------- SD INITIALIZATION --------------------
bool sdInitWithSpeed() {
    const uint8_t speeds[] = { 12, 8, 4 };
    const size_t N = sizeof(speeds) / sizeof(speeds[0]);

    for (size_t i = 0; i < N; i++) {
        deselectAll();
        selectSD();
        bool ok = sd.begin(SdSpiConfig(SD_CS_PIN, SHARED_SPI, SD_SCK_MHZ(speeds[i])));
        deselectSD();

        Serial.print("SD init ");
        Serial.print(ok ? "OK" : "FAIL");
        Serial.print(" at ");
        Serial.print(speeds[i]);
        Serial.println(" MHz");

        if (ok) {
            Serial.println("==> SD card ready!");
            return true;
        }
    delay(60);
    }
    return false;
}

// -------------------- FRAME LOADING --------------------
bool loadFrameToRAM(const char* path, uint16_t** frameBuffer) {
    deselectAll();
    selectSD();

    if (!file.open(path, O_READ)) {
        deselectSD();
        return false;
    }

    uint32_t fileSize = file.size();
    uint32_t expectedSize = FRAME_W * FRAME_H * 2;

    if (fileSize != expectedSize) {
        file.close();
        deselectSD();
        return false;
    }

  *frameBuffer = (uint16_t*)malloc(expectedSize);
    if (*frameBuffer == NULL) {
        file.close();
        deselectSD();
        return false;
    }

    int bytesRead = file.read((uint8_t*)*frameBuffer, expectedSize);
    file.close();
    deselectSD();

    if (bytesRead != (int)expectedSize) {
        free(*frameBuffer);
        *frameBuffer = NULL;
        return false;
    }

    return true;
}

bool loadAllFrames() {
    Serial.println("\nLoading frames into RAM...");

    char path[64];
    bool success = true;

    for (uint8_t i = 0; i < NUM_IDLE_NORMAL; i++) {
        snprintf(path, sizeof(path), "/cat_idle/n%02u.rgb", i);
        if (!loadFrameToRAM(path, &frames_idle_normal[i])) success = false;
    }

    for (uint8_t i = 0; i < NUM_IDLE_HAPPY; i++) {
        snprintf(path, sizeof(path), "/cat_idle/h%02u.rgb", i);
        if (!loadFrameToRAM(path, &frames_idle_happy[i])) success = false;
    }

    for (uint8_t i = 0; i < NUM_IDLE_ANNOYED; i++) {
        snprintf(path, sizeof(path), "/cat_idle/a%02u.rgb", i);
        if (!loadFrameToRAM(path, &frames_idle_annoyed[i])) success = false;
    }

    for (uint8_t i = 0; i < NUM_DRINK; i++) {
        snprintf(path, sizeof(path), "/cat_drink/d%02u.rgb", i);
        if (!loadFrameToRAM(path, &frames_drink[i])) success = false;
    }

    for (uint8_t i = 0; i < NUM_POOP; i++) {
        snprintf(path, sizeof(path), "/cat_poop/p%02u.rgb", i);
        if (!loadFrameToRAM(path, &frames_poop[i])) success = false;
    }

    if (success) Serial.println("[OK] ALL FRAMES LOADED");
    else         Serial.println("[WARN] SOME FRAMES FAILED TO LOAD");

    return success;
}

// -------------------- FRAME RENDERING --------------------
void drawFrameToSprite(uint16_t* frameBuffer) {
    if (frameBuffer == NULL) return;

#if USB_AVAILABLE
  // NEW: if Windows is actively poking the drive, skip draw briefly
    if (mscStarted && (millis() - lastUsbMs) < 250) return;

  // existing guard
    if (usbActive) return;
#endif

    spr.fillSprite(BG_COLOR);

    for (int16_t dy = 0; dy < SPRITE_H; dy++) {
        for (int16_t dx = 0; dx < SPRITE_W; dx++) {
        int16_t sx = (int16_t)(dx / SCALE);
        int16_t sy = (int16_t)(dy / SCALE);

        if (sx >= FRAME_W) sx = FRAME_W - 1;
        if (sy >= FRAME_H) sy = FRAME_H - 1;

        uint16_t pixel = frameBuffer[sy * FRAME_W + sx];
        if (pixel == TRANSPARENT_COLOR) continue;

        spr.drawPixel(dx, dy, pixel);
        }
    }

    selectTFT();
    spr.pushSprite(spriteX, spriteY);
    deselectTFT();
}

// -------------------- STORAGE MONITORING --------------------
float getUsedPercent() {
    selectSD();
    uint32_t totalClusters = sd.vol()->clusterCount();
    if (totalClusters == 0) {
        deselectSD();
        return 0.0f;
    }
    uint32_t freeClusters = sd.vol()->freeClusterCount();
    deselectSD();

    float used = 100.0f * (1.0f - (float)freeClusters / (float)totalClusters);
    if (used < 0) used = 0;
    if (used > 100) used = 100;
    return used;
}

// -------------------- MODE CONTROL --------------------
void startDrink() { if (mode != MODE_IDLE) return; mode = MODE_DRINK; frameIndex = 0; lastFrameMs = 0; }
void startPoop()  { if (mode != MODE_IDLE) return; mode = MODE_POOP;  frameIndex = 0; lastFrameMs = 0; }
void startIdle()  { mode = MODE_IDLE;  frameIndex = 0; lastFrameMs = 0; }

// -------------------- SETUP --------------------
void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n\n====================================");
    Serial.println("Cat Pet - USB DIAGNOSTIC (2.4 + FIX)");
    Serial.println("====================================\n");

#if USB_AVAILABLE
    Serial.println("[OK] TinyUSB library detected!");
#else
    Serial.println("[ERROR] TinyUSB NOT DETECTED!");
    Serial.println("FIX: Tools -> USB Stack -> Adafruit TinyUSB");
#endif

    SPI.setSCK(SPI_SCK_PIN);
    SPI.setTX(SPI_MOSI_PIN);
    SPI.setRX(SPI_MISO_PIN);
    SPI.begin();

    pinMode(TFT_CS_PIN, OUTPUT);
    pinMode(SD_CS_PIN, OUTPUT);
    deselectAll();
    delay(10);

  // TFT
    Serial.println("Initializing TFT...");
    selectTFT();
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(BG_COLOR);
    deselectTFT();
    Serial.println("[OK] TFT initialized\n");

  // Sprite
    spr.setColorDepth(16);
    spr.createSprite(SPRITE_W, SPRITE_H);
    spr.fillSprite(BG_COLOR);
    spriteX = (tft.width() - SPRITE_W) / 2;
    spriteY = (tft.height() - SPRITE_H) / 2;

  // SD
    Serial.println("Initializing SD card...");
    if (!sdInitWithSpeed()) {
        Serial.println("[ERROR] SD card failed!");
        selectTFT();
        tft.fillScreen(BG_COLOR);
        tft.setTextColor(TFT_RED, BG_COLOR);
        tft.drawString("SD FAILED!", 10, 10, 2);
        deselectTFT();
        while (1) {
    #if USB_AVAILABLE
        TinyUSBDevice.task();
    #endif
        delay(10);
        }
    }
    Serial.println("[OK] SD card ready\n");

#if !USB_MSC_ONLY
    if (!loadAllFrames()) {
        Serial.println("[ERROR] Frame loading failed!");
        while (1) {
#if USB_AVAILABLE
    TinyUSBDevice.task();
#endif
    delay(10);
    }
}

    lastUsedPct = getUsedPercent();
    Serial.print("Initial storage used: ");
    Serial.print(lastUsedPct, 1);
    Serial.println("%\n");

    selectTFT();
    tft.fillScreen(BG_COLOR);
    deselectTFT();
    drawFrameToSprite(frames_idle_normal[0]);

    Serial.println("====================================");
    Serial.println("CAT PET IS ALIVE!");
    Serial.println("====================================\n");
    #else
    Serial.println("====================================");
    Serial.println("USB MSC-ONLY MODE");
    Serial.println("====================================\n");
    #endif

#if USB_AVAILABLE
  // USB MSC init
    Serial.println("Initializing USB Mass Storage...");

    usb_msc.setID("Adafruit", "CatPet SD", "1.0");
    usb_msc.setReadWriteCallback(msc_read_callback, msc_write_callback, msc_flush_callback);

    usb_msc.setUnitReady(false);
    bool usbOk = usb_msc.begin();

    if (TinyUSBDevice.mounted()) {
        TinyUSBDevice.detach();
        delay(10);
        TinyUSBDevice.attach();
    }

    if (!usbOk) {
        Serial.println("[ERROR] usb_msc.begin() failed");
    } else {
        uint32_t sectorCount = sd.card()->sectorCount();
        usb_msc.setCapacity(sectorCount, 512);
        usb_msc.setUnitReady(true);
        mscStarted = true;

        Serial.println("[OK] USB MSC started successfully!");
        Serial.println("====================================");
        Serial.println("UNPLUG + REPLUG USB NOW");
        Serial.println("Wait ~10 seconds after replug");
        Serial.println("====================================\n");
    }
#else
    Serial.println("[SKIP] USB not available (TinyUSB not detected)");
#endif
}

// -------------------- MAIN LOOP --------------------
#if USB_MSC_ONLY
void loop() {
#if USB_AVAILABLE
    TinyUSBDevice.task();
#endif
    delay(1);
}
#else
void loop() {
#if USB_AVAILABLE
    TinyUSBDevice.task();
#endif

    uint32_t now = millis();

#if USB_AVAILABLE
    if (mscStarted &&
        sessWriteCalls > 0 &&
        (now - lastWriteSeenMs) > WRITE_IDLE_END_MS &&
        (now - lastSessionHandledMs) > WRITE_IDLE_END_MS) {

    uint32_t duration = now - sessStartMs;

        // 1) ignore micro-actions (create empty file, rename, tiny metadata noise)
        if (sessWriteCalls <= IGNORE_CALLS_MAX) {
        lastSessionHandledMs = now;
        sessMinLBA = 0xFFFFFFFF;
        sessMaxLBA = 0;
        sessWriteCalls = 0;
        sessStartMs = 0;
        return;
        }


        // 2) DRINK = many writes AND it lasted long enough (real copy/save)
        if (sessWriteCalls >= DRINK_CALLS_THRESHOLD && duration >= DRINK_MIN_DURATION_MS) {
        startDrink();
        } else {
        startPoop();
        }

        // reset session
        lastSessionHandledMs = now;
        sessMinLBA = 0xFFFFFFFF;
        sessMaxLBA = 0;
        sessWriteCalls = 0;
        sessStartMs = 0;
    }
#endif




// Poll storage used% ONLY BEFORE MSC starts.
  // Once MSC is started, do NOT touch FAT from MCU (stability).
    static uint32_t lastPollMs = 0;
    if (now - lastPollMs > 2000) {
        lastPollMs = now;

#if USB_AVAILABLE
    if (!mscStarted && !usbActive) {
#endif
    float usedPct = getUsedPercent();
    float delta = usedPct - lastUsedPct;

    if (mode == MODE_IDLE) {
        if (delta >= DELTA_TRIGGER) startDrink();
        else if (delta <= -DELTA_TRIGGER) startPoop();
    }

    lastUsedPct = usedPct;
#if USB_AVAILABLE
    }
#endif
    }

    uint32_t dt = (mode == MODE_POOP) ? POOP_DT : (mode == MODE_DRINK ? DRINK_DT : IDLE_DT);
    if (now - lastFrameMs < dt) return;
    lastFrameMs = now;

    if (mode == MODE_DRINK) {
        if (frameIndex < NUM_DRINK) drawFrameToSprite(frames_drink[frameIndex]);
        frameIndex++;
        if (frameIndex >= NUM_DRINK) startIdle();
        return;
    }

    if (mode == MODE_POOP) {
        if (frameIndex < NUM_POOP) drawFrameToSprite(frames_poop[frameIndex]);
        frameIndex++;
        if (frameIndex >= NUM_POOP) startIdle();
        return;
    }

    uint16_t** currentSet;
    uint8_t setSize;

    if (lastUsedPct <= T0) {
        currentSet = frames_idle_normal;
        setSize = NUM_IDLE_NORMAL;
    } else if (lastUsedPct <= T1) {
        currentSet = frames_idle_happy;
        setSize = NUM_IDLE_HAPPY;
    } else {
        currentSet = frames_idle_annoyed;
        setSize = NUM_IDLE_ANNOYED;
    }

    uint16_t localFrame = frameIndex % setSize;
    drawFrameToSprite(currentSet[localFrame]);

    frameIndex++;
    if (frameIndex > 10000) frameIndex = 0;
}
#endif
