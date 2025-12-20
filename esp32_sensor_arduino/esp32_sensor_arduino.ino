#include <Arduino.h>
#include <RadioLib.h>
#include <U8g2lib.h>
#include <TinyGPS++.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiMulti.h>

// ★★★ 設定角色 ★★★
#define INITIATING_NODE // 如果是 Sensor 端請保留此行 (取消註解)；如果是 Gateway 端請註解掉此行

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

float temperature = 0.0f;
float humidity    = 0.0f;     
double gpsLat = 0, gpsLng = 0; 
int gpsSat = 0;

// [新增] 取得 ChipID 的小工具函式 (Sensor 和 Gateway 都可以用)
String getChipId() {
  uint64_t chipid = ESP.getEfuseMac(); // 讀取 eFuse 中的唯一識別碼 (其實就是 MAC)
  char chipIdBuf[13];
  // 格式化成 12 位數的 Hex 字串 (例如: A1B2C3D4E5F6)
  snprintf(chipIdBuf, 13, "%04X%08X", (uint16_t)(chipid >> 32), (uint32_t)chipid);
  return String(chipIdBuf);
}

//=================================================================
//   Sensor 專用邏輯 (Deep Sleep 版本)
//=================================================================
#ifdef INITIATING_NODE

#define AM2120_PIN  32
#define GPS_RX 23       
#define GPS_TX 12      
TinyGPSPlus gps;
HardwareSerial GPS_Serial(1);

String myChipId = ""; // 用來存這台 Sensor 的 ID

// AM2120 讀取函式
bool readAM2120(float &h, float &t) {
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

void showMsg(const char* line1, const char* line2) { 
  oled.clearBuffer();
  oled.setFont(u8g2_font_ncenB08_tr);
  oled.setCursor(0, 16); oled.print(line1);
  oled.setCursor(0, 32); oled.print(line2);
  oled.sendBuffer();
}

void enterDeepSleep() {
    Serial.println("Entering Deep Sleep...");
    showMsg("Sleep Mode", "Press Btn to Wake");
    delay(1000); 
    
    oled.setPowerSave(1);
    radio.sleep();
    
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BUTTON_PIN, 0); 
    esp_deep_sleep_start();
}

void performTask() {
    showMsg("Waking Up...", "Detecting...");
    unsigned long detectStart = millis();
    bool am2120Success = false;
    
    while(millis() - detectStart < 2000) {
        while (GPS_Serial.available()) gps.encode(GPS_Serial.read());
        if (!am2120Success) {
            am2120Success = readAM2120(humidity, temperature);
        }
    }

    if (gps.location.isValid()) {
        gpsLat = gps.location.lat();
        gpsLng = gps.location.lng();
        gpsSat = gps.satellites.value();
    }

    int btnState = 1; 

    // [修改] 組合 Payload，加入 ChipID 在最後
    String payload = String(temperature, 1) + "," + String(humidity, 1) + "," +
                     String(gpsLat, 6) + "," + String(gpsLng, 6) + "," +
                     String(gpsSat) + "," + String(btnState) + "," + 
                     myChipId; // <--- 使用 ChipID
    
    showMsg("Sending Data...", payload.c_str());
    Serial.println("Sending: " + payload);
    
    radio.transmit(payload);

    showMsg("Wait Reply...", "Listening");
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
        Serial.println("Buzzer Triggered!");
        for(int i=0; i<30; i++) { 
            digitalWrite(BUZZER_PIN, LOW); delay(50);
            digitalWrite(BUZZER_PIN, HIGH); delay(50);
        }
    } else {
        showMsg("Done", "No Command");
        delay(500);
    }
}

#else
//=================================================================
//   Gateway 專用邏輯 (被動接收模式)
//=================================================================
WiFiMulti wifiMulti;
String API_URL = "https://monarchistic-organizationally-magdalene.ngrok-free.dev/api/sensor-data";

const char* JWT_TOKEN = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJ1c2VyIjoiYWRtaW4iLCJyb2xlIjoiYWRtaW4iLCJleHAiOjE3OTc2OTA4NDN9.Tqf9gQGjjr27vxddFEcE_qefZ264tm2OuV764Qa7Oj4"; 

void initWiFi() {
  WiFi.mode(WIFI_STA);
  Serial.print("Gateway ChipID: ");
  Serial.println(getChipId()); // 顯示 Gateway 自己的 ID

  wifiMulti.addAP("Stephen_3F", "root1234");
  wifiMulti.addAP("enohpi61", "rootroot");
  
  oled.clearBuffer(); oled.setCursor(0,16);
  oled.print("Connecting WiFi..."); oled.sendBuffer();
  
  while (wifiMulti.run() != WL_CONNECTED) delay(500);
  
  oled.clearBuffer();
  oled.setCursor(0,16); oled.print("WiFi OK");
  oled.setCursor(0,32); oled.print("IP: ");
  oled.setCursor(0,48); oled.print(WiFi.localIP()); 
  oled.sendBuffer();
}

// [修改] 參數名稱改為 sensorChipId 比較明確
bool sendToBackend(bool isValid, float t, float h, double lat, double lng, int sat, int btn, String sensorChipId) {
  if(wifiMulti.run() != WL_CONNECTED) return false;

  HTTPClient http;
  http.begin(API_URL);
  http.addHeader("Content-Type", "application/json");
  String authHeader = "Bearer " + String(JWT_TOKEN);
  http.addHeader("Authorization", authHeader);

  String json = "{";
  // [修改] 使用 Sensor 傳過來的 ChipID
  json += "\"machine_id\":\"" + sensorChipId + "\","; 

  if (isValid) {
      json += "\"status\":\"ok\",";
      json += "\"temp\":" + String(t) + ",";
      json += "\"hum\":" + String(h) + ",";
      json += "\"lat\":" + String(lat, 6) + ",";
      json += "\"lng\":" + String(lng, 6) + ",";
      json += "\"sat\":" + String(sat) + ",";
      json += "\"button\":" + String(btn);
  } else {
      json += "\"status\":\"timeout\",";
      json += "\"temp\":0,\"hum\":0,\"lat\":0,\"lng\":0,\"sat\":0,\"button\":0";
  }
  json += "}";

  int httpCode = http.POST(json);

  String payload = http.getString();
  http.end();

  if (httpCode == 401 || httpCode == 403) {
      Serial.println("Upload Failed: Invalid Token!");
  }

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
    Serial.println(F("LoRa init failed!"));
    while (true);
  }

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, HIGH); 

// --- Sensor 端流程 ---
#ifdef INITIATING_NODE
  // [修改] 這裡改用 ChipID，不再依賴 WiFi.macAddress()
  myChipId = getChipId(); 
  Serial.print("Sensor ChipID: ");
  Serial.println(myChipId);
  
  // 顯示在螢幕上方便註冊
  oled.clearBuffer();
  oled.setCursor(0,16); oled.print("Sensor Ready");
  oled.setCursor(0,32); oled.print("ID:");
  oled.setCursor(0,48); oled.print(myChipId);
  oled.sendBuffer();
  delay(3000); // 暫停一下讓使用者抄寫 ID

  GPS_Serial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
  performTask();
  enterDeepSleep();

// --- Gateway 端流程 ---
#else
  initWiFi(); 
  
  oled.clearBuffer(); 
  oled.setCursor(0,16); oled.print("Gateway Ready"); 
  oled.setCursor(0,32); oled.print("Listening..."); 
  oled.sendBuffer();
  
  radio.startReceive(); 
#endif
}

//=================================================================
//   LOOP
//=================================================================
void loop() {
#ifdef INITIATING_NODE
  // Sensor 端使用 Deep Sleep

#else
  // --- Gateway 端邏輯 ---
  String receivedStr;
  
  if (radio.receive(receivedStr) == RADIOLIB_ERR_NONE) {
      Serial.println("RX: " + receivedStr);
      
      // 基本長度檢查
      if (receivedStr.length() > 10) {
          float t, h;
          double lat, lng;
          int sat, btn;
          String r_chipId; // [修改] 接收到的 sensor ChipID
          
          // CSV 解析 (格式: temp,hum,lat,lng,sat,btn,ChipID)
          int p1 = receivedStr.indexOf(',');
          int p2 = receivedStr.indexOf(',', p1+1);
          int p3 = receivedStr.indexOf(',', p2+1);
          int p4 = receivedStr.indexOf(',', p3+1);
          int p5 = receivedStr.indexOf(',', p4+1);
          int p6 = receivedStr.indexOf(',', p5+1); 
          
          if (p1 > 0 && p6 > 0) {
              t = receivedStr.substring(0,p1).toFloat();
              h = receivedStr.substring(p1+1,p2).toFloat();
              lat = receivedStr.substring(p2+1,p3).toDouble();
              lng = receivedStr.substring(p3+1,p4).toDouble();
              sat = receivedStr.substring(p4+1).toInt();
              btn = receivedStr.substring(p5+1, p6).toInt(); 
              r_chipId = receivedStr.substring(p6+1);       

              r_chipId.trim(); // 去除空白

              oled.clearBuffer();
              oled.setCursor(0,16);
              oled.printf("RX Temp: %.1f", t);
              oled.setCursor(0,32);
              oled.print("ID: " + r_chipId); 
              oled.setCursor(0,48);
              oled.print("Uploading...");
              oled.sendBuffer();

              // 上傳至後端 (傳入解析出來的 Sensor ChipID)
              bool triggerBuzzer = sendToBackend(true, t, h, lat, lng, sat, btn, r_chipId);
              
              if (triggerBuzzer) {
                  Serial.println("Command: BUZZER_ON. Sending back...");
                  oled.setCursor(0,48); oled.print("CMD: BUZZ"); oled.sendBuffer();
                  
                  delay(50);
                  radio.transmit("CMD_BUZZ");
                  radio.startReceive(); 
              } else {
                  oled.setCursor(0,48); oled.print("Upload OK"); oled.sendBuffer();
              }
          }
      }
  }
#endif
}