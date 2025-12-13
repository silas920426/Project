#include <Arduino.h>
#include <RadioLib.h>
#include <U8g2lib.h>
#include <TinyGPS++.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiMulti.h>

//=================================================================
//   ★ 切換模式：Sensor Node / Gateway Node
//=================================================================
#define INITIATING_NODE   // Sensor Node
// #define INITIATING_NODE // Gateway Node（註解上面這行即可）

//=================================================================
//   ★ LoRa 腳位 / 物件
//=================================================================
#define LORA_SCK    5
#define LORA_MISO   35
#define LORA_MOSI   27
#define LORA_CS     18
#define LORA_DIO2   36
#define LORA_BUSY   34
#define LORA_RESET   0

SPIClass SPI_LORA(VSPI);
LLCC68 radio = new Module(LORA_CS, LORA_DIO2, LORA_RESET, LORA_BUSY, SPI_LORA);

//=================================================================
//   ★ OLED
//=================================================================
#define I2C_SDA     4
#define I2C_SCL     15
U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(
  U8G2_R0, U8X8_PIN_NONE, I2C_SCL, I2C_SDA
);

//=================================================================
//   ★ AM2120（Sensor 專用）
//=================================================================
#define AM2120_PIN  32
float temperature = 0.0f;
float humidity    = 0.0f;

bool readAM2120(float &h, float &t) {
  uint8_t data[5] = {0, 0, 0, 0, 0};
  pinMode(AM2120_PIN, OUTPUT);
  digitalWrite(AM2120_PIN, LOW);
  delayMicroseconds(1800);
  digitalWrite(AM2120_PIN, HIGH);
  delayMicroseconds(30);
  pinMode(AM2120_PIN, INPUT);

  unsigned long tStart = micros();
  while (digitalRead(AM2120_PIN) == HIGH) if (micros() - tStart > 100) return false;
  while (digitalRead(AM2120_PIN) == LOW)  if (micros() - tStart > 200) return false;
  while (digitalRead(AM2120_PIN) == HIGH) if (micros() - tStart > 200) return false;

  for (int i = 0; i < 40; i++) {
    while (digitalRead(AM2120_PIN) == LOW);
    unsigned long tHigh = micros();
    while (digitalRead(AM2120_PIN) == HIGH);
    if ((micros() - tHigh) > 40)
      data[i/8] |= (1 << (7 - (i%8)));
  }

  if ((uint8_t)(data[0]+data[1]+data[2]+data[3]) != data[4]) return false;

  h = ((data[0]<<8) + data[1]) * 0.1f;
  t = (((data[2]&0x7F)<<8) + data[3]) * 0.1f;
  if (data[2] & 0x80) t = -t;
  return true;
}

//=================================================================
//   ★ GPS（Sensor 專用）
//=================================================================
#define GPS_RX 23
#define GPS_TX 12

TinyGPSPlus gps;
HardwareSerial GPS_Serial(1);

double gpsLat = 0;
double gpsLng = 0;
int gpsSat   = 0;
bool gpsFix  = false;

void updateGPS() {
  while (GPS_Serial.available())
    gps.encode(GPS_Serial.read());

  gpsSat = gps.satellites.isValid() ? gps.satellites.value() : 0;

  gpsFix = gps.location.isValid() && gps.location.age() < 2000 && gpsSat >= 3;

  if (gpsFix) {
    gpsLat = gps.location.lat();
    gpsLng = gps.location.lng();
  }
}

// =================================================================
// ★ 新增：Button & Buzzer 腳位
// =================================================================
#define BUTTON_PIN  25  
#define BUZZER_PIN  21  

int buzzMode = 0;       // 0: 關閉, 1: 開啟
bool lastButtonState = HIGH; // 按鈕上一次的狀態 (用於邊緣檢測)


//=================================================================
//   ★ Sensor OLED 畫面
//=================================================================

void drawSensorStandby(unsigned long lastSend) {
  oled.clearBuffer();
  oled.setFont(u8g2_font_ncenB08_tr);

  // T/H
  oled.setCursor(0, 12);
  oled.printf("T:%.1fC  H:%.1f%%", temperature, humidity);

  // Lat
  oled.setCursor(0, 24);
  oled.print("Lat:");
  oled.print(gpsLat, 4);

  // Lng
  oled.setCursor(0, 36);
  oled.print("Lng:");
  oled.print(gpsLng, 4);

  // 倒數
  unsigned long diff = millis() - lastSend;
  int countdown = 10 - diff / 1000;
  if (countdown < 0) countdown = 0;

  oled.setCursor(0, 48);
  oled.print("Next TX: ");
  oled.print(countdown);
  oled.print("s");

  oled.sendBuffer();
}

void showStatus(const char* msg) {
  oled.clearBuffer();
  oled.setFont(u8g2_font_ncenB08_tr);
  oled.setCursor(0, 24);
  oled.print(msg);
  oled.sendBuffer();
}

//=================================================================
//   ★ Gateway：WiFi + HTTP
//=================================================================
#ifndef INITIATING_NODE

WiFiMulti wifiMulti;

const char* ssid1     = "Stephen_3F";
const char* password1 = "root1234";

const char* ssid2     = "enohpi61";
const char* password2 = "rootroot";

String API_URL = "https://monarchistic-organizationally-magdalene.ngrok-free.dev/api/sensor-data";

volatile bool loraReceivedFlag = false;

ICACHE_RAM_ATTR
void setFlag() { loraReceivedFlag = true; }

void initWiFi() {
  wifiMulti.addAP(ssid1, password1);
  wifiMulti.addAP(ssid2, password2);

  while (wifiMulti.run() != WL_CONNECTED) delay(500);
}

void sendToBackend(float t, float h, double lat, double lng, int sat) {
  HTTPClient http;
  http.begin(API_URL);
  http.addHeader("Content-Type", "application/json");

  String json = "{";
  json += "\"temp\":" + String(t) + ",";
  json += "\"hum\":" + String(h) + ",";
  json += "\"lat\":" + String(lat,6) + ",";
  json += "\"lng\":" + String(lng,6) + ",";
  json += "\"sat\":" + String(sat);
  json += "}";

  http.POST(json);
  http.end();
}

// Parse
bool parsePayload(String s, float &t, float &h, double &lat, double &lng, int &sat) {
  int p1 = s.indexOf(',');
  int p2 = s.indexOf(',', p1+1);
  int p3 = s.indexOf(',', p2+1);
  int p4 = s.indexOf(',', p3+1);

  if (p1<0 || p2<0 || p3<0 || p4<0) return false;

  t = s.substring(0,p1).toFloat();
  h = s.substring(p1+1,p2).toFloat();
  lat = s.substring(p2+1,p3).toDouble();
  lng = s.substring(p3+1,p4).toDouble();
  sat = s.substring(p4+1).toInt();

  return true;
}

#endif

//=================================================================
//   ★ Setup
//=================================================================
void setup() {
  Serial.begin(115200);
  oled.begin();

  pinMode(BUTTON_PIN, INPUT_PULLUP); // 使用內建上拉電阻，按鈕平常是 HIGH，按下是 LOW
  
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, HIGH);     // 預設關閉

  SPI_LORA.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  radio.begin(923.875, 125.0, 7, 5, 0x12, 13, 10);

#ifdef INITIATING_NODE
  GPS_Serial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
#else
  initWiFi();
  radio.setDio1Action(setFlag);
  radio.startReceive();
#endif
}

//=================================================================
//   ★ Loop
//=================================================================
void loop() {

#ifdef INITIATING_NODE
  //==================== Sensor Node ====================

  updateGPS();
  readAM2120(humidity, temperature);


// ========== 按鈕 + 蜂鳴器（含防抖動） ==========

static bool buzzerOn = false;
static unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 30;  // 30ms 防抖動
int reading = digitalRead(BUTTON_PIN);
// 偵測到變化時重置防抖動計時
if (reading != lastButtonState) {
    lastDebounceTime = millis();
}
// 超過 debounceDelay 後才確認狀態
if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading == LOW) {
        buzzerOn = true;
    } else {
        buzzerOn = false;
    }
}
// 根據結果控制蜂鳴器（低電平響）
digitalWrite(BUZZER_PIN, buzzerOn ? LOW : HIGH);
lastButtonState = reading;
//================================================

  static unsigned long lastSend = 0;

  drawSensorStandby(lastSend);

  // 到時間送
  if (millis() - lastSend >= 10000) {
    lastSend = millis();

    String payload =
      String(temperature,1) + "," +
      String(humidity,1)    + "," +
      String(gpsLat,6)      + "," +
      String(gpsLng,6)      + "," +
      String(gpsSat);

    showStatus("Sending...");

    int state = radio.transmit(payload);

    if (state == RADIOLIB_ERR_NONE) {
      showStatus("TX OK");

    } else {
      oled.clearBuffer();
      oled.setCursor(0,24);
      oled.printf("TX Error:%d", state);
      oled.sendBuffer();
    }

    delay(1000);  // 顯示 1 秒，再回到主畫面
  }

#else
  //==================== Gateway Node ====================

  if (loraReceivedFlag) {
    loraReceivedFlag = false;

    String str;
    radio.readData(str);

    float t,h;
    double lat,lng;
    int sat;

    if (parsePayload(str, t,h,lat,lng,sat)) {
      sendToBackend(t,h,lat,lng,sat);
    }

    radio.startReceive();
  }

#endif
}
