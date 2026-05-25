// ===== ESP32 Weather Display =====
// Метеостанция на базе ESP32 + ILI9341 (2.8" 240x320)
// Отображает: время, погоду, индикатор обновления (30 мин)

// ===== Подключаемые библиотеки =====
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_ILI9341.h>
#include <Adafruit_GFX.h>
#include <SPI.h>
#include <time.h>

// ===== КОНФИГУРАЦИЯ =====

// WiFi
#define WIFI_SSID "Panasonic"
#define WIFI_PASSWORD "22022020"

// Пины ILI9341 дисплея
#define TFT_CS   5    // Chip Select
#define TFT_DC   17   // Data/Command
#define TFT_RST  16   // Reset
#define TFT_MOSI 23   // SPI MOSI
#define TFT_MISO 19   // SPI MISO
#define TFT_CLK  18   // SPI Clock

// OpenWeatherMap API
#define WEATHER_API_URL "http://api.openweathermap.org/data/2.5/weather"
#define WEATHER_API_KEY "fbfa1772be8f824a9300ce5aa326db37"
#define WEATHER_CITY "Tashkent"
#define WEATHER_COUNTRY_CODE "UZ"

// Интервалы обновления
#define UPDATE_INTERVAL 1800000  // 30 минут в миллисекундах
#define DISPLAY_UPDATE_INTERVAL 1000  // Обновление дисплея каждую секунду

// Часовой пояс
#define TZ_OFFSET 5  // Tashkent: UTC+5

// Размеры дисплея ПОСЛЕ поворота на 90 градусов с rotation(1)
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

// Цвета (RGB565 16-bit)
#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_BLUE    0x001F
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_YELLOW  0xFFE0
#define COLOR_CYAN    0x07FF
#define COLOR_MAGENTA 0xF81F
#define COLOR_ORANGE  0xFD20

// Параметры прогресс-бара
#define PROGRESSBAR_X 20
#define PROGRESSBAR_Y 210
#define PROGRESSBAR_WIDTH 280
#define PROGRESSBAR_HEIGHT 20
#define PROGRESSBAR_FILL_COLOR COLOR_CYAN
#define PROGRESSBAR_BORDER_COLOR COLOR_MAGENTA
#define PROGRESSBAR_BG_COLOR COLOR_BLACK

// ===== СТРУКТУРЫ =====

struct WeatherData {
  String city;
  float temperature;
  float feelsLike;
  int humidity;
  int pressure;
  String description;
  String icon;
};

// ===== ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ =====

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
WeatherData currentWeather;
uint32_t lastWeatherUpdateTime = 0;
uint32_t lastDisplayUpdateTime = 0;
bool weatherFetched = false;

// ===== ПРОТОТИПЫ ФУНКЦИЙ =====
void drawProgressBar(uint32_t remainingMS);
void displayWeather();
void displayLoading();
void displayError(const String &error);
void updateDisplay();
String getCurrentTimeString();
uint32_t getTimeUntilNextUpdate();

// ===== ФУНКЦИИ =====

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n========== ESP32 Weather Display ==========");
  Serial.println("Инициализация...");
  
  // Инициализация дисплея
  initDisplay();
  displayLoading();
  
  delay(2000);
  
  // Подключение к WiFi
  connectToWiFi();
  
  // Синхронизация времени с NTP
  syncTime();
  
  // Получение первоначальных данных о погоде
  fetchWeather();
  
  Serial.println("✓ Инициализация завершена!");
}

void loop() {
  // Обновление погоды каждые 30 минут
  if (millis() - lastWeatherUpdateTime >= UPDATE_INTERVAL) {
    fetchWeather();
  }
  // Обновление дисплея каждую секунду (ТОЛЬКО если данные получены)
  if (weatherFetched && (millis() - lastDisplayUpdateTime >= DISPLAY_UPDATE_INTERVAL)) {
    updateDisplay();
    lastDisplayUpdateTime = millis();
  }
  
  delay(100);
}

// ===== ИНИЦИАЛИЗАЦИЯ И ПОДКЛЮЧЕНИЕ =====

void initDisplay() {
  SPI.begin(TFT_CLK, TFT_MISO, TFT_MOSI);
  tft.begin();

  tft.setRotation(0);  // 320x240 ориентация
  tft.fillScreen(COLOR_BLACK);
  Serial.print("✓ Дисплей инициализирован: ");
  Serial.print(tft.width());
  Serial.print("x");
  Serial.println(tft.height());
}

void connectToWiFi() {
  Serial.print("Подключение к WiFi: ");
  Serial.println(WIFI_SSID);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("✓ WiFi подключен! IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("✗ Ошибка подключения к WiFi");
    displayError("WiFi не подключен");
    delay(3000);
  }
}

void syncTime() {
  const char* ntpServer = "pool.ntp.org";
  const long gmtOffset_sec = TZ_OFFSET * 3600;
  const int daylightOffset_sec = 0;
  
  Serial.print("Синхронизация времени с NTP...");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  time_t now = time(nullptr);
  int count = 0;
  
  while (now < 24 * 3600 && count < 100) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
    count++;
  }
  
  Serial.println();
  Serial.println("✓ Время синхронизировано");
}

// ===== ПОЛУЧЕНИЕ ДАННЫХ О ПОГОДЕ =====

void fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("✗ WiFi не подключен!");
    displayError("WiFi отключен");
    return;
  }
  
  Serial.println("📡 Запрос данных о погоде...");
  displayLoading();
  
  HTTPClient http;
  
  String url = String(WEATHER_API_URL);
  url += "?q=" + String(WEATHER_CITY) + "," + String(WEATHER_COUNTRY_CODE);
  url += "&appid=" + String(WEATHER_API_KEY);
  url += "&units=metric";
  
  http.begin(url);
  int httpCode = http.GET();
  
  if (httpCode != HTTP_CODE_OK) {
    Serial.print("✗ HTTP ошибка: ");
    Serial.println(httpCode);
    displayError("HTTP Error: " + String(httpCode));
    http.end();
    return;
  }
  
  String payload = http.getString();
  http.end();
  
  if (parseWeatherJSON(payload)) {
    Serial.println("✓ Данные о погоде получены");
    weatherFetched = true;
    lastWeatherUpdateTime = millis();
    lastDisplayUpdateTime = millis();
    displayWeather();
  } else {
    Serial.println("✗ Ошибка парсинга JSON");
    displayError("Ошибка парсинга данных");
  }
}

bool parseWeatherJSON(const String &json) {
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, json);
  
  if (error) {
    Serial.print("✗ JSON ошибка: ");
    Serial.println(error.c_str());
    return false;
  }
  
  currentWeather.city = doc["name"].as<String>();
  currentWeather.temperature = doc["main"]["temp"].as<float>();
  currentWeather.feelsLike = doc["main"]["feels_like"].as<float>();
  currentWeather.humidity = doc["main"]["humidity"].as<int>();
  currentWeather.pressure = doc["main"]["pressure"].as<int>();
  currentWeather.description = doc["weather"][0]["main"].as<String>();
  currentWeather.icon = doc["weather"][0]["icon"].as<String>();
  
  Serial.println("Погода в " + currentWeather.city + ": " + currentWeather.description);
  Serial.println("  Температура: " + String(currentWeather.temperature) + "°C");
  
  return true;
}

// ===== ОТОБРАЖЕНИЕ НА ДИСПЛЕЕ =====

String getCurrentTimeString() {
  time_t now = time(nullptr);
  struct tm timeinfo = *localtime(&now);
  
  char buffer[20];
  strftime(buffer, sizeof(buffer), "%H:%M:%S", &timeinfo);
  return String(buffer);
}

uint32_t getTimeUntilNextUpdate() {
  uint32_t elapsed = millis() - lastWeatherUpdateTime;
  if (elapsed >= UPDATE_INTERVAL) {
    return 0;
  }
  return UPDATE_INTERVAL - elapsed;
}

void drawProgressBar(uint32_t remainingMS) {
  // Рисуем рамку прогресс-бара
  tft.drawRect(PROGRESSBAR_X, PROGRESSBAR_Y, PROGRESSBAR_WIDTH, PROGRESSBAR_HEIGHT, PROGRESSBAR_BORDER_COLOR);
  
  // Вычисляем процент выполнения (от 0 до 100)
  uint32_t elapsedMS = UPDATE_INTERVAL - remainingMS;
  uint32_t fillWidth = (elapsedMS * (PROGRESSBAR_WIDTH - 2)) / UPDATE_INTERVAL;
  
  // Очищаем внутренность бара
  tft.fillRect(PROGRESSBAR_X + 1, PROGRESSBAR_Y + 1, PROGRESSBAR_WIDTH - 2, PROGRESSBAR_HEIGHT - 2, PROGRESSBAR_BG_COLOR);
  
  // Рисуем заполненную часть
  if (fillWidth > 0) {
    tft.fillRect(PROGRESSBAR_X + 1, PROGRESSBAR_Y + 1, fillWidth, PROGRESSBAR_HEIGHT - 2, PROGRESSBAR_FILL_COLOR);
  }
  
  // Выводим время оставшееся (опционально, можно убрать)
  uint32_t seconds = remainingMS / 1000;
  uint32_t minutes = seconds / 60;
  seconds = seconds % 60;
  
  tft.setTextColor(COLOR_WHITE);
  tft.setTextSize(1);
  tft.setCursor(PROGRESSBAR_X + 10, PROGRESSBAR_Y + 5);
  
  if (minutes < 10) tft.print("0");
  tft.print(minutes);
  tft.print(":");
  if (seconds < 10) tft.print("0");
  tft.print(seconds);
  
  // Вывод процента справа
  uint32_t percent = (elapsedMS * 100) / UPDATE_INTERVAL;
  tft.setCursor(PROGRESSBAR_X + PROGRESSBAR_WIDTH - 35, PROGRESSBAR_Y + 5);
  tft.print(percent);
  tft.print("%");
}

void updateDisplay() {
  if (!weatherFetched) {
    return;
  }
  
  String timeStr = getCurrentTimeString();
  uint32_t timeUntilUpdate = getTimeUntilNextUpdate();
  
  // ВАЖНО: Обновляем ТОЛЬКО нижнюю секцию
  // Координаты: y от 195 до 240
  tft.fillRect(0, 195, SCREEN_WIDTH, 45, PROGRESSBAR_BG_COLOR);
  drawProgressBar(timeUntilUpdate);

  // Обновляем время в верхней части
  tft.fillRect(120, 25, 200, 30, COLOR_BLACK);
  tft.setTextColor(COLOR_YELLOW);
  tft.setTextSize(3);
  tft.setCursor(120, 25);
  tft.println(timeStr);
}

void displayLoading() {
  tft.fillScreen(COLOR_BLACK);
  tft.setTextColor(COLOR_WHITE);
  tft.setTextSize(2);
  tft.setCursor(100, 110);
  tft.println("Loading...");
}

void displayError(const String &error) {
  tft.fillScreen(COLOR_BLACK);
  tft.setTextColor(COLOR_RED);
  tft.setTextSize(2);
  tft.setCursor(10, 50);
  tft.println("ERROR");
  
  tft.setTextColor(COLOR_WHITE);
  tft.setTextSize(1);
  tft.setCursor(10, 100);
  tft.println(error);
}

void displayWeather() {
  tft.fillScreen(COLOR_BLACK);
  delay(50);
  
  // ===== ВЕРХНЯЯ СЕКЦИЯ - ВРЕМЯ (y: 0-60) =====
  tft.drawRect(0, 0, SCREEN_WIDTH, 60, COLOR_CYAN);
  tft.setTextColor(COLOR_CYAN);
  tft.setTextSize(2);
  tft.setCursor(10, 15);
  tft.println("TIME:");
  
  String timeStr = getCurrentTimeString();
  tft.setTextColor(COLOR_YELLOW);
  tft.setTextSize(3);
  tft.setCursor(120, 25);
  tft.println(timeStr);
  delay(10);
  
  // ===== СРЕДНЯЯ СЕКЦИЯ - ПОГОДА (y: 70-195) =====
  // Высота ровно 125 пикселей (195 - 70)
  tft.drawRect(0, 70, SCREEN_WIDTH, 125, COLOR_GREEN);

  // Левая колонка (x: 10-160)
  tft.setTextColor(COLOR_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 85);
  tft.print("City: ");
  tft.println(currentWeather.city);
  
  // Большая температура
  tft.setTextColor(COLOR_ORANGE);
  tft.setTextSize(4);
  tft.setCursor(10, 115);
  tft.print((int)currentWeather.temperature);
  tft.println("C");
  delay(10);
  
  // Правая колонка (x: 200-310)
  tft.setTextColor(COLOR_GREEN);
  tft.setTextSize(2);
  
  tft.setCursor(200, 85);
  tft.print("Weather:");
  tft.setCursor(200, 100);
  tft.println(currentWeather.description);
  
  tft.setCursor(200, 125);
  tft.print("Humidity:");
  tft.setCursor(200, 140);
  tft.print(currentWeather.humidity);
  tft.println("%");
  
  tft.setCursor(200, 165);
  tft.print("Pressure:");
  tft.setCursor(200, 180);
  tft.print(currentWeather.pressure);
  tft.println(" hPa");
  delay(10);
  
  // ===== НИЖНЯЯ СЕКЦИЯ - ПРОГРЕСС БАР (y: 195-240) =====
  uint32_t timeUntilUpdate = getTimeUntilNextUpdate();
  drawProgressBar(timeUntilUpdate);
}
