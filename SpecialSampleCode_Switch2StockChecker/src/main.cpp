#include <Arduino.h>
#include <FastLED.h>
#include <SPI.h>
#include <SD.h>
// ESP-NOW
#include <WiFi.h>
#include <esp_now.h>
#include <esp_system.h>
#include <esp_wifi.h>       // ★これが必要
#include <esp_wifi_types.h> // ★WIFI_SECOND_CHAN_* を使うなら
#include <math.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <ctype.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// pin assign
#define LED 16
#define SW 5
#define SD_DETECT 13
#define MISO 12
#define MOSI 10
#define SCK 11
#define CS 9
#define MODE_1 6
#define MODE_2 7

// parameter
#define NUM_LED 1

// FastLED LED array
CRGB leds[NUM_LED];
const int brightness = 100;

// network setup
const char *WIFI_SSID = "SSID";
const char *WIFI_PASS = "Password";
const char *RAKUTEN_APP_ID = "XXXXXXXXXXXXXXX";//楽天APIのアカウントを作るともらえるIDです


//楽天のJANコード。各商品ページに必ずJANコードの記載があります。それを入力します。
//動作はweb上でできるので、一度試してみることをお勧めします。
// const char *TARGET_PRODUCT_JAN_CODE = "2100014542294"; // 楽天ブックスのインストアコード/JANコード これがスイッチ2だ！
const char *TARGET_PRODUCT_JAN_CODE = "4902370552843"; //これはプロコン２
// const char *TARGET_PRODUCT_JAN_CODE = "4902370553574";//テスト用のポケモンLegensds

// discode webhook
const char *DISCORD_WEBHOOK_URL = "https://discord.com/api/webhooks/1419679188058902589/Jzl2qz5r_PIo182FAEaJjXB2Dv1kSoyq11732FOcZ7POtXl5FqIdCgYedzjUaEhIcT4Z";
String RAKUTEN_API_URL = String("https://app.rakuten.co.jp/services/api/BooksGame/Search/20170404") + "?format=json" + "&jan=" + TARGET_PRODUCT_JAN_CODE + "&applicationId=" + RAKUTEN_APP_ID;

// ESP-NOW setup
// const uint8_t ESPNOW_PEER_ADDRESS[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // Broadcast; replace with receiver MAC if needed
const uint8_t ESPNOW_PEER_ADDRESS[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // Broadcast; replace with receiver MAC if needed

// ESP-NOWで送る最小ペイロード：在庫あり=1 / なし=0（1バイト固定）
using StockAvailabilityPayload = uint8_t;

static bool espNowInitialized = false;
static bool pendingStockReset = false;
static unsigned long stockResetDeadlineMs = 0;
static bool lastButtonState = false;

constexpr long GMT_OFFSET_SEC = 9 * 3600; // JST
constexpr int DAYLIGHT_OFFSET_SEC = 0;
const char *NTP_SERVER = "ntp.nict.jp";

volatile bool wifiConnected = false;
volatile bool webhookSent = false;
volatile bool timeInitialized = false;
volatile bool pendingWifiConnectedEvent = false;
volatile bool pendingWifiDisconnectedEvent = false;
time_t baseEpoch = 0;
unsigned long baseMillis = 0;
time_t lastReportedEpoch = 0;
unsigned long lastRakutenFetchMs = 0;
static bool sdMounted = false;
// static bool helloLogged = false;

const unsigned long RAKUTEN_FETCH_INTERVAL_MS = 60UL * 1000UL;

TaskHandle_t timeTaskHandle = nullptr;
void SendMessage2Discode();
void handleWifiConnected();
void handleWifiDisconnected();
void initializeTime();
void updateLocalTime();
void fetchRakutenInventory();
String formatCurrentTimestamp();
String extractJsonValue(const String &json, const char *key);
void WiFiEventHandler(WiFiEvent_t event, WiFiEventInfo_t info);
void TimeTask(void *parameter);
void setStatusLed(const CRGB &color);
static void onEspNowSend(const uint8_t *mac_addr, esp_now_send_status_t status);
static bool initEspNow();
static bool sendEspNowAvailability(bool inStock);
bool sendPushover(const String &title, const String &msg, int priority);
void sendMessageToBocco(String message);
void refreshAccessToken();
void fetchRoomId();

// Pushoverの設定
const char *PUSHOVER_TOKEN = "XXXXXXXXXXXXXXXXXXXXXXXXXXX"; //Pushoverを登録すると入手できるトークンです
const char *PUSHOVER_USER = "XXXXXXXXXXXXXXXXXXXXXXXXXXXx"; //Pushoverを登録すると入手できるUSER IDです
//参考 https://qiita.com/seisei_ai/items/8a789946ec05aaa3e204


// BOCCO API
//  Bocco Emo APIのURLとトークン
// Bocco Emo APIのトークンエンドポイントとリフレッシュトークン
const char *refreshTokenUrl = "https://platform-api.bocco.me/oauth/token/refresh";
const char *refreshToken = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"; // 保存したリフレッシュトークン
String accessToken = "";                                           // 新しいアクセストークンを保存
String roomUuid = "";                                              // 部屋IDを保存する変数
//参考 https://platform-api.bocco.me/api-docs/#overview--%E3%81%94%E5%88%A9%E7%94%A8%E9%96%8B%E5%A7%8B%E3%81%AE%E6%96%B9%E6%B3%95

/*
 * JP: SPI/SDを初期化してSDをマウントします（既にマウント済みならtrue）。
 * EN: Initialize SPI/SD and mount the card (returns true if already mounted).
 */
static bool mountSD()
{
  if (sdMounted)
    return true;
  // Initialize SPI with custom pins
  SPI.begin(SCK, MISO, MOSI, CS);
  if (!SD.begin(CS, SPI, 25000000))
  { // 25 MHz
    return false;
  }
  sdMounted = true;
  return true;
}

/*
 * JP: SDカードをアンマウントします（SD.end()呼び出しと状態更新）。
 * EN: Unmount the SD card (call SD.end() and update state).
 */
static void unmountSD()
{
  if (!sdMounted)
    return;
  SD.end();
  sdMounted = false;
}

/*
 * JP: SD_DETECTピンを読み、SDカードが挿入されているかを返します（アクティブLOW想定）。
 * EN: Read SD_DETECT and return whether the SD card is inserted (active LOW).
 */
static inline bool isCardInserted()
{
  // Active HIGH detect: HIGH means card present
  return digitalRead(SD_DETECT) == LOW;
}

void setup()
{
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.onEvent(WiFiEventHandler);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("Connecting to WiFi SSID: %s\n", WIFI_SSID);
  // ★ ESP-NOWの初期化はWi-Fi接続(GOT_IP)後にWiFiEventHandler内で一度だけ実行します

  FastLED.addLeds<WS2812B, LED, GRB>(leds, NUM_LED);
  FastLED.setBrightness(brightness); // 0-255
  FastLED.clear(true);
  setStatusLed(CRGB::Red);

  // pinMode
  pinMode(SW, INPUT);
  pinMode(MODE_1, INPUT);
  pinMode(MODE_2, INPUT);
  pinMode(SD_DETECT, INPUT_PULLUP);

  lastButtonState = digitalRead(SW) == HIGH;

  BaseType_t taskCreated = xTaskCreate(
      TimeTask,
      "TimeTask",
      8192, // ここバッファとっとくと安心
      nullptr,
      1,
      &timeTaskHandle);

  if (taskCreated != pdPASS)
  {
    Serial.println("Failed to create time maintenance task.");
  }
}

void loop()
{
  // Handle SD insertion/removal dynamically
  static bool lastInserted = false;
  bool inserted = isCardInserted();

  if (inserted && !lastInserted)
  {
    if (!sdMounted && !mountSD())
    {
      Serial.println("SD mount failed");
    }
    else
    {
      sdMounted = true;
      Serial.println("SD mounted");
    }
  }
  else if (!inserted && lastInserted)
  {
    Serial.println("SD removed");
    unmountSD();
    sdMounted = false; // マウント状態だけ戻す
    // helloLogged = false;   // 再挿入時に再び書けるようにする
  }
  lastInserted = inserted;

  unsigned long loopNow = millis();
  bool buttonState = digitalRead(SW) == HIGH;
  if (buttonState && !lastButtonState)
  {
    setStatusLed(CRGB::Blue);
    if (!sendEspNowAvailability(true))
    {
      Serial.println("[ESP-NOW] Failed to send availability notification");
    }
    SendMessage2Discode();
    pendingStockReset = true;
    stockResetDeadlineMs = loopNow + 10000UL;
  }
  lastButtonState = buttonState;

  if (pendingStockReset && static_cast<long>(loopNow - stockResetDeadlineMs) >= 0)
  {
    Serial.println("send Message to stop the sysytem");
    setStatusLed(CRGB::White);
    if (!sendEspNowAvailability(false))
    {
      Serial.println("[ESP-NOW] Failed to send availability notification");
    }
    pendingStockReset = false;
  }

  if (wifiConnected)
  {
    unsigned long now = loopNow;
    if (now - lastRakutenFetchMs >= RAKUTEN_FETCH_INTERVAL_MS)
    {
      fetchRakutenInventory();
      lastRakutenFetchMs = now;
    }
  }

  // vTaskDelay(pdMS_TO_TICKS(200));
}

void handleWifiConnected()
{
  Serial.print("WiFi connected: IP address ");
  Serial.println(WiFi.localIP());

  setStatusLed(CRGB::White);

  lastRakutenFetchMs = millis() - RAKUTEN_FETCH_INTERVAL_MS;
}

void handleWifiDisconnected()
{
  Serial.println("WiFi disconnected. Status LED -> RED");
  setStatusLed(CRGB::Red);
  lastRakutenFetchMs = 0;
}

void WiFiEventHandler(WiFiEvent_t event, WiFiEventInfo_t info)
{
  switch (event)
  {
  case ARDUINO_EVENT_WIFI_STA_GOT_IP:
    wifiConnected = true;
    pendingWifiConnectedEvent = true;
    // ★ チャンネル確定後に一度だけESP-NOW初期化
    if (!espNowInitialized)
    {
      if (!initEspNow())
      {
        Serial.println("[ESP-NOW] Initialization failed after GOT_IP");
      }
    }
    break;
  case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    wifiConnected = false;
    webhookSent = false;
    timeInitialized = false;
    pendingWifiDisconnectedEvent = true;
    baseEpoch = 0;
    baseMillis = 0;
    lastReportedEpoch = 0;
    Serial.printf("WiFi disconnected (reason: %d). Reconnecting...\n", info.wifi_sta_disconnected.reason);
    WiFi.reconnect();
    espNowInitialized = false;
    break;
  default:
    break;
  }
}

void initializeTime()
{
  if (timeInitialized)
  {
    return;
  }

  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  Serial.println("Syncing time with NTP...");

  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 5000))
  {
    baseEpoch = time(nullptr);
    baseMillis = millis();
    timeInitialized = true;
    lastReportedEpoch = 0;
    // Serial.printf("Time synced: %04d-%02d-%02d %02d:%02d:%02d\n",
    //               timeinfo.tm_year + 1900,
    //               timeinfo.tm_mon + 1,
    //               timeinfo.tm_mday,
    //               timeinfo.tm_hour,
    //               timeinfo.tm_min,
    //               timeinfo.tm_sec);
  }
  else
  {
    Serial.println("Failed to obtain time from NTP server.");
  }
}

void updateLocalTime()
{
  if (!timeInitialized)
  {
    return;
  }

  unsigned long elapsedMs = millis() - baseMillis;
  time_t currentEpoch = baseEpoch + (elapsedMs / 1000);

  if (currentEpoch != lastReportedEpoch)
  {
    lastReportedEpoch = currentEpoch;
    struct tm timeinfo;
    if (localtime_r(&currentEpoch, &timeinfo))
    {
      // Serial.printf("Current time: %04d-%02d-%02d %02d:%02d:%02d\n",
      //               timeinfo.tm_year + 1900,
      //               timeinfo.tm_mon + 1,
      //               timeinfo.tm_mday,
      //               timeinfo.tm_hour,
      //               timeinfo.tm_min,
      //               timeinfo.tm_sec);
    }
  }
}

void TimeTask(void *parameter)
{
  (void)parameter;

  for (;;)
  {
    if (wifiConnected)
    {
      if (pendingWifiConnectedEvent)
      {
        pendingWifiConnectedEvent = false;
        handleWifiConnected();
      }
      if (!timeInitialized)
      {
        initializeTime();
      }
      updateLocalTime();
      vTaskDelay(pdMS_TO_TICKS(200));
    }
    else
    {
      if (pendingWifiDisconnectedEvent)
      {
        pendingWifiDisconnectedEvent = false;
        handleWifiDisconnected();
      }
      vTaskDelay(pdMS_TO_TICKS(500));
    }
  }
}

void setStatusLed(const CRGB &color)
{
  leds[0] = color;
  FastLED.show();
}

static void onEspNowSend(const uint8_t *mac_addr, esp_now_send_status_t status)
{
  if (status != ESP_NOW_SEND_SUCCESS)
  {
    Serial.printf("[ESP-NOW] Send status: %d\n", status);
  }

  Serial.printf("[ESP-NOW] send_cb: status=%d, dest=%02X:%02X:%02X:%02X:%02X:%02X, t=%lu\n",
                status,
                mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5],
                millis());
}

static bool initEspNow()
{
  if (espNowInitialized)
  {
    return true;
  }

  // ★ 省電力OFF（取りこぼし軽減）
  esp_wifi_set_ps(WIFI_PS_NONE);

  // ★ 現在チャンネルを表示（AP接続後であることを確認）
  uint8_t primary;
  wifi_second_chan_t second;
  esp_wifi_get_channel(&primary, &second);
  Serial.printf("[ESP-NOW] init on ch=%u\n", primary);

  if (esp_now_init() != ESP_OK)
  {
    Serial.println("[ESP-NOW] esp_now_init failed");
    return false;
  }

  esp_now_register_send_cb(onEspNowSend);

  if (!esp_now_is_peer_exist(ESPNOW_PEER_ADDRESS))
  {
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, ESPNOW_PEER_ADDRESS, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK)
    {
      Serial.println("[ESP-NOW] Failed to add peer");
      return false;
    }
  }

  espNowInitialized = true;
  Serial.println("[ESP-NOW] Initialized");
  return true;
}

static bool sendEspNowAvailability(bool inStock)
{
  if (!espNowInitialized && !initEspNow())
  {
    return false;
  }

  // 1バイトに明示的にエンコード（構造体の詰め物・型差異を回避）
  StockAvailabilityPayload payload = inStock ? 1 : 0;
  const uint8_t *buf = reinterpret_cast<const uint8_t *>(&payload);
  const size_t len = sizeof(payload); // 常に1

  uint8_t dbgCh;
  wifi_second_chan_t dbgSc;
  esp_wifi_get_channel(&dbgCh, &dbgSc);
  Serial.printf("[ESP-NOW] about to send on ch=%u\n", dbgCh);
  esp_err_t result = esp_now_send(ESPNOW_PEER_ADDRESS, buf, len);
  if (result != ESP_OK)
  {
    Serial.printf("[ESP-NOW] Send failed: %d (len=%u, val=%u)\n", result, (unsigned)len, (unsigned)payload);
    return false;
  }
  Serial.printf("[ESP-NOW] Sent (len=%u, val=%u)\n", (unsigned)len, (unsigned)payload);
  return true;
}

String formatCurrentTimestamp()
{
  if (!timeInitialized)
  {
    return String("N/A");
  }

  unsigned long elapsed = millis() - baseMillis;
  time_t currentEpoch = baseEpoch + static_cast<time_t>(elapsed / 1000);
  struct tm timeinfo;
  if (!localtime_r(&currentEpoch, &timeinfo))
  {
    return String("N/A");
  }

  char buffer[21];
  snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
           timeinfo.tm_year + 1900,
           timeinfo.tm_mon + 1,
           timeinfo.tm_mday,
           timeinfo.tm_hour,
           timeinfo.tm_min,
           timeinfo.tm_sec);
  return String(buffer);
}

String extractJsonValue(const String &json, const char *key)
{
  String needle = String("\"") + key + "\"";
  int keyIndex = json.indexOf(needle);
  if (keyIndex < 0)
  {
    return String();
  }

  int colonIndex = json.indexOf(':', keyIndex + needle.length());
  if (colonIndex < 0)
  {
    return String();
  }

  int valueIndex = colonIndex + 1;
  while (valueIndex < json.length() && isspace(static_cast<unsigned char>(json[valueIndex])))
  {
    valueIndex++;
  }
  if (valueIndex >= json.length())
  {
    return String();
  }

  if (json[valueIndex] == '"')
  {
    valueIndex++;
    String value;
    while (valueIndex < json.length())
    {
      char c = json[valueIndex++];
      if (c == '\\' && valueIndex < json.length())
      {
        value += json[valueIndex++];
      }
      else if (c == '\"')
      {
        break;
      }
      else
      {
        value += c;
      }
    }
    return value;
  }

  String value;
  while (valueIndex < json.length())
  {
    char c = json[valueIndex];
    if (c == ',' || c == '}' || c == ']' || c == '\n' || c == '\r')
    {
      break;
    }
    value += c;
    valueIndex++;
  }
  value.trim();
  return value;
}

void fetchRakutenInventory()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    return;
  }

  Serial.println("[Rakuten] Fetching API response...");

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;

  if (https.begin(client, RAKUTEN_API_URL))
  {
    int httpCode = https.GET();
    if (httpCode > 0)
    {
      Serial.printf("[Rakuten] HTTP %d\n", httpCode);
      String payload = https.getString();
      String availability = extractJsonValue(payload, "availability");
      String timestamp = formatCurrentTimestamp();
      long availabilityValue = -1;
      if (availability.length() == 0)
      {
        setStatusLed(CRGB::White);
        Serial.printf("[Rakuten] %s availability: -1 (missing)\n", timestamp.c_str());
        if (!sendEspNowAvailability(false)) // 在庫なかったら・・・
        {
          Serial.println("[ESP-NOW] Failed to send availability notification");
        }
      }
      else
      {
        availabilityValue = availability.toInt();
        Serial.printf("[Rakuten] %s availability: %s\n", timestamp.c_str(), availability.c_str());
      }
      if (availabilityValue > 0)
      {
        Serial.println("[Rakuten] Availability > 0. Sending Discord webhook...");
        if (!sendEspNowAvailability(true))
        {
          Serial.println("[ESP-NOW] Failed to send availability notification");
        }
        SendMessage2Discode();

        // 在庫があるようなら、その結果をログに残す
        if (sdMounted)
        {
          File file = SD.open("/SwitchLog.csv", FILE_APPEND);
          if (!file)
          {
            file = SD.open("/SwitchLog.csv", FILE_WRITE);
          }

          if (file)
          {
            file.print(timestamp.c_str());
            file.print(",");
            file.println(availability.c_str());
            file.close();
            Serial.println("[SD] Appended hello world to /SwitchLog.csv");
          }
          else
          {
            Serial.println("[SD] Failed to open /SwitchLog.csv");
          }
        }
      }
    }
    else
    {
      Serial.printf("[Rakuten] Request failed: %s\n", https.errorToString(httpCode).c_str());
    }
    https.end();
  }
  else
  {
    Serial.println("[Rakuten] Unable to start HTTPS connection");
  }
}

void SendMessage2Discode()
{

  setStatusLed(CRGB::Green);
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;

  String timestamp = formatCurrentTimestamp();
  String payload = "{"
                   "\"content\": \"Nintendo Switch2 本体のゲリラ入荷検知!! \\n情報取得時刻: " +
                   timestamp + "\","
                               "\"embeds\": [{"
                               "\"title\": \"楽天ブックス\","
                               "\"description\": \"急いでアクセスしてSwitch2を買うんだ！！\","
                               "\"url\": \"https://books.rakuten.co.jp/search?sitem=switch2+%E6%9C%AC%E4%BD%93&g=000&l-id=pc-search-box\""
                               "}],"
                               "\"components\": [{"
                               "\"type\": 1,"
                               "\"components\": [{"
                               "\"type\": 2, \"style\": 5,"
                               "\"label\": \"楽天で開く\","
                               "\"url\": \"https://books.rakuten.co.jp/search?sitem=switch2+%E6%9C%AC%E4%BD%93&g=000&l-id=pc-search-box\""
                               "}]"
                               "}]"
                               "}";

  if (https.begin(client, DISCORD_WEBHOOK_URL))
  {
    https.addHeader("Content-Type", "application/json");
    int httpCode = https.POST(payload);
    if (httpCode > 0)
    {
      Serial.printf("Webhook sent. Response: %d\n", httpCode);
    }
    else
    {
      Serial.printf("Failed to send webhook: %s\n", https.errorToString(httpCode).c_str());
    }
    https.end();
  }
  else
  {
    Serial.println("Unable to initialize HTTPS connection to Discord webhook");
  }

  //Pushoverも使おう！

  // 送信テスト(push over も活用)
  if (sendPushover("Switch2ゲリラ入荷!!", "楽天ブックスでSwitch2がゲリラ入荷されています!急いで購入せよ!", 0))
  {
    Serial.println("Sent!");
  }
  else
  {
    Serial.println("Send failed");
  }

  //bocco emoにも送るよ
      // メッセージをBoccoに送信
    refreshAccessToken(); // リフレッシュアクセストークンを使って、現在のアクセストークンを取得
    fetchRoomId();        // アカウントが有する部屋のルームIDを取得

    // 文章を生成して
    String boccoMessage = "ニンテンドースイッチツーがゲリラ入荷したよ！急いで楽天ブックスにアクセスして購入しよう！";
    sendMessageToBocco(boccoMessage); // メッセージを送る

}

bool sendPushover(const String &title, const String &msg, int priority)
{
  WiFiClientSecure client;
  client.setInsecure(); // まずは手早く動かす用。本番はsetCACertでルートCAを設定推奨
  HTTPClient https;

  if (!https.begin(client, "https://api.pushover.net/1/messages.json"))
  {
    Serial.println("begin() failed");
    return false;
  }

  // application/x-www-form-urlencoded でPOST
  String body = "token=" + String(PUSHOVER_TOKEN) +
                "&user=" + String(PUSHOVER_USER) +
                "&title=" + title +
                "&message=" + msg +
                "&priority=" + String(priority); // 0:通常, 1:高, 2:緊急(要retry/expire)

  https.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int code = https.POST(body);
  String resp = https.getString();
  https.end();

  Serial.printf("HTTP %d\n", code);
  Serial.println(resp);
  return (code > 0 && code < 400);
}



/************
  BOCCO emo controls
**************/
// アクセストークンを取得する
void refreshAccessToken()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    HTTPClient http;

    // APIリクエストの設定
    http.begin(refreshTokenUrl);
    http.addHeader("Content-Type", "application/json");

    // リクエストボディにリフレッシュトークンを含める
    String requestBody = "{\"refresh_token\": \"" + String(refreshToken) + "\"}";

    // POSTリクエストを送信
    int httpResponseCode = http.POST(requestBody);

    // レスポンスの確認
    if (httpResponseCode > 0)
    {
      String payload = http.getString();

      // JSONデータのパース
      StaticJsonDocument<1024> doc;
      DeserializationError error = deserializeJson(doc, payload);

      if (error)
      {

        return;
      }

      // 新しいアクセストークンを取得
      accessToken = doc["access_token"].as<String>(); // 新しいアクセストークンを保存
#ifdef DEBUG
      Serial.print("New Access Token: ");
      Serial.println(accessToken);
#endif
    }
    else
    {
#ifdef DEBUG
      Serial.print("Error on HTTP request: ");
      Serial.println(httpResponseCode);
#endif
    }

    // HTTP接続を終了
    http.end();
  }
  else
  {
#ifdef DEBUG
    Serial.println("WiFi Disconnected");
#endif
  }
}

// ルームIDを取得する
void fetchRoomId()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    HTTPClient http;

    const char *boccoApiUrl = "https://platform-api.bocco.me/v1/rooms";

    // APIリクエストの設定
    http.begin(boccoApiUrl);
    http.addHeader("Authorization", String("Bearer ") + accessToken);

    // GETリクエストを送信
    int httpResponseCode = http.GET();

    // レスポンスの確認
    if (httpResponseCode > 0)
    {
      String payload = http.getString();
#ifdef DEBUG
      Serial.println("Response received:");
      Serial.println(payload);
#endif
      // JSONデータのパース
      StaticJsonDocument<1024> doc;
      DeserializationError error = deserializeJson(doc, payload);

      if (error)
      {
#ifdef DEBUG
        Serial.print("Failed to parse JSON: ");
        Serial.println(error.c_str());
#endif
        return;
      }

      // 部屋のUUIDを取得
      roomUuid = doc["rooms"][0]["uuid"].as<String>(); // 部屋IDを取得
#ifdef DEBUG
      Serial.print("Room UUID: ");
      Serial.println(roomUuid);
#endif
    }
    else
    {
#ifdef DEBUG
      Serial.print("Error on HTTP request: ");
      Serial.println(httpResponseCode);
#endif
    }

    // HTTP接続を終了
    http.end();
  }
  else
  {
#ifdef DEBUG
    Serial.println("WiFi Disconnected");
#endif
  }
}

// BOCCOにメッセージを送る
void sendMessageToBocco(String message)
{
  if (WiFi.status() == WL_CONNECTED && roomUuid != "")
  {
    HTTPClient http;

    // APIリクエストの設定
    String apiUrlWithRoom = "https://platform-api.bocco.me/v1/rooms/" + roomUuid + "/messages/text";
    http.begin(apiUrlWithRoom);
    http.addHeader("Authorization", String("Bearer ") + accessToken);
    http.addHeader("Content-Type", "application/json");

    // メッセージのJSONフォーマット
    String requestBody = "{\"text\": \"" + message + "\", \"immediate\": true}";

    // POSTリクエストを送信
    int httpResponseCode = http.POST(requestBody);

    // レスポンスの確認
    if (httpResponseCode > 0)
    {
      String response = http.getString();
#ifdef DEBUG
      Serial.print("Response code: ");
      Serial.println(httpResponseCode);
      Serial.println("Response from server:");
      Serial.println(response);
#endif
    }
    else
    {
#ifdef DEBUG
      Serial.print("Error on sending POST: ");
      Serial.println(httpResponseCode);
#endif
    }

    // HTTP接続を終了
    http.end();
  }
  else
  {
#ifdef DEBUG
    Serial.println("WiFi Disconnected or Room UUID not available");
#endif
  }
}