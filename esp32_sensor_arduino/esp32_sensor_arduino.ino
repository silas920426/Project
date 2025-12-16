#include <Arduino.h>
#include <RadioLib.h>
#include <U8g2lib.h>
#include <TinyGPS++.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiMulti.h>

//設定角色 
#define INITIATING_NODE // 如果是 Sensor 端保留此行；如果是 Gateway 註解掉此行

// --- LoRa 定義 ---
#define LORA_SCK    5
#define LORA_MISO   35
#define LORA_MOSI   27
#define LORA_CS     18
#define LORA_DIO2   36
#define LORA_BUSY   34
#define LORA_RESET  0

SPIClass SPI_LORA(VSPI);
LLCC68 radio = new Module(LORA_CS, LORA_DIO2, LORA_RESET, LORA_BUSY, SPI_LORA);

// --- OLED 定義 ---
#define I2C_SDA     4
#define I2C_SCL     15
U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE, I2C_SCL, I2C_SDA);

// --- 按鈕&蜂鳴器 ---
#define BUTTON_PIN  25  
#define BUZZER_PIN  21  


float temperature = 0.0f;      //溫度
float humidity    = 0.0f;      //濕度
double gpsLat = 0, gpsLng = 0; //經緯度
int gpsSat = 0;                //衛星數

//=================================================================
//   Sensor 專用邏輯
//=================================================================
#ifdef INITIATING_NODE

#define AM2120_PIN  32  // AM2120 資料腳位
#define GPS_RX 23       // GPS 模組 RX 腳位，接 ESP32 發送端 TX
#define GPS_TX 12       // GPS 模組 TX 腳位，接 ESP32 接收端 RX
TinyGPSPlus gps;
HardwareSerial GPS_Serial(1);


bool readAM2120(float &h, float &t) {   // 讀取 AM2120
  uint8_t data[5] = {0};
  pinMode(AM2120_PIN, OUTPUT);
  digitalWrite(AM2120_PIN, LOW); delayMicroseconds(1800);
  digitalWrite(AM2120_PIN, HIGH); delayMicroseconds(30);
  pinMode(AM2120_PIN, INPUT);
  unsigned long tStart = micros();
  while (digitalRead(AM2120_PIN) == HIGH) if (micros() - tStart > 100) return false;
  while (digitalRead(AM2120_PIN) == LOW)  if (micros() - tStart > 200) return false;
  while (digitalRead(AM2120_PIN) == HIGH) if (micros() - tStart > 200) return false;
  for (int i = 0; i < 40; i++) {
    while (digitalRead(AM2120_PIN) == LOW);
    unsigned long tHigh = micros();
    while (digitalRead(AM2120_PIN) == HIGH);
    if ((micros() - tHigh) > 40) data[i/8] |= (1 << (7 - (i%8)));
  }
  if ((uint8_t)(data[0]+data[1]+data[2]+data[3]) != data[4]) return false;
  h = ((data[0]<<8) + data[1]) * 0.1f;
  t = (((data[2]&0x7F)<<8) + data[3]) * 0.1f;
  if (data[2] & 0x80) t = -t;
  return true;
}

void updateGPS() {  // 讀取 GPS 資料
  while (GPS_Serial.available()) gps.encode(GPS_Serial.read());
  if (gps.location.isValid()) {
    gpsLat = gps.location.lat();
    gpsLng = gps.location.lng();
    gpsSat = gps.satellites.value();
  }
}

void showMsg(const char* line1, const char* line2) { // OLED 顯示訊息
  oled.clearBuffer();
  oled.setFont(u8g2_font_ncenB08_tr);
  oled.setCursor(0, 16); oled.print(line1);
  oled.setCursor(0, 32); oled.print(line2);
  oled.sendBuffer();
}

// 統一發送函式：處理發送 + 等待 Buzzer 回應
void sendSensorData(bool isUrgent) {
    readAM2120(humidity, temperature);
    int btnState = isUrgent ? 1 : 0; 

    String payload = String(temperature, 1) + "," + String(humidity, 1) + "," +
                     String(gpsLat, 6) + "," + String(gpsLng, 6) + "," +
                     String(gpsSat) + "," + String(btnState);

    if (isUrgent) showMsg("BTN PRESSED!", "Sending...");
    else showMsg("Polled", "Sending...");
    
    Serial.println("Sending: " + payload);
    radio.transmit(payload);

    // 等待回應
    unsigned long waitStart = millis();
    bool buzzTriggered = false;
    radio.startReceive(); 

    while(millis() - waitStart < 3000) {
        String reply;
        if (radio.receive(reply) == RADIOLIB_ERR_NONE) {
            if (reply == "CMD_BUZZ") {
                buzzTriggered = true;
                break;
            }
        }
    }

    if (buzzTriggered) {
        showMsg("ALARM!", "BUZZER ON");
        for(int i=0; i<30; i++) { 
            digitalWrite(BUZZER_PIN, LOW); delay(50);
            digitalWrite(BUZZER_PIN, HIGH); delay(50);
        }
    }
    showMsg("Sensor Listening...", "Waiting...");
    radio.startReceive(); 
}

#else
//=================================================================
//   Gateway 專用邏輯
//=================================================================
WiFiMulti wifiMulti;
String API_URL = "https://monarchistic-organizationally-magdalene.ngrok-free.dev/api/sensor-data";

void initWiFi() {
  wifiMulti.addAP("Stephen_3F", "root1234");
  wifiMulti.addAP("enohpi61", "rootroot");
  oled.clearBuffer(); oled.setCursor(0,16); oled.print("Connecting WiFi..."); oled.sendBuffer();
  while (wifiMulti.run() != WL_CONNECTED) delay(500);
  oled.setCursor(0,32); oled.print("WiFi OK"); oled.sendBuffer();
}

// 修改後的上傳函式：支援「正常資料」與「無資料(Timeout)」
bool sendToBackend(bool isValid, float t, float h, double lat, double lng, int sat, int btn) {
  if(wifiMulti.run() != WL_CONNECTED) return false;

  HTTPClient http;
  http.begin(API_URL);
  http.addHeader("Content-Type", "application/json");

  String json = "{";
  if (isValid) {
      json += "\"status\":\"ok\","; 
      json += "\"temp\":" + String(t) + ",";
      json += "\"hum\":" + String(h) + ",";
      json += "\"lat\":" + String(lat, 6) + ",";
      json += "\"lng\":" + String(lng, 6) + ",";
      json += "\"sat\":" + String(sat) + ",";
      json += "\"button\":" + String(btn);
  } else {
      // 逾時沒收到資料，上傳 0，並標記 timeout
      json += "\"status\":\"timeout\","; 
      json += "\"temp\":0,";
      json += "\"hum\":0,";
      json += "\"lat\":0,";
      json += "\"lng\":0,";
      json += "\"sat\":0,";
      json += "\"button\":0";
  }
  json += "}";

  int httpCode = http.POST(json);
  String payload = http.getString();
  http.end();

  Serial.println("Server Response: " + payload);
  if (payload.indexOf("BUZZER_ON") > 0) return true; 
  return false;
}
#endif

//=================================================================
//   SETUP
//=================================================================
void setup() {
  Serial.begin(115200);
  oled.begin();
  oled.setFont(u8g2_font_ncenB08_tr);

  SPI_LORA.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  int state = radio.begin(923.875, 125.0, 7, 5, 0x12, 13, 10);
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println(F("LoRa init success!"));
  } else {
    while (true);
  }

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, HIGH); 

//sensor端
#ifdef INITIATING_NODE
  GPS_Serial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
  showMsg("Sensor Hybrid", "Ready");
  radio.startReceive(); 
#else
//Gateway端
  initWiFi();
  oled.clearBuffer(); oled.setCursor(0,16); oled.print("Gateway Active"); oled.sendBuffer();
  radio.startReceive(); 
#endif
}

//=================================================================
//   LOOP
//=================================================================
void loop() {

#ifdef INITIATING_NODE
  // Sensor 端
  updateGPS();

  if (digitalRead(BUTTON_PIN) == LOW) {
      delay(50);
      if (digitalRead(BUTTON_PIN) == LOW) {
          Serial.println("Button Pressed! Sending actively...");
          sendSensorData(true); 
          while(digitalRead(BUTTON_PIN) == LOW) { delay(10); }
      }
  }

  String str;
  if (radio.receive(str) == RADIOLIB_ERR_NONE) {
      if (str == "REQ") {
          Serial.println("Poll received! Sending...");
          sendSensorData(false);
      }
  }

#else
  // Gateway 端
  static unsigned long lastPollTime = 0;
  static unsigned long pollStartTime = 0; // 記錄發出 REQ 的時間
  static bool isWaitingForReply = false;  // 標記是否正在等回覆
  
  // 每 60 秒發送一次 REQ
  if (millis() - lastPollTime > 60000) {
      lastPollTime = millis();
      
      Serial.println("Time to Poll, sending REQ...");
      oled.setCursor(0,16); oled.print("Polling..."); oled.sendBuffer();

      radio.transmit("REQ"); 
      radio.startReceive();
      
      // 設定逾時監控
      isWaitingForReply = true;
      pollStartTime = millis();
  }

  // 接收任務
  String receivedStr;
  if (radio.receive(receivedStr) == RADIOLIB_ERR_NONE) {
      // 收到資料了，取消逾時等待
      isWaitingForReply = false;
      
      Serial.println("RX: " + receivedStr);
      if (receivedStr.length() > 5) {
          float t, h;
          double lat, lng;
          int sat, btn;
          
          int p1 = receivedStr.indexOf(',');
          int p2 = receivedStr.indexOf(',', p1+1);
          int p3 = receivedStr.indexOf(',', p2+1);
          int p4 = receivedStr.indexOf(',', p3+1);
          int p5 = receivedStr.indexOf(',', p4+1);

          if (p1 > 0) {
              t = receivedStr.substring(0,p1).toFloat();
              h = receivedStr.substring(p1+1,p2).toFloat();
              lat = receivedStr.substring(p2+1,p3).toDouble();
              lng = receivedStr.substring(p3+1,p4).toDouble();
              sat = receivedStr.substring(p4+1).toInt();
              btn = receivedStr.substring(p5+1).toInt();

              oled.clearBuffer();
              oled.setCursor(0,16); oled.printf("RX Data (Btn:%d)", btn);
              oled.setCursor(0,32); oled.print("Uploading...");
              oled.sendBuffer();

              //正常上傳 (isValid = true)
              bool triggerBuzzer = sendToBackend(true, t, h, lat, lng, sat, btn);

              if (triggerBuzzer) {
                  Serial.println("Triggering Remote Buzzer...");
                  delay(50); 
                  radio.transmit("CMD_BUZZ");
                  radio.startReceive(); 
              } else {
                  oled.setCursor(0,48); oled.print("Upload OK"); 
                  oled.sendBuffer();
              }
          }
      }
  }
// 如果超過5秒沒收到資料則回傳0 0 0 0 0 0
  if (isWaitingForReply && (millis() - pollStartTime > 5000)) {
      isWaitingForReply = false; // 停止等待

      Serial.println("Poll Timeout! No Data.");
      oled.clearBuffer();
      oled.setCursor(0,16); oled.print("Poll Timeout");
      oled.setCursor(0,32); oled.print("Upld Empty...");
      oled.sendBuffer();

      // 逾時上傳 (isValid = false)
      sendToBackend(false, 0, 0, 0, 0, 0, 0);
      
      oled.setCursor(0,48); oled.print("Empty Sent");
      oled.sendBuffer();
  }
#endif
}