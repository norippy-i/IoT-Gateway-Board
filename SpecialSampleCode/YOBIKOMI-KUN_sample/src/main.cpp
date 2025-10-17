#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>        // ★これが必要
#include <esp_wifi_types.h>  // ★WIFI_SECOND_CHAN_* を使うなら
#include <Arduino.h>
#include <FastLED.h>
#include <M5Unified.h>


#define LED_PIN 26
#define LED_NUM 16
#define PLAY 32
#define ATOM_LED_PIN 27

#define PLAY_TIME 30 * 1000

// #define DEBUG
// #define SOUND_ON

CRGB leds[LED_NUM];
CRGB atom_led[1];

// ====== ESPNOW RX CONFIG ======
#ifndef ESPNOW_CHANNEL
#define ESPNOW_CHANNEL 8     // TUF-AX3000の2.4GHz実チャンネルに合わせる
#endif

static void logErr(const char* where, esp_err_t err){
  if(err!=ESP_OK){
    Serial.printf("[%s] err=%d\n", where, err);
  }
}

static void logCurrentChannel(const char* tag){
  uint8_t ch = 0; wifi_second_chan_t sc = WIFI_SECOND_CHAN_NONE;
  esp_err_t e = esp_wifi_get_channel(&ch, &sc);
  if(e==ESP_OK){
    Serial.printf("[%s] current channel=%u\n", tag, ch);
  }else{
    Serial.printf("[%s] current channel=<unknown>\n", tag);
    logErr("get_channel", e);
  }
}

struct StockAvailabilityPayload
{
  bool hasStock;
};

// ESP-NOW RXバッファ・状態
static volatile bool rx_event = false;
static StockAvailabilityPayload rx_payload{};
static volatile int rx_len = 0;
static uint8_t rx_mac[6] = {0};
static uint32_t rx_flash_until = 0; // 内蔵LEDの受信フラッシュ制御

// PLAY制御のための状態管理（非ブロッキング）
static bool play_active = false;
static uint32_t play_until_ms = 21 * 1000;
unsigned long int timecount = 0;
static bool fade_active = false;

void onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len);

void onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len)
{
  Serial.println("receive data");
  if (len < 0)
    return;
  if (len > (int)sizeof(StockAvailabilityPayload))
    len = sizeof(StockAvailabilityPayload);
  memcpy((void *)rx_mac, mac, 6);
  if (len == (int)sizeof(StockAvailabilityPayload))
  {
    memcpy((void *)&rx_payload, incomingData, sizeof(rx_payload));
    rx_len = len;
    rx_event = true; // ループ側で処理＆フラッシュ
  }
}

void setup()
{
  Serial.begin(115200);
  // M5Unified 初期化（ATOM）
  auto cfg = M5.config(); // uartも使えるはず
  M5.begin(cfg);

  // ESP-NOW受信専用初期化
  WiFi.mode(WIFI_AP_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);

    // 省電力OFF（取りこぼし軽減）
  esp_wifi_set_ps(WIFI_PS_NONE);
  // SoftAPを希望チャネルで立ち上げる → ラジオが有効化され get/set_channel が安定
  const char* AP_SSID = "espnow-pin";   // パスワードなしの一時AP
  bool apOk = WiFi.softAP(AP_SSID, nullptr, ESPNOW_CHANNEL, 0 /*hidden*/, 1 /*max conn*/);
  Serial.printf("[AP] start(%s) ch=%d -> %s\n", AP_SSID, ESPNOW_CHANNEL, apOk?"OK":"FAIL");

  // 直後の現在チャネルを確認（以降はこのchでESP-NOWを受信）
  logCurrentChannel("AP-SET");

  if (esp_now_init() != ESP_OK)
  {
    // 初期化失敗時は内蔵LEDを赤表示
    atom_led[0] = CRGB::Red;
    FastLED.show();
  }
  else
  {
    esp_now_register_recv_cb(onDataRecv);
  }

  Serial.println("[ESP-NOW] recv callback registered");

  Serial.print("Device MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.println("Waiting for broadcast packets...\n");

  // LED 初期化（外付け WS2812B ストリップ）
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, LED_NUM);
  FastLED.addLeds<WS2812B, ATOM_LED_PIN, GRB>(atom_led, 1);
  FastLED.setBrightness(100);
  atom_led[0] = CRGB::Blue;
  FastLED.show();

  // PLAY 出力ピン
  pinMode(PLAY, OUTPUT);
  digitalWrite(PLAY, LOW);

  delay(2000);
  atom_led[0] = CRGB::Black;
  FastLED.show();
  Serial.println("setup complete");

#ifdef DEBUG
  timecount = millis();
  play_active = true;
  digitalWrite(PLAY, HIGH);
  timecount = millis();
  delay(500);
  digitalWrite(PLAY, LOW); // 500 msecでボタンを止めるわけよ。
#endif
}

void loop()
{
  M5.update();

  // ESP-NOW受信イベント処理
  if (rx_event)
  {
    rx_event = false;
    // 受信通知として内蔵LEDを点灯（短い青）
    atom_led[0] = CRGB::Blue;
    FastLED.show();
    // rx_flash_until = millis() + 120; // 120ms フラッシュ

    // 必要ならシリアルにダンプ
    // Serial.printf("ESP-NOW RX: %d bytes from %02X:%02X:%02X:%02X:%02X:%02X\n",
    //               rx_len, rx_mac[0], rx_mac[1], rx_mac[2], rx_mac[3], rx_mac[4], rx_mac[5]);

    // 受信データによりPLAYトリガ
    if (rx_len == (int)sizeof(StockAvailabilityPayload))
    {
      bool pressed = rx_payload.hasStock;
      if (pressed)
      { // 今売り出している！！
#ifdef SOUND_ON
        digitalWrite(PLAY, HIGH);
#endif
        timecount = millis();
        play_active = true;
        fade_active = false; // フェード中なら中断
        delay(500);
        digitalWrite(PLAY, LOW);
      }
      else
      { // OFF 連絡
        digitalWrite(PLAY, LOW);
      }
    }
  }

  // PLAY の非ブロッキングタイムアウト
  // ボタンを止める
  if (play_active && (int32_t)(millis() - timecount) >= play_until_ms)
  {
#ifdef SOUND_ON
    digitalWrite(PLAY, HIGH);
#endif
    timecount = millis();
    play_active = false;
    fade_active = true; // フェード開始ï
    delay(500);
    digitalWrite(PLAY, LOW); // 500 msecでボタンを止めるわけよ。
  }

  // --- 🌈 LED 虹色アニメーション or フェードアウト ---
  if (play_active)
  {
    static uint8_t hue = 0;
    fill_rainbow(leds, LED_NUM, hue, 7);
    FastLED.show();
    EVERY_N_MILLISECONDS(20) { hue++; }
  }
  else
  {
    // フェードアウト：徐々に暗くする（既に真っ黒なら何もしない）
    static const uint8_t fadeAmt = 20; // 1回あたりの減衰量（大きいほど速く消灯）
    EVERY_N_MILLISECONDS(20)
    {
      // 何かしら光っている時だけ処理
      bool anyLit = false;
      for (int i = 0; i < LED_NUM; ++i)
      {
        if (leds[i].r || leds[i].g || leds[i].b)
        {
          anyLit = true;
          break;
        }
      }
      if (anyLit || fade_active)
      {
        fadeToBlackBy(leds, LED_NUM, fadeAmt);
        FastLED.show();
      }
      // 全消灯を検出したらフェード完了
      bool allBlack = true;
      for (int i = 0; i < LED_NUM; ++i)
      {
        if (leds[i].r || leds[i].g || leds[i].b)
        {
          allBlack = false;
          break;
        }
      }
      if (allBlack)
      {
        fade_active = false;
      }
    }
  }
}

// フラッシュの消灯タイミング
// if (rx_flash_until && (int32_t)(millis() - rx_flash_until) >= 0)
// {
//   atom_led[0] = CRGB::Black;
//   FastLED.show();
//   rx_flash_until = 0;
// }

// // // --- ボタン押下: PLAY を 500ms だけ HIGH ---
// if (M5.BtnA.wasPressed()) {
//   digitalWrite(PLAY, HIGH);
//   play_active = true;
//   play_until_ms = millis() + 500; // 500ms
// }

// // PLAY の非ブロッキングタイムアウト
// if (play_active && (int32_t)(millis() - play_until_ms) >= 0) {
//   digitalWrite(PLAY, LOW);
//   play_active = false;
// }
