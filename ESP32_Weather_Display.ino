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
#define WIFI_SSID "YOUR_SSID"
#define WIFI_PASSWORD "YOUR_PASSWORD"

// Пины ILI9341 дисплея
#define TFT_CS   5    // Chip Select
#define TFT_DC   17   // Data/Command
#define TFT_RST  16   // Reset
#define TFT_MOSI 23   // SPI MOSI
#define TFT_MISO 19   // SPI MISO
#define TFT_CLK  18   // SPI Clock

// OpenWeatherMap API
#define WEATHER_API_URL "http://api.openweathermap.org/data/2.5/weather"
#define WEATHER_API_KEY "YOUR_OPENWEATHER_API_KEY"
#define WEATHER_CITY "Moscow"
#define WEATHER_COUNTRY_CODE "RU"

// Интервалы обновления
#define UPDATE_INTERVAL 1800000  // 30 минут в миллисекундах
#define DISPLAY_UPDATE_INTERVAL 1000  // Обновление дисплея каждую секунду

// Часовой пояс
#define TZ_OFFSET 3  // Москва: UTC+3

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

// ===== ФУНКЦИИ =====

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n========== ESP32 Weather Display ==========");
  Serial.println("Инициализация...");
  
  // Инициализация дисплея
  initDisplay();
  displayLoading();
  
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
  
  // Обновление дисплея каждую секунду
  if (millis() - lastDisplayUpdateTime >= DISPLAY_UPDATE_INTERVAL) {
    updateDisplay();
    lastDisplayUpdateTime = millis();
  }
  
  delay(100);
}

// ===== ИНИЦИАЛИЗАЦИЯ И ПОДКЛЮЧЕНИЕ =====

void initDisplay() {
  SPI.begin(TFT_CLK, TFT_MISO, TFT_MOSI);
  tft.begin();
  tft.setRotation(0);  // Портретная ориентация
  tft.fillScreen(COLOR_BLACK);
  
  Serial.println("✓ Дисплей инициализирован");
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

void updateDisplay() {
  if (!weatherFetched) {
    return;
  }
  
  String timeStr = getCurrentTimeString();
  uint32_t timeUntilUpdate = getTimeUntilNextUpdate();
  
  // Только обновляем нижнюю секцию (отсчет)
  tft.fillRect(0, 260, 240, 60, COLOR_BLACK);
  drawCountdownSection(timeUntilUpdate);
  
  // Обновляем время в верхней секции
  tft.fillRect(100, 35, 130, 25, COLOR_BLACK);
  tft.setTextColor(COLOR_YELLOW);
  tft.setTextSize(3);
  tft.setCursor(70, 35);
  tft.println(timeStr);
}

void displayLoading() {
  tft.fillScreen(COLOR_BLACK);
  tft.setTextColor(COLOR_WHITE);
  tft.setTextSize(2);
  tft.setCursor(70, 150);
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
  
  // ===== ВЕРХНЯЯ СЕКЦИЯ - ВРЕМЯ (0-60 пикселей) =====
  tft.drawRect(0, 0, 240, 60, COLOR_CYAN);
  tft.setTextColor(COLOR_CYAN);
  tft.setTextSize(2);
  tft.setCursor(20, 15);
  tft.println("TIME:");
  
  String timeStr = getCurrentTimeString();
  tft.setTextColor(COLOR_YELLOW);
  tft.setTextSize(3);
  tft.setCursor(70, 35);
  tft.println(timeStr);
  
  // ===== СРЕДНЯЯ СЕКЦИЯ - ПОГОДА (70-260 пикселей) =====
  tft.drawRect(0, 70, 240, 180, COLOR_GREEN);
  
  // Город
  tft.setTextColor(COLOR_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 85);
  tft.print("City: ");
  tft.println(currentWeather.city);
  
  // Температура (большой текст)
  tft.setTextColor(COLOR_ORANGE);
  tft.setTextSize(4);
  tft.setCursor(10, 115);
  tft.print((int)currentWeather.temperature);
  tft.println("C");
  
  // Ощущаемая температура
  tft.setTextColor(COLOR_WHITE);
  tft.setTextSize(1);
  tft.setCursor(10, 155);
  tft.print("Feels like: ");
  tft.print((int)currentWeather.feelsLike);
  tft.println("C");
  
  // Описание
  tft.setCursor(10, 170);
  tft.print("Weather: ");
  tft.println(currentWeather.description);
  
  // Влажность
  tft.setCursor(10, 185);
  tft.print("Humidity: ");
  tft.print(currentWeather.humidity);
  tft.println("%");
  
  // Давление
  tft.setCursor(10, 200);
  tft.print("Pressure: ");
  tft.print(currentWeather.pressure);
  tft.println(" hPa");
  
  // Иконка погоды (символ)
  drawWeatherIcon(currentWeather.icon, 160, 120);
  
  // ===== НИЖНЯЯ СЕКЦИЯ - ОТСЧЕТ (260-320 пикселей) =====
  uint32_t timeUntilUpdate = getTimeUntilNextUpdate();
  drawCountdownSection(timeUntilUpdate);
}

void drawWeatherIcon(const String &icon, int x, int y) {
  tft.setTextColor(COLOR_BLUE);
  tft.setTextSize(3);
  tft.setCursor(x - 30, y);
  
  if (icon.indexOf("01d") >= 0) {
    tft.println("SUN");
  } else if (icon.indexOf("01n") >= 0) {
    tft.println("MOON");
  } else if (icon.indexOf("02") >= 0 || icon.indexOf("03") >= 0 || icon.indexOf("04") >= 0) {
    tft.println("CLOUD");
  } else if (icon.indexOf("09") >= 0 || icon.indexOf("10") >= 0) {
    tft.println("RAIN");
  } else if (icon.indexOf("11") >= 0) {
    tft.println("STORM");
  } else if (icon.indexOf("13") >= 0) {
    tft.println("SNOW");
  } else if (icon.indexOf("50") >= 0) {
    tft.println("FOG");
  }
}

void drawCountdownSection(uint32_t remainingMS) {
  tft.drawRect(0, 260, 240, 60, COLOR_MAGENTA);
  
  tft.setTextColor(COLOR_WHITE);
  tft.setTextSize(1);
  tft.setCursor(10, 270);
  tft.println("Next update in:");
  
  // Переводим миллисекунды в минуты и секунды
  uint32_t seconds = remainingMS / 1000;
  uint32_t minutes = seconds / 60;
  seconds = seconds % 60;
  
  // Выводим время
  tft.setTextColor(COLOR_YELLOW);
  tft.setTextSize(3);
  tft.setCursor(30, 290);
  
  if (minutes < 10) tft.print("0");
  tft.print(minutes);
  tft.print(":");
  if (seconds < 10) tft.print("0");
  tft.println(seconds);
}

// На случай если нужно сделать полное обновление дисплея
void fullDisplayUpdate() {
  if (weatherFetched) {
    displayWeather();
  }
}
