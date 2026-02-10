/*
 * Cat Pet - Fast RAM-Based Animation (2x Scaling Version)
 * THIS IS THE BACKUP VERSION - 2x SCALING, NO CROPPING
 * 
 * If you don't like the 2.125x version, upload this one instead!
 * 
 * - 128×128 pixels (2x from 64×64)
 * - Perfectly fits screen height
 * - No cropping
 * - Small black borders on sides
 * 
 * Hardware: Seeed XIAO RP2040 + Adafruit ST7735 TFT with SD card
 */

#include <SPI.h>
#include <TFT_eSPI.h>
#include <SdFat.h>

// -------------------- PINS (GPIO numbers) --------------------
static const uint8_t SPI_SCK_PIN  = 2;   // GPIO2  = XIAO D8
static const uint8_t SPI_MOSI_PIN = 3;   // GPIO3  = XIAO D10
static const uint8_t SPI_MISO_PIN = 4;   // GPIO4  = XIAO D9
static const uint8_t TFT_CS_PIN   = 27;  // XIAO D1 (TFT CS)
static const uint8_t SD_CS_PIN    = 26;  // XIAO D0 (SD CARD_CS)

// -------------------- DISPLAY + SPRITE --------------------
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);

static const int16_t FRAME_W = 64;    // Original frame size from file
static const int16_t FRAME_H = 64;
static const int16_t SPRITE_W = 128;  // Sprite size (2x scaled) - NO CROPPING
static const int16_t SPRITE_H = 128;

static const uint16_t BG_COLOR = TFT_BLACK;
static const uint16_t TRANSPARENT_COLOR = 0xF81F; // Magenta in RGB565

// Screen position for centered sprite
int16_t spriteX = 0;
int16_t spriteY = 0;

// -------------------- SD --------------------
SdFat sd;
File32 file;

// -------------------- ANIMATION TIMING --------------------
static const uint16_t IDLE_FPS  = 2;  // 2 FPS matches Piskel preview
static const uint16_t DRINK_FPS = 2;
static const uint16_t POOP_FPS  = 2;

static const uint32_t IDLE_DT  = 1000 / IDLE_FPS;   // 500ms
static const uint32_t DRINK_DT = 1000 / DRINK_FPS;  // 500ms
static const uint32_t POOP_DT  = 1000 / POOP_FPS;   // 500ms

// Storage thresholds
static const float T0 = 25.0f;  // Normal -> Happy threshold
static const float T1 = 75.0f;  // Happy -> Annoyed threshold
static const float DELTA_TRIGGER = 1.0f; // % change to trigger animation

// Animation states
enum Mode { MODE_IDLE, MODE_DRINK, MODE_POOP };
Mode mode = MODE_IDLE;

float lastUsedPct = -1.0f;
uint32_t lastFrameMs = 0;
uint16_t frameIndex = 0;

// -------------------- FRAME STORAGE (RAM) --------------------
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
  const uint8_t speeds[] = { 36, 24, 18, 12, 8, 4 };
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
    Serial.print("ERROR: Cannot open ");
    Serial.println(path);
    deselectSD();
    return false;
  }

  uint32_t fileSize = file.size();
  uint32_t expectedSize = FRAME_W * FRAME_H * 2;
  
  if (fileSize != expectedSize) {
    Serial.print("ERROR: Wrong file size for ");
    Serial.print(path);
    Serial.print(" (expected ");
    Serial.print(expectedSize);
    Serial.print(", got ");
    Serial.print(fileSize);
    Serial.println(")");
    file.close();
    deselectSD();
    return false;
  }

  *frameBuffer = (uint16_t*)malloc(expectedSize);
  if (*frameBuffer == NULL) {
    Serial.print("ERROR: Out of memory for ");
    Serial.println(path);
    file.close();
    deselectSD();
    return false;
  }

  int bytesRead = file.read((uint8_t*)*frameBuffer, expectedSize);
  file.close();
  deselectSD();

  if (bytesRead != expectedSize) {
    Serial.print("ERROR: Read failed for ");
    Serial.print(path);
    Serial.print(" (expected ");
    Serial.print(expectedSize);
    Serial.print(" bytes, got ");
    Serial.print(bytesRead);
    Serial.println(")");
    free(*frameBuffer);
    *frameBuffer = NULL;
    return false;
  }

  Serial.print("Loaded: ");
  Serial.println(path);
  return true;
}

bool loadAllFrames() {
  Serial.println("\n=================================");
  Serial.println("Loading frames into RAM...");
  Serial.println("=================================");
  
  char path[64];
  bool success = true;

  Serial.println("\n[1/5] Loading idle_normal frames...");
  for (uint8_t i = 0; i < NUM_IDLE_NORMAL; i++) {
    snprintf(path, sizeof(path), "/cat_idle/n%02u.rgb", i);
    if (!loadFrameToRAM(path, &frames_idle_normal[i])) success = false;
  }

  Serial.println("\n[2/5] Loading idle_happy frames...");
  for (uint8_t i = 0; i < NUM_IDLE_HAPPY; i++) {
    snprintf(path, sizeof(path), "/cat_idle/h%02u.rgb", i);
    if (!loadFrameToRAM(path, &frames_idle_happy[i])) success = false;
  }

  Serial.println("\n[3/5] Loading idle_annoyed frames...");
  for (uint8_t i = 0; i < NUM_IDLE_ANNOYED; i++) {
    snprintf(path, sizeof(path), "/cat_idle/a%02u.rgb", i);
    if (!loadFrameToRAM(path, &frames_idle_annoyed[i])) success = false;
  }

  Serial.println("\n[4/5] Loading drink frames...");
  for (uint8_t i = 0; i < NUM_DRINK; i++) {
    snprintf(path, sizeof(path), "/cat_drink/d%02u.rgb", i);
    if (!loadFrameToRAM(path, &frames_drink[i])) success = false;
  }

  Serial.println("\n[5/5] Loading poop frames...");
  for (uint8_t i = 0; i < NUM_POOP; i++) {
    snprintf(path, sizeof(path), "/cat_poop/p%02u.rgb", i);
    if (!loadFrameToRAM(path, &frames_poop[i])) success = false;
  }

  Serial.println("\n=================================");
  if (success) {
    Serial.println("ALL FRAMES LOADED SUCCESSFULLY!");
  } else {
    Serial.println("SOME FRAMES FAILED TO LOAD!");
  }
  Serial.println("=================================\n");

  return success;
}

// -------------------- FRAME RENDERING (2X SCALING) --------------------
void drawFrameToSprite(uint16_t* frameBuffer) {
  /*
   * Draw a 64x64 frame from RAM to a 128x128 sprite with 2x scaling
   * Each pixel is drawn as a 2x2 block for crisp pixel art scaling
   * Magenta pixels (0xF81F) are skipped for transparency
   */
  
  if (frameBuffer == NULL) return;

  spr.fillSprite(BG_COLOR);

  // Draw each pixel from 64x64 frame as a 2x2 block in 128x128 sprite
  for (int16_t y = 0; y < FRAME_H; y++) {
    for (int16_t x = 0; x < FRAME_W; x++) {
      uint16_t pixel = frameBuffer[y * FRAME_W + x];
      
      if (pixel == TRANSPARENT_COLOR) continue;
      
      int16_t sx = x * 2;
      int16_t sy = y * 2;
      
      spr.drawPixel(sx,     sy,     pixel);
      spr.drawPixel(sx + 1, sy,     pixel);
      spr.drawPixel(sx,     sy + 1, pixel);
      spr.drawPixel(sx + 1, sy + 1, pixel);
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
void startDrink() {
  mode = MODE_DRINK;
  frameIndex = 0;
  lastFrameMs = 0;
  Serial.println("MODE: Drink animation");
}

void startPoop() {
  mode = MODE_POOP;
  frameIndex = 0;
  lastFrameMs = 0;
  Serial.println("MODE: Poop animation");
}

void startIdle() {
  mode = MODE_IDLE;
  frameIndex = 0;
  lastFrameMs = 0;
  Serial.println("MODE: Idle");
}

// -------------------- SETUP --------------------
void setup() {
  Serial.begin(115200);
  delay(500);
  
  Serial.println("\n\n====================================");
  Serial.println("Cat Pet - 2x Scaling (NO CROP)");
  Serial.println("====================================\n");

  SPI.setSCK(SPI_SCK_PIN);
  SPI.setTX(SPI_MOSI_PIN);
  SPI.setRX(SPI_MISO_PIN);
  SPI.begin();

  pinMode(TFT_CS_PIN, OUTPUT);
  pinMode(SD_CS_PIN, OUTPUT);
  deselectAll();
  delay(10);

  Serial.println("Initializing TFT display...");
  selectTFT();
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(BG_COLOR);
  tft.setTextColor(TFT_GREEN, BG_COLOR);
  tft.setTextSize(1);
  tft.drawString("Cat Pet 2x", 10, 10, 2);
  tft.drawString("Initializing...", 10, 30, 2);
  Serial.println("TFT OK");

  spr.setColorDepth(16);
  spr.createSprite(SPRITE_W, SPRITE_H);
  spr.fillSprite(BG_COLOR);

  spriteX = (tft.width() - SPRITE_W) / 2;
  spriteY = (tft.height() - SPRITE_H) / 2;
  deselectTFT();

  Serial.println("\nInitializing SD card...");
  if (!sdInitWithSpeed()) {
    selectTFT();
    tft.fillScreen(BG_COLOR);
    tft.setTextColor(TFT_RED, BG_COLOR);
    tft.drawString("SD CARD FAILED!", 10, 10, 2);
    deselectTFT();
    Serial.println("FATAL: SD card init failed!");
    while (1) delay(1000);
  }

  selectTFT();
  tft.drawString("Loading frames...", 10, 50, 2);
  deselectTFT();
  
  if (!loadAllFrames()) {
    selectTFT();
    tft.fillScreen(BG_COLOR);
    tft.setTextColor(TFT_RED, BG_COLOR);
    tft.drawString("FRAME LOAD FAILED!", 10, 10, 2);
    deselectTFT();
    Serial.println("FATAL: Frame loading failed!");
    while (1) delay(1000);
  }

  lastUsedPct = getUsedPercent();
  Serial.print("Initial storage used: ");
  Serial.print(lastUsedPct, 1);
  Serial.println("%");

  selectTFT();
  tft.fillScreen(BG_COLOR);
  deselectTFT();
  
  drawFrameToSprite(frames_idle_normal[0]);

  Serial.println("\n====================================");
  Serial.println("CAT PET IS ALIVE! (2x scaling)");
  Serial.println("====================================\n");
  Serial.println("Animation: RUNNING at 2 FPS\n");
}

// -------------------- MAIN LOOP --------------------
void loop() {
  uint32_t now = millis();

  static uint32_t lastPollMs = 0;
  if (now - lastPollMs > 1000) {
    lastPollMs = now;

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
