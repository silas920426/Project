LoRa Emergency Communication System
Project Overview

本專案為大學畢業專題「颱風災害長者緊急通訊系統」。

當災害發生導致網路基礎設施中斷時，長者可透過按鈕發送求救訊號，利用 LoRa 無線通訊技術將資料從 Sensor Device 傳送至 Gateway Device，再由 Gateway 將資料上傳至 Web Server，提供救難人員即時查看求救訊息與定位資訊。

![image](https://github.com/silas920426/Project/blob/main/Doc1.docx)

功能
LoRa 無線通訊
SOS 緊急求救訊號傳送
GPS 即時定位回傳
感測器資料蒐集
Flask RESTful API
SQLite 資料儲存
Web 即時監控平台
裝置狀態監控


裝置
Sensor Device
ESP32-WROOM
GPS NEO-7M
LoRa LLCC68
AM2120 溫溼度感測器
OLED Display
Buzzer
Emergency Button
Gateway Device
ESP32-WROOM
LoRa LLCC68


使用語法
後端
Python
Flask
RESTful API
SQLite3
前端
HTML
JavaScript
韌體開發
C++
ESP32 Arduino Framework


負責內容
ESP32 韌體程式開發
LoRa 通訊模組整合
GPS 定位資料處理
Flask API 開發
SQLite 資料庫設計
Web 監控平台建置
系統整合測試

專案結果
完成 Sensor → Gateway → Server 資料傳輸
成功實現無 WiFi 環境下求救訊號傳送
即時顯示 GPS 定位資訊
建立 Web 平台供救難人員監控設備狀態
完成硬體、通訊、後端與前端整合驗證

Technologies

Python | Flask | RESTful API | SQLite | ESP32 | Arduino | LoRa | GPS | HTML | JavaScript | C++
