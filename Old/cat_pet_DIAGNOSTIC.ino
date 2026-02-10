/*
 * Cat Pet - USB DIAGNOSTIC Version
 * 
 * This version has extra debugging to figure out why USB isn't working
 * 
 * Upload this, then check Serial Monitor at 115200 baud
 */

#include <SPI.h>
#include <TFT_eSPI.h>
#include <SdFat.h>

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
static const float SCALE = 2.125f;
static const int16_t SPRITE_W = 136;
static const int16_t SPRITE_H = 136;

static const uint16_t BG_COLOR = TFT_BLACK;
static const uint16_t TRANSPARENT_COLOR = 0xF81F;

int16_t spriteX = 0;
int16_t spriteY = 0;

// -------------------- SD CARD --------------------
SdFat sd;
File32 file;

#if USB_AVAILABLE
volatile bool usbActive = false;
Adafruit_USBD_MSC usb_msc;

bool msc_read_callback(uint32_t lba, void* buffer, uint32_t bufsize) {
  usbActive = true;
  bool result = sd.card()->readSectors(lba, (uint8_t*)buffer, bufsize / 512);
  usbActive = false;
  return result ? bufsize : -1;
}

int32_t msc_write_callback(uint32_t lba, uint8_t* buffer, uint32_t bufsize) {
  usbActive = true;
  bool result = sd.card()->writeSectors(lba, buffer, bufsize / 512);
  usbActive = false;
  return result ? bufsize : -1;
}

void msc_flush_callback(void) {
  sd.card()->syncDevice();
}
#endif

// -------------------- ANIMATION --------------------
static const uint16_t IDLE_FPS  = 4;
static const uint16_t DRINK_FPS = 4;
static const uint16_t POOP_FPS  = 2;

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
  const uint8_t speeds[] = { 24, 18, 12, 8 };
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

  if (bytesRead != expectedSize) {
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

  if (success) {
    Serial.println("ALL FRAMES LOADED SUCCESSFULLY!");
  } else {
    Serial.println("SOME FRAMES FAILED TO LOAD!");
  }

  return success;
}

// -------------------- FRAME RENDERING --------------------
void drawFrameToSprite(uint16_t* frameBuffer) {
  if (frameBuffer == NULL) return;
  
#if USB_AVAILABLE
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
#if USB_AVAILABLE
  if (usbActive) return lastUsedPct;
#endif
  
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
void startDrink() {
  mode = MODE_DRINK;
  frameIndex = 0;
  lastFrameMs = 0;
}

void startPoop() {
  mode = MODE_POOP;
  frameIndex = 0;
  lastFrameMs = 0;
}

void startIdle() {
  mode = MODE_IDLE;
  frameIndex = 0;
  lastFrameMs = 0;
}

// -------------------- SETUP --------------------
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n====================================");
  Serial.println("Cat Pet - USB DIAGNOSTIC");
  Serial.println("====================================\n");

  // CHECK USB STACK
  Serial.println("DIAGNOSTIC CHECKS:");
  Serial.println("------------------");
  
#if USB_AVAILABLE
  Serial.println("[OK] TinyUSB library detected!");
  Serial.println("[OK] USB Stack is set to TinyUSB");
#else
  Serial.println("[ERROR] TinyUSB NOT DETECTED!");
  Serial.println("[ERROR] USB Stack might not be set correctly");
  Serial.println();
  Serial.println("FIX: Tools -> USB Stack -> Adafruit TinyUSB");
  Serial.println();
#endif

  Serial.print("Board: ");
  Serial.println(ARDUINO_BOARD);
  Serial.println();

  // Configure SPI
  SPI.setSCK(SPI_SCK_PIN);
  SPI.setTX(SPI_MOSI_PIN);
  SPI.setRX(SPI_MISO_PIN);
  SPI.begin();

  pinMode(TFT_CS_PIN, OUTPUT);
  pinMode(SD_CS_PIN, OUTPUT);
  deselectAll();
  delay(10);

  // Initialize TFT
  Serial.println("Initializing TFT...");
  selectTFT();
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(BG_COLOR);
  tft.setTextColor(TFT_GREEN, BG_COLOR);
  tft.setTextSize(1);
  tft.drawString("Diagnostic Mode", 10, 10, 2);
  deselectTFT();
  Serial.println("[OK] TFT initialized\n");

  // Initialize sprite
  spr.setColorDepth(16);
  spr.createSprite(SPRITE_W, SPRITE_H);
  spr.fillSprite(BG_COLOR);
  spriteX = (tft.width() - SPRITE_W) / 2;
  spriteY = (tft.height() - SPRITE_H) / 2;

  // Initialize SD
  Serial.println("Initializing SD card...");
  if (!sdInitWithSpeed()) {
    Serial.println("[ERROR] SD card failed!");
    selectTFT();
    tft.fillScreen(BG_COLOR);
    tft.setTextColor(TFT_RED, BG_COLOR);
    tft.drawString("SD FAILED!", 10, 10, 2);
    deselectTFT();
    while (1) delay(1000);
  }
  Serial.println("[OK] SD card ready\n");

#if USB_AVAILABLE
  // Initialize USB Mass Storage
  Serial.println("Initializing USB Mass Storage...");
  selectTFT();
  tft.drawString("Init USB...", 10, 30, 2);
  deselectTFT();
  
  Serial.println("Step 1: Getting SD card info...");
  uint32_t sectorCount = sd.card()->sectorCount();
  Serial.print("  Sector count: ");
  Serial.println(sectorCount);
  
  Serial.println("Step 2: Setting up USB MSC...");
  usb_msc.setID("Adafruit", "CatPet SD", "1.0");
  Serial.println("  ID set");
  
  usb_msc.setCapacity(sectorCount, 512);
  Serial.println("  Capacity set");
  
  usb_msc.setReadWriteCallback(msc_read_callback, msc_write_callback, msc_flush_callback);
  Serial.println("  Callbacks set");
  
  usb_msc.setUnitReady(true);
  Serial.println("  Unit ready");
  
  Serial.println("Step 3: Starting USB MSC...");
  bool usbOk = usb_msc.begin();
  
  if (usbOk) {
    Serial.println("[OK] USB MSC started successfully!");
    Serial.println();
    Serial.println("====================================");
    Serial.println("UNPLUG AND REPLUG USB NOW!");
    Serial.println("Wait 10 seconds after replugging");
    Serial.println("'CatPet SD' should appear");
    Serial.println("====================================");
    Serial.println();
    
    selectTFT();
    tft.drawString("USB OK!", 10, 50, 2);
    tft.drawString("Unplug/replug", 10, 70, 2);
    deselectTFT();
  } else {
    Serial.println("[ERROR] USB MSC failed to start!");
    Serial.println("This shouldn't happen if TinyUSB is detected");
    
    selectTFT();
    tft.setTextColor(TFT_RED, BG_COLOR);
    tft.drawString("USB FAILED!", 10, 50, 2);
    deselectTFT();
  }
#else
  Serial.println("[SKIP] USB not available (TinyUSB not detected)");
  Serial.println();
  Serial.println("====================================");
  Serial.println("USB WILL NOT WORK!");
  Serial.println("====================================");
  Serial.println("Fix: Tools -> USB Stack -> Adafruit TinyUSB");
  Serial.println("Then re-upload this sketch");
  Serial.println();
  
  selectTFT();
  tft.setTextColor(TFT_YELLOW, BG_COLOR);
  tft.drawString("No USB Stack!", 10, 30, 2);
  deselectTFT();
#endif

  // Load frames
  if (!loadAllFrames()) {
    Serial.println("[ERROR] Frame loading failed!");
    while (1) delay(1000);
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
  Serial.println("====================================");
  Serial.println("Animation: RUNNING at 4 FPS\n");
}

// -------------------- MAIN LOOP --------------------
void loop() {
  uint32_t now = millis();

  static uint32_t lastPollMs = 0;
  if (now - lastPollMs > 2000) {
    lastPollMs = now;

#if USB_AVAILABLE
    if (!usbActive) {
#endif
      float usedPct = getUsedPercent();
      float delta = usedPct - lastUsedPct;

      if (mode == MODE_IDLE) {
        if (delta >= DELTA_TRIGGER) {
          startDrink();
        } else if (delta <= -DELTA_TRIGGER) {
          startPoop();
        }
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
    if (frameIndex < NUM_DRINK) {
      drawFrameToSprite(frames_drink[frameIndex]);
    }
    frameIndex++;
    if (frameIndex >= NUM_DRINK) {
      startIdle();
    }
    return;
  }

  if (mode == MODE_POOP) {
    if (frameIndex < NUM_POOP) {
      drawFrameToSprite(frames_poop[frameIndex]);
    }
    frameIndex++;
    if (frameIndex >= NUM_POOP) {
      startIdle();
    }
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
