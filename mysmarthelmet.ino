#include <MPU6050_tockn.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h> 
#include <time.h>

// --- [1: setting] ---
const char* ssid = "12345678"; 
const char* password = "12345678";
const char* lineAccessToken = "zyjDmAadRmIXh319vRJRy3jG3AuDIOyMJGtf8gJ2CLtE0VNfK3ssrAyBTvmwac6xyINGwmJtNHHj8HWtxnen6f4S2wzYORGAtX8A9m9rqxcqtsBqvXyNPiDzSMLnvYiZCdLNJCscYYb/wwnVQ386KAdB04t89/1O/w1cDnyilFU="; 
const char* myUserId = "U2041da3d5d7a001af9976117979b42fe"; 

// *** URL Webhook MacroDroid *** ดึงgpsจากเเอปมือถือ not work ตอนนี้
const char* macroDroidURL = "https://trigger.macrodroid.com/ac019c44-9963-4d09-9497-32017f6e206c/accident_alert";

// --- [2: pin] ---
#define I2C_SDA 4
#define I2C_SCL 5
#define BUZZER_PIN 2  
#define BUTTON_PIN 3  

MPU6050 mpu6050(Wire);
float alpha = 0.8; 
float fX = 0, fY = 0, fZ = 0; 
TaskHandle_t TaskAlert_Handle = NULL; 
bool isAlerting = false;
volatile bool cancelled = false; // ใช้ volatile เพื่อให้ทำงานกับ Interrupt ได้

// --- [ฟังก์ชัน Interrupt: เวลากดปุ่มจะแทรกแซงการทำงานทันที] ---
void IRAM_ATTR buttonISR() {
  cancelled = true;
}

// --- [MacroDroid เเตกอยู่] ---
void triggerMobileGPS() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[SYSTEM] sending Webhook ...");
    HTTPClient http;
    http.begin(macroDroidURL);
    int httpCode = http.GET();
    if (httpCode == 200) Serial.println("[MACRODROID] success! (200)");
    else Serial.printf("[MACRODROID] code Error: %d\n", httpCode);
    http.end();
  }
}

// --- [LINE API] ---
void sendLineMessage(String message) {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure secureClient;
    secureClient.setInsecure(); 
    HTTPClient http;
    http.setTimeout(8000); 
    http.begin(secureClient, "https://api.line.me/v2/bot/message/push");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + String(lineAccessToken));
    http.setReuse(false); 
    
    String jsonPayload = "{\"to\":\"" + String(myUserId) + "\",\"messages\":[{\"type\":\"text\",\"text\":\"" + message + "\"}]}";
    int httpResponseCode = http.POST(jsonPayload);
    
    if (httpResponseCode == 200) Serial.println("[LINE] sent!");
    else Serial.printf("[LINE] code Error: %d\n", httpResponseCode);
    
    http.end();
  }
}

// --- [get time] ---
String getTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "Time is not specified"; 
  char timeStringBuff[50];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%d/%m/%Y %H:%M:%S", &timeinfo);
  return String(timeStringBuff);
}

void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // ปุ่ม Interrupt 
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonISR, FALLING);

  Serial.println("\n[SYSTEM] connecting WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\n[SUCCESS] WiFi Connected!");

  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  
  Wire.begin(I2C_SDA, I2C_SCL);
  mpu6050.begin();
  Serial.println("[SUCCESS] MPU6050 started!");

  xTaskCreate(TaskIMU, "IMU_Task", 4096, NULL, 2, NULL); 
  xTaskCreate(TaskAlert, "Alert_Task", 8192, NULL, 1, &TaskAlert_Handle);
}

void loop() {}

void TaskIMU(void *pvParameters) {
  // --- [ป้องกัน error code -1 จากไลน์] ---
  mpu6050.update();
  fX = mpu6050.getAccX();
  fY = mpu6050.getAccY();
  fZ = mpu6050.getAccZ();
  // ------------------------------------------------

  while (1) {
    mpu6050.update();
    fX = (alpha * mpu6050.getAccX()) + ((1.0 - alpha) * fX);
    fY = (alpha * mpu6050.getAccY()) + ((1.0 - alpha) * fY);
    fZ = (alpha * mpu6050.getAccZ()) + ((1.0 - alpha) * fZ);

    float magnitude = sqrt(pow(fX, 2) + pow(fY, 2) + pow(fZ, 2));

    // ปรับระดับความเเรง magnitude
    if (magnitude > 2.0 && !isAlerting) {
      Serial.println("\n[IMU] !!! Fall detected!");
      xTaskNotifyGive(TaskAlert_Handle); 
    }
    vTaskDelay(10 / portTICK_PERIOD_MS); 
  }
}

// --- [ส่วนเเจ้งเตือน] ---
void TaskAlert(void *pvParameters) {
  while (1) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY); 
    
    isAlerting = true;
    cancelled = false; // รีเซ็ตค่าปุ่มกด
    unsigned long startTime = millis();

    // 1. ชนเเล้ว ร้องเตือน 0.5 วิ แล้วปิดทันที
    tone(BUZZER_PIN, 2700);
    vTaskDelay(500 / portTICK_PERIOD_MS); 
    noTone(BUZZER_PIN); // *** บังคับปิดเสียงก่อนบอร์ดไปดึงเน็ต ***

    // 2. ดึงข้อมูลเเมพจากเเอพ ตอนนี้เเตกอยู่ 
    triggerMobileGPS(); 
    String accidentTime = getTimestamp();
    sendLineMessage("🚨 [MySmartHelmet-BU] ตรวจพบการกระแทก!\\n⏰ เวลา: " + accidentTime + "\\n\\n⚠️ กำลังสั่งให้มือถือดึงพิกัดแผนที่...");

    // 3. เข้าสู่ช่วงนับเวลา 15 วินาที (ตี๊ด...ๆ)
    while (millis() - startTime < 15000) { 
      if (cancelled) break;
      
      tone(BUZZER_PIN, 2700); 
      vTaskDelay(200 / portTICK_PERIOD_MS);
      noTone(BUZZER_PIN);
      vTaskDelay(200 / portTICK_PERIOD_MS);
      
      if (cancelled) break;
    }
    noTone(BUZZER_PIN); // เเล้วปิดสนิท

    // 4. ส่งผลสรุป
    if (cancelled) {
      sendLineMessage("🟢 [อัปเดตสถานการณ์]\\nผู้บาดเจ็บกดยกเลิกการแจ้งเตือนแล้ว! (มีสติสัมปชัญญะ)");
    } else {
      sendLineMessage("🆘 [อัปเดตสถานการณ์: วิกฤต!!]\\nไม่มีการตอบสนองจากผู้บาดเจ็บ! อันตรายร้ายแรง โปรดเข้าช่วยเหลือด่วน!!");
    }
    
    vTaskDelay(3000 / portTICK_PERIOD_MS); 
    isAlerting = false;
    Serial.println("[SYSTEM] รีเซ็ตระบบ พร้อมรับการกระแทกครั้งต่อไป...");
  }
}

