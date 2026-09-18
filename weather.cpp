#include "weather.h"
#include "storage.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

static WeatherData _data = {};

// Data čte WebServer task (core 0) a zapisuje hlavní smyčka (core 1) —
// krátký zámek zabrání roztrhané kopii struktury.
static SemaphoreHandle_t weatherMutex = nullptr;
struct WeatherLock {
  WeatherLock()  { if (weatherMutex) xSemaphoreTake(weatherMutex, portMAX_DELAY); }
  ~WeatherLock() { if (weatherMutex) xSemaphoreGive(weatherMutex); }
};

static void setStatus(const char *msg) {
  WeatherLock lock;
  strlcpy(_data.statusMsg, msg, sizeof(_data.statusMsg));
}

void Weather_Init(void) {
  if (!weatherMutex) weatherMutex = xSemaphoreCreateMutex();
  WeatherLock lock;
  _data.dataValid = false;
  strlcpy(_data.statusMsg, "Čekám na první aktualizaci...", sizeof(_data.statusMsg));
}

void Weather_Update(float lat, float lon) {
  if (WiFi.status() != WL_CONNECTED) {
    setStatus("WiFi odpojeno");
    return;
  }
  // Bez synchronizovaného času by "posledních 24 h" počítalo od roku 1970
  // a výsledkem by byly validně vypadající nuly.
  time_t now = time(nullptr);
  if (now < 1000000000L) {
    setStatus("Čekám na synchronizaci času");
    return;
  }

  char url[384];
  snprintf(url, sizeof(url),
    "http://api.open-meteo.com/v1/forecast"
    "?latitude=%.4f&longitude=%.4f"
    "&hourly=precipitation,temperature_2m"
    "&past_days=1&forecast_days=2"
    "&timezone=Europe%%2FPrague"
    "&timeformat=unixtime",
    lat, lon);

  Serial.printf("[WTH] GET %s\n", url);
  HTTPClient http;
  http.begin(url);
  http.setTimeout(12000);
  http.setUserAgent("ESP32-Irrigation/1.0");

  int code = http.GET();
  Serial.printf("[WTH] HTTP %d\n", code);

  if (code != 200) {
    char msg[48];
    snprintf(msg, sizeof(msg), "HTTP chyba: %d", code);
    setStatus(msg);
    http.end(); return;
  }

  String body = http.getString();
  http.end();
  Serial.printf("[WTH] Přijato %d B\n", body.length());

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    char msg[64];
    snprintf(msg, sizeof(msg), "JSON chyba: %s", err.c_str());
    setStatus(msg);
    Serial.printf("[WTH] JSON error: %s\n", err.c_str());
    return;
  }

  if (!doc["hourly"]["time"].is<JsonArray>()) {
    setStatus("Neočekávaná struktura dat");
    return;
  }

  JsonArray times  = doc["hourly"]["time"];
  JsonArray precip = doc["hourly"]["precipitation"];
  JsonArray temps  = doc["hourly"]["temperature_2m"];

  float p24 = 0, n24 = 0, curTemp = 0;
  float tDiff = 1e9f;

  for (int i = 0; i < (int)times.size(); i++) {
    time_t t = (time_t)times[i].as<long>();
    float  p = precip[i].is<float>() ? precip[i].as<float>() : 0;
    float  T = temps[i].is<float>()  ? temps[i].as<float>()  : 0;
    if (t >= now - 86400 && t <= now)       p24  += p;
    if (t >  now && t <= now + 86400)       n24  += p;
    float d = fabsf((float)(t - now));
    if (d < tDiff) { tDiff = d; curTemp = T; }
  }

  {
    WeatherLock lock;
    _data.past24hRainMm  = p24;
    _data.next24hRainMm  = n24;
    _data.currentTempC   = curTemp;
    _data.dataValid      = true;
    _data.lastUpdate     = now;
    snprintf(_data.statusMsg, sizeof(_data.statusMsg),
      "OK: %.1f mm (24h), předpověď %.1f mm, %.1f°C", p24, n24, curTemp);
  }

  Serial.printf("[WTH] ✓ %.1f mm | předpověď %.1f mm | %.1f°C\n", p24, n24, curTemp);
}

WeatherData Weather_GetData(void) { WeatherLock lock; return _data; }

bool Weather_ShouldSkip(char *reason, size_t reasonLen) {
  WeatherData d = Weather_GetData();
  if (!d.dataValid) return false;
  WeatherSettings ws = Storage_GetWeather();
  if (ws.rainSkipEnabled && d.past24hRainMm >= ws.pastRainThreshMm) {
    if (reason) snprintf(reason, reasonLen, "Přeskočeno — pršelo %.1f mm", d.past24hRainMm);
    return true;
  }
  if (ws.forecastSkipEnabled && d.next24hRainMm >= ws.forecastRainThreshMm) {
    if (reason) snprintf(reason, reasonLen, "Přeskočeno — předpověď %.1f mm", d.next24hRainMm);
    return true;
  }
  return false;
}

String Weather_StatusString(void) {
  WeatherLock lock;
  return String(_data.statusMsg);
}
