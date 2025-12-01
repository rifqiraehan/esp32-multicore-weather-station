#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

#define DHTPIN 21
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// LED indikator cuaca
#define LED_CERAH   15
#define LED_BERAWAN 7
#define LED_HUJAN   6
#define LED_BURUK   5  

// Buzzer
#define BUZZER 12

// Potensiometer → simulasi kecepatan angin
#define POT_PIN 17

struct SensorData {
  float temperature;
  float humidity;
  int windSpeed;
};

QueueHandle_t sensorQueue;
SemaphoreHandle_t serialMutex;

void task_baca_sensor(void *pvParameters);
void task_update_layar(void *pvParameters);

void setup() {
  Serial.begin(115200);
  dht.begin();

  pinMode(LED_CERAH, OUTPUT);
  pinMode(LED_BERAWAN, OUTPUT);
  pinMode(LED_HUJAN, OUTPUT);
  pinMode(LED_BURUK, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED gagal!");
    while (1);
  }

  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.print("Booting...");
  display.display();
  delay(1000);

  serialMutex = xSemaphoreCreateMutex();
  sensorQueue = xQueueCreate(1, sizeof(SensorData));

  // Core 1
  xTaskCreatePinnedToCore(task_baca_sensor, "TaskSensor", 4096, NULL, 1, NULL, 1);

  // Core 0
  xTaskCreatePinnedToCore(task_update_layar, "TaskLayar", 4096, NULL, 1, NULL, 0);
}

void loop() {
  vTaskDelay(portMAX_DELAY);
}

void task_baca_sensor(void *pvParameters) {
  for (;;) {
    SensorData data;

    data.temperature = dht.readTemperature();
    data.humidity = dht.readHumidity();
    data.windSpeed = analogRead(POT_PIN); // 0–4095

    if (isnan(data.temperature) || isnan(data.humidity)) {
      if (xSemaphoreTake(serialMutex, portMAX_DELAY)) {
        Serial.println("[Sensor] Gagal membaca DHT!");
        xSemaphoreGive(serialMutex);
      }
    } else {
      xQueueOverwrite(sensorQueue, &data);
    }

    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

void tampilkanCuaca(String kondisi) {
  display.setTextSize(1);
  display.setCursor(0, 50);
  display.print("Cuaca: ");
  display.print(kondisi);
}

void playTone(int freq, int dur) {
  tone(BUZZER, freq, dur);
  delay(dur);
  noTone(BUZZER);
}

void task_update_layar(void *pvParameters) {
  SensorData data;

  for (;;) {
    if (xQueueReceive(sensorQueue, &data, portMAX_DELAY)) {

      // Reset LED
      digitalWrite(LED_CERAH, LOW);
      digitalWrite(LED_BERAWAN, LOW);
      digitalWrite(LED_HUJAN, LOW);
      digitalWrite(LED_BURUK, LOW);

      int angin = map(data.windSpeed, 0, 4095, 0, 100);

      String kondisi = "";
      
      // Prioritas 1: Suhu sangat tinggi
      if (data.temperature > 35) {
        kondisi = "SUHU TINGGI";
        digitalWrite(LED_BURUK, HIGH);

        // Bunyi seperti badai (3x beep cepat)
        for (int i = 0; i < 3; i++) {
          playTone(200, 100);
        }
      }

      // Cerah
      else if (data.humidity < 60 && data.temperature >= 24 && data.temperature <= 32 && angin < 30) {
        kondisi = "CERAH";
        digitalWrite(LED_CERAH, HIGH);
        playTone(900, 80);

      }

      // Berawan
      else if (data.humidity < 80 && angin < 40) {
        kondisi = "BERAWAN";
        digitalWrite(LED_BERAWAN, HIGH);
        playTone(700, 80);

      }

      // Hujan
      else if (data.humidity >= 80 && angin < 60) {
        kondisi = "HUJAN";
        digitalWrite(LED_HUJAN, HIGH);
        playTone(500, 120);

      }

      // Kondisi buruk / badai
      else {
        kondisi = "BADAI";
        digitalWrite(LED_BURUK, HIGH);

        for (int i = 0; i < 3; i++) {
          playTone(200, 100);
        }
      }

      // ========== OLED DISPLAY ==========
      display.clearDisplay();

      display.setTextSize(1);
      display.setCursor(0, 0);
      display.print("Temp: ");
      display.print(data.temperature);
      display.cp437(true);
      display.write(167);
      display.print("C");

      display.setCursor(0, 15);
      display.print("Humidity: ");
      display.print(data.humidity);
      display.print(" %");

      display.setCursor(0, 30);
      display.print("Wind: ");
      display.print(angin);
      display.print(" %");

      tampilkanCuaca(kondisi);

      display.display();
    }
  }
}
