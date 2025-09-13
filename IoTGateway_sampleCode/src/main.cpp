#include <Arduino.h>
#include <FastLED.h>
#include <SPI.h>
#include <SD.h>

// pin assign
#define LED       16
#define SW        5
#define SD_DETECT 13
#define MISO      12
#define MOSI      10
#define SCK       11
#define CS        9
#define MODE_1    6
#define MODE_2    7

//parameter
#define NUM_LED 1

// FastLED LED array
CRGB leds[NUM_LED];

// Interrupt flag for SW rising edge
volatile bool swTap = false;

// ISR: set flag on rising edge
/*
 * JP: SW(タクトスイッチ)の立ち上がりエッジ割り込みハンドラ。
 *     ISR内ではフラグを立てるだけにし、実処理はloop()側で行います。
 * EN: Rising-edge ISR for the tap switch (SW).
 *     Keep ISR minimal: set a flag; handle work in loop().
 */
void IRAM_ATTR onSwRise() {
  swTap = true;
}

// --- SPDT (MODE_A/MODE_B) handling ---
/*
 * JP: MODE_1/MODE_2 の組で構成されたSPDTスイッチの位置を判定します。
 *     LOW/HIGHの組合せから「1」「2」または不定("UNKNOWN")を返します。
 * EN: Determine SPDT position using MODE_1/MODE_2 lines.
 *     Returns "1", "2", or "UNKNOWN" based on LOW/HIGH combination.
 */
static const char* readModePos() {
  int a = digitalRead(MODE_1);
  int b = digitalRead(MODE_2);
  if (a == LOW && b == HIGH) return "1";   // MODE_A side selected
  if (b == LOW && a == HIGH) return "2";   // MODE_B side selected
  return "UNKNOWN";                        // both HIGH/LOW or floating
}
static const char* lastModePos = "UNKNOWN";

// --- SD card handling (SPI) ---
static bool sdMounted = false;

/*
 * JP: SD_DETECTピンを読み、SDカードが挿入されているかを返します（アクティブLOW想定）。
 * EN: Read SD_DETECT and return whether the SD card is inserted (active LOW).
 */
static inline bool isCardInserted() {
  // Active LOW detect assumed
  return digitalRead(SD_DETECT) == LOW;
}

/*
 * JP: SPI/SDを初期化してSDをマウントします（既にマウント済みならtrue）。
 * EN: Initialize SPI/SD and mount the card (returns true if already mounted).
 */
static bool mountSD() {
  if (sdMounted) return true;
  // Initialize SPI with custom pins
  SPI.begin(SCK, MISO, MOSI, CS);
  if (!SD.begin(CS, SPI, 25000000)) { // 25 MHz
    return false;
  }
  sdMounted = true;
  return true;
}

/*
 * JP: SDカードをアンマウントします（SD.end()呼び出しと状態更新）。
 * EN: Unmount the SD card (call SD.end() and update state).
 */
static void unmountSD() {
  if (!sdMounted) return;
  SD.end();
  sdMounted = false;
}

static uint32_t helloIndex = 1; // next file index

/*
 * JP: ルートディレクトリを走査し、HELLO_####.TXT の最大番号を見つけて
 *     次に使うインデックス(helloIndex)を設定します。
 * EN: Scan root directory to find the highest HELLO_####.TXT and
 *     set helloIndex to the next value to use.
 */
static void findNextHelloIndex() {
  if (!sdMounted) return;
  File root = SD.open("/");
  if (!root) return;
  uint32_t maxIdx = 0;
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    String name = f.name();
    name.toUpperCase();
    if (name.startsWith("/HELLO_") && name.endsWith(".TXT")) {
      int us = name.indexOf('_');
      int dot = name.lastIndexOf('.');
      if (us >= 0 && dot > us + 1) {
        String num = name.substring(us + 1, dot);
        uint32_t val = num.toInt();
        if (val > maxIdx) maxIdx = val;
      }
    }
    f.close();
  }
  root.close();
  helloIndex = maxIdx + 1;
}

/*
 * JP: "/HELLO_####.TXT" を新規作成し、中身に "Hello" を1行書き込みます。
 *     成功でtrueを返し、helloIndexは使用ごとにインクリメントされます。
 * EN: Create a new "/HELLO_####.TXT" and write one line "Hello".
 *     Returns true on success; helloIndex is incremented after use.
 */
static bool writeHelloFile() {
  if (!sdMounted) return false;
  // Try a few attempts in case of collisions
  for (int tries = 0; tries < 5; ++tries) {
    char path[24];
    snprintf(path, sizeof(path), "/HELLO_%04u.TXT", (unsigned)helloIndex);
    if (SD.exists(path)) {
      helloIndex++;
      continue;
    }
    File f = SD.open(path, FILE_WRITE);
    if (!f) {
      helloIndex++;
      continue;
    }
    f.println("Hello");
    f.close();
    Serial.print("Saved: ");
    Serial.println(path);
    helloIndex++;
    return true;
  }
  return false;
}

/*
 * JP: システム初期化：シリアル、ピン設定、割り込み登録、LED初期化、
 *     SPDT初期状態の表示、SD挿入時のマウントと連番探索を行います。
 * EN: System setup: initialize Serial, configure pins, attach ISR, init LEDs,
 *     print initial SPDT state, and mount SD + find next index if inserted.
 */
void setup(){
  Serial.begin(115200);

  unsigned long t0 = millis();
  while(!Serial && millis() - t0 < 3000) {}  // CDC待ち（最大3秒）
  Serial.println("Boot OK (CDC + OPI PSRAM)");
  Serial.println("=== PSRAM Test ===");
  Serial.printf("Total PSRAM: %d bytes\n", ESP.getPsramSize());
  Serial.printf("Free  PSRAM: %d bytes\n", ESP.getFreePsram());

  // PSRAM動作確認（軽く確保）
  // 1MB 確保してみる
  void* p1 = heap_caps_malloc(1024*1024, MALLOC_CAP_SPIRAM);
  Serial.printf("Alloc 1MB: %s\n", p1 ? "OK" : "FAIL");

  // 4MB 確保してみる
  void* p2 = heap_caps_malloc(4*1024*1024, MALLOC_CAP_SPIRAM);
  Serial.printf("Alloc 4MB: %s\n", p2 ? "OK" : "FAIL");

  //largest PSRAM
  size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  Serial.printf("Largest free block: %d bytes\n", largest);

  // 後片付け
  if (p1) free(p1);
  if (p2) free(p2);

  Serial.println("=== Test Done ===");

  //pinMode
  pinMode(SW, INPUT);
  pinMode(MODE_1, INPUT);
  pinMode(MODE_2, INPUT);
  pinMode(SD_DETECT, INPUT_PULLUP);

  // Attach interrupt on SW (rising edge)
  attachInterrupt(digitalPinToInterrupt(SW), onSwRise, RISING);

  // FastLED init (WS2812B, GRB, 800kHz)
  FastLED.addLeds<WS2812B, LED, GRB>(leds, NUM_LED);
  FastLED.setBrightness(100); // 0-255
  FastLED.clear(true);
  Serial.println("FastLED init done");

  // Report initial SPDT position (MODE_A/MODE_B)
  lastModePos = readModePos();
  Serial.print("MODE: ");
  Serial.println(lastModePos);

  // SD card initial check/mount
  if (isCardInserted()) {
    if (mountSD()) {
      Serial.println("SD mounted");
      findNextHelloIndex();
    } else {
      Serial.println("SD mount failed");
    }
  } else {
    Serial.println("SD not inserted");
  }
}



/*
 * JP: メインループ：LEDアニメ更新、SW押下時の"Hello"ファイル作成、
 *     SPDTの変化検出（デバウンス）、SDカードの挿抜監視を行います。
 * EN: Main loop: update LED animation, create "Hello" file on SW tap,
 *     debounce/report SPDT changes, and monitor SD insert/remove.
 */
void loop(){
  static uint8_t hue = 0;
  static const unsigned long DEBOUNCE_MS = 20;
  static const char* pendingPos = nullptr;
  static unsigned long pendingSince = 0;

  // Simple rainbow animation
  fill_rainbow(leds, NUM_LED, hue, (NUM_LED > 1) ? (uint8_t)(255 / NUM_LED) : 255);
  FastLED.show();
  hue += 2; // speed
  delay(20);

  // Print when SW tapped (rising edge)
  if (swTap) {
    swTap = false;
    Serial.println("Tap Switch");
    if (isCardInserted()) {
      if (!sdMounted) {
        if (mountSD()) {
          Serial.println("SD mounted");
          findNextHelloIndex();
        } else {
          Serial.println("SD mount failed");
        }
      }
      if (sdMounted) {
        if (!writeHelloFile()) {
          Serial.println("Write failed");
        }
      }
    } else {
      if (sdMounted) {
        Serial.println("SD removed");
        unmountSD();
      } else {
        Serial.println("No SD card");
      }
    }
  }

  // Debounced SPDT change detection
  const char* nowPos = readModePos();
  const unsigned long now = millis();
  if (pendingPos != nowPos) {
    pendingPos = nowPos;
    pendingSince = now;
  } else {
    if (pendingPos != lastModePos && (now - pendingSince) >= DEBOUNCE_MS) {
      lastModePos = pendingPos;
      Serial.print("MODE: ");
      Serial.println(lastModePos);
    }
  }

  // Handle SD insertion/removal dynamically
  static bool lastInserted = isCardInserted();
  bool inserted = isCardInserted();
  if (inserted != lastInserted) {
    lastInserted = inserted;
    if (inserted) {
      Serial.println("SD inserted");
      if (mountSD()) {
        Serial.println("SD mounted");
        findNextHelloIndex();
      } else {
        Serial.println("SD mount failed");
      }
    } else {
      Serial.println("SD removed");
      unmountSD();
    }
  }
}
