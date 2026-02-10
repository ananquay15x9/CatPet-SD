/*
 * Cat Pet - Fast RAM-Based Animation
 * Loads all frames into RAM at startup for smooth 4 FPS animation
 * 
 * Hardware: Seeed XIAO RP2040 + Adafruit ST7735 TFT with SD card
 * 
 * Version: 2.2 (RAM-optimized) bigger scale but vertically
 */

#include <SPI.h>
#include <TFT_eSPI.h>
#include <SdFat.h>

// -------------------- PINS (GPIO numbers) --------------------
static const uint8_t SPI_SCK_PIN  = 2;   // GPIO2  = XIAO D8
static const uint8_t SPI_MOSI_PIN = 3;   // GPIO3  = XIAO D10
static const uint8_t SPI_MISO_PIN = 4;   // GPIO4  = XIAO D9
static const uint8_t TFT_CS_PIN   = 27;  // XIAO D1 (TFT CS)
static const uint8_t SD_CS_PIN    = 26;  // XIAO D0 (SD CARD_CS) - keep separate from TFT CS

// -------------------- DISPLAY + SPRITE --------------------
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);

static const int16_t FRAME_W = 64;    // Original frame size from file
static const int16_t FRAME_H = 64;
static const float SCALE = 2.5f;      // 160 / 64 = 2.5x (fills height in portrait, crops width)
static const int16_t SPRITE_W = 160;  // Sprite size (scaled)
static const int16_t SPRITE_H = 160;

static const uint16_t BG_COLOR = TFT_BLACK;
static const uint16_t TRANSPARENT_COLOR = 0xF81F; // Magenta in RGB565

// Screen position for centered sprite
int16_t spriteX = 0;
int16_t spriteY = 0;

// -------------------- SD --------------------
SdFat sd;
File32 file;

// -------------------- ANIMATION TIMING --------------------
static const uint16_t IDLE_FPS  = 1;
static const uint16_t DRINK_FPS = 1;
static const uint16_t POOP_FPS  = 2;

static const uint32_t IDLE_DT  = 1000 / IDLE_FPS;   // 333ms
static const uint32_t DRINK_DT = 1000 / DRINK_FPS;  // 333ms
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
// Each frame is 64x64 pixels * 2 bytes = 8,192 bytes
// Total: 22 frames * 8KB = 176KB

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
  const uint8_t speeds[] = { 36, 24, 18, 12, 8, 4 }; // MHz
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
  /*
   * Load a 64x64 RGB565 frame from SD card into RAM
   * File format: pure RGB565 data, 2 bytes per pixel, 8192 bytes total
   */
  
  deselectAll();
  selectSD();

  if (!file.open(path, O_READ)) {
    Serial.print("ERROR: Cannot open ");
    Serial.println(path);
    deselectSD();
    return false;
  }

  // Check file size
  uint32_t fileSize = file.size();
  uint32_t expectedSize = FRAME_W * FRAME_H * 2; // 8192 bytes
  
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

  // Allocate memory
  *frameBuffer = (uint16_t*)malloc(expectedSize);
  if (*frameBuffer == NULL) {
    Serial.print("ERROR: Out of memory for ");
    Serial.println(path);
    file.close();
    deselectSD();
    return false;
  }

  // Read entire frame into RAM
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
  /*
   * Load all 22 frames from SD card into RAM
   * This happens once at startup
   */
  
  Serial.println("\n=================================");
  Serial.println("Loading frames into RAM...");
  Serial.println("=================================");
  
  char path[64];
  bool success = true;

  // Load idle normal frames (n00-n03)
  Serial.println("\n[1/5] Loading idle_normal frames...");
  for (uint8_t i = 0; i < NUM_IDLE_NORMAL; i++) {
    snprintf(path, sizeof(path), "/cat_idle/n%02u.rgb", i);
    if (!loadFrameToRAM(path, &frames_idle_normal[i])) {
      success = false;
    }
  }

  // Load idle happy frames (h00-h03)
  Serial.println("\n[2/5] Loading idle_happy frames...");
  for (uint8_t i = 0; i < NUM_IDLE_HAPPY; i++) {
    snprintf(path, sizeof(path), "/cat_idle/h%02u.rgb", i);
    if (!loadFrameToRAM(path, &frames_idle_happy[i])) {
      success = false;
    }
  }

  // Load idle annoyed frames (a00-a03)
  Serial.println("\n[3/5] Loading idle_annoyed frames...");
  for (uint8_t i = 0; i < NUM_IDLE_ANNOYED; i++) {
    snprintf(path, sizeof(path), "/cat_idle/a%02u.rgb", i);
    if (!loadFrameToRAM(path, &frames_idle_annoyed[i])) {
      success = false;
    }
  }

  // Load drink frames (d00-d03)
  Serial.println("\n[4/5] Loading drink frames...");
  for (uint8_t i = 0; i < NUM_DRINK; i++) {
    snprintf(path, sizeof(path), "/cat_drink/d%02u.rgb", i);
    if (!loadFrameToRAM(path, &frames_drink[i])) {
      success = false;
    }
  }

  // Load poop frames (p00-p05)
  Serial.println("\n[5/5] Loading poop frames...");
  for (uint8_t i = 0; i < NUM_POOP; i++) {
    snprintf(path, sizeof(path), "/cat_poop/p%02u.rgb", i);
    if (!loadFrameToRAM(path, &frames_poop[i])) {
      success = false;
    }
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

// -------------------- FRAME RENDERING --------------------
void drawFrameToSprite(uint16_t* frameBuffer) {
  /*
   * Draw a 64x64 frame from RAM to a 136x136 sprite with 2.125x scaling
   * Uses nearest-neighbor scaling for crisp pixel art
   * Magenta pixels (0xF81F) are skipped for transparency
   */
  
  if (frameBuffer == NULL) return;

  // Clear sprite to background color first
  spr.fillSprite(BG_COLOR);

  // Scale factor: 160 / 64 = 2.5
  // For each pixel in the 160x160 sprite, find corresponding source pixel
  
  for (int16_t dy = 0; dy < SPRITE_H; dy++) {
    for (int16_t dx = 0; dx < SPRITE_W; dx++) {
      // Calculate source pixel position (nearest-neighbor)
      int16_t sx = (int16_t)(dx / SCALE);
      int16_t sy = (int16_t)(dy / SCALE);
      
      // Bounds check (should always be within 0-63)
      if (sx >= FRAME_W) sx = FRAME_W - 1;
      if (sy >= FRAME_H) sy = FRAME_H - 1;
      
      uint16_t pixel = frameBuffer[sy * FRAME_W + sx];
      
      // Skip transparent pixels (magenta)
      if (pixel == TRANSPARENT_COLOR) continue;
      
      spr.drawPixel(dx, dy, pixel);
    }
  }

  // Push sprite to screen (will be cropped left/right in portrait mode)
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
  Serial.println("Cat Pet - Fast RAM Animation v2.0");
  Serial.println("====================================\n");

  // Configure SPI pins
  SPI.setSCK(SPI_SCK_PIN);
  SPI.setTX(SPI_MOSI_PIN);
  SPI.setRX(SPI_MISO_PIN);
  SPI.begin();

  pinMode(TFT_CS_PIN, OUTPUT);
  pinMode(SD_CS_PIN, OUTPUT);
  deselectAll();
  delay(10);

  // Initialize TFT
  Serial.println("Initializing TFT display...");
  selectTFT();
  tft.init();
  tft.setRotation(0);  // Portrait mode, CCW from landscape (128x160)
  tft.fillScreen(BG_COLOR);
  tft.setTextColor(TFT_GREEN, BG_COLOR);
  tft.setTextSize(1);
  tft.drawString("Cat Pet v2.0", 10, 10, 2);
  tft.drawString("Initializing...", 10, 30, 2);
  Serial.println("TFT OK");

  // Initialize sprite (160x160 for 2.5x scaled display)
  // Note: Screen is 128px wide in portrait, so left/right will crop 16px each
  spr.setColorDepth(16);
  spr.createSprite(SPRITE_W, SPRITE_H);
  spr.fillSprite(BG_COLOR);

  // Calculate centered position for 160x160 sprite on 128x160 screen
  // X: (128-160)/2 = -16px (crops 16px left, 16px right)
  // Y: (160-160)/2 = 0px from top (fills height)
  spriteX = (tft.width() - SPRITE_W) / 2;
  spriteY = (tft.height() - SPRITE_H) / 2;
  
  Serial.print("Sprite position: X=");
  Serial.print(spriteX);
  Serial.print(", Y=");
  Serial.println(spriteY);
  
  deselectTFT();

  // Initialize SD card
  Serial.println("\nInitializing SD card...");
  if (!sdInitWithSpeed()) {
    selectTFT();
    tft.fillScreen(BG_COLOR);
    tft.setTextColor(TFT_RED, BG_COLOR);
    tft.drawString("SD CARD FAILED!", 10, 10, 2);
    tft.drawString("Check connection", 10, 30, 2);
    deselectTFT();
    Serial.println("FATAL: SD card init failed!");
    while (1) delay(1000);
  }

  // Load all frames into RAM
  selectTFT();
  tft.drawString("Loading frames...", 10, 50, 2);
  deselectTFT();
  
  if (!loadAllFrames()) {
    selectTFT();
    tft.fillScreen(BG_COLOR);
    tft.setTextColor(TFT_RED, BG_COLOR);
    tft.drawString("FRAME LOAD FAILED!", 10, 10, 2);
    tft.drawString("Check SD files", 10, 30, 2);
    deselectTFT();
    Serial.println("FATAL: Frame loading failed!");
    while (1) delay(1000);
  }

  // Get initial storage state
  lastUsedPct = getUsedPercent();
  Serial.print("Initial storage used: ");
  Serial.print(lastUsedPct, 1);
  Serial.println("%");

  // Clear screen and show first frame
  selectTFT();
  tft.fillScreen(BG_COLOR);
  deselectTFT();
  
  drawFrameToSprite(frames_idle_normal[0]);

  Serial.println("\n====================================");
  Serial.println("CAT PET IS ALIVE!");
  Serial.println("====================================\n");
  Serial.println("Animation: RUNNING at 1 FPS\n");
}

// -------------------- MAIN LOOP --------------------
void loop() {
  uint32_t now = millis();

  // Check storage changes every second
  static uint32_t lastPollMs = 0;
  if (now - lastPollMs > 1000) {
    lastPollMs = now;

    float usedPct = getUsedPercent();
    float delta = usedPct - lastUsedPct;

    // Only trigger animations from idle mode
    if (mode == MODE_IDLE) {
      if (delta >= DELTA_TRIGGER) {
        startDrink();
      } else if (delta <= -DELTA_TRIGGER) {
        startPoop();
      }
    }

    lastUsedPct = usedPct;
  }

  // Frame timing
  uint32_t dt = (mode == MODE_POOP) ? POOP_DT : (mode == MODE_DRINK ? DRINK_DT : IDLE_DT);
  if (now - lastFrameMs < dt) return;
  lastFrameMs = now;

  // Render the appropriate frame
  if (mode == MODE_DRINK) {
    // Play drink animation (4 frames)
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
    // Play poop animation (6 frames)
    if (frameIndex < NUM_POOP) {
      drawFrameToSprite(frames_poop[frameIndex]);
    }
    frameIndex++;
    if (frameIndex >= NUM_POOP) {
      startIdle();
    }
    return;
  }

  // Idle mode - cycle through appropriate state
  uint16_t** currentSet;
  uint8_t setSize;
  
  if (lastUsedPct <= T0) {
    // Normal (0-25%)
    currentSet = frames_idle_normal;
    setSize = NUM_IDLE_NORMAL;
  } else if (lastUsedPct <= T1) {
    // Happy (26-75%)
    currentSet = frames_idle_happy;
    setSize = NUM_IDLE_HAPPY;
  } else {
    // Annoyed (76-100%)
    currentSet = frames_idle_annoyed;
    setSize = NUM_IDLE_ANNOYED;
  }

  // Loop through frames
  uint16_t localFrame = frameIndex % setSize;
  drawFrameToSprite(currentSet[localFrame]);
  
  frameIndex++;
  // Keep frameIndex from overflowing
  if (frameIndex > 10000) frameIndex = 0;
}
