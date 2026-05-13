#include "storage.h"

static Preferences prefs;

static void openNs(const char *ns, bool rw) { prefs.begin(ns, !rw); }

void Storage_Init(void) {
  openNs("sys", false);
  uint8_t magic = prefs.getUChar("magic", 0);
  prefs.end();
  if (magic != 0xAB) {
    Serial.println("[STOR] První spuštění — nahrávám výchozí nastavení");

    const char *names[6] = {"Okruh 1","Okruh 2","Okruh 3","Okruh 4","Okruh 5","Okruh 6"};
    for (uint8_t z = 1; z <= 6; z++) {
      ZoneConfig cfg = {};
      strlcpy(cfg.name, names[z-1], 32);
      cfg.enabled = true;
      for (int p = 0; p < MAX_PROGRAMS_PER_ZONE; p++) {
        cfg.programs[p] = {false, 0, 6, 0, 10};
      }
      Storage_SetZone(z, cfg);
    }

    WeatherSettings ws = {true, true, 5.0f, 3.0f, WEATHER_LAT, WEATHER_LON};
    Storage_SetWeather(ws);

    SystemSettings ss = {};
    ss.masterValveEnabled = true;
    ss.masterPreDelay  = MASTER_VALVE_PRE_S;
    ss.masterPostDelay = MASTER_VALVE_POST_S;
    strlcpy(ss.ntpServer, NTP_SERVER, 64);
    Storage_SetSystem(ss);

    openNs("sys", true);
    prefs.putUChar("magic", 0xAB);
    prefs.end();
  } else {
    Serial.println("[STOR] Flash OK");
  }
}

ZoneConfig Storage_GetZone(uint8_t z) {
  ZoneConfig cfg = {};
  if (z < 1 || z > 6) return cfg;
  char ns[8]; snprintf(ns, 8, "zone%d", z);
  openNs(ns, false);
  prefs.getBytes("cfg", &cfg, sizeof(cfg));
  prefs.end();
  return cfg;
}

void Storage_SetZone(uint8_t z, const ZoneConfig &cfg) {
  if (z < 1 || z > 6) return;
  char ns[8]; snprintf(ns, 8, "zone%d", z);
  openNs(ns, true);
  prefs.putBytes("cfg", &cfg, sizeof(cfg));
  prefs.end();
}

WeatherSettings Storage_GetWeather(void) {
  WeatherSettings ws = {};
  openNs("weather", false);
  prefs.getBytes("cfg", &ws, sizeof(ws));
  prefs.end();
  if (ws.latitude == 0) { ws.latitude = WEATHER_LAT; ws.longitude = WEATHER_LON; }
  if (ws.pastRainThreshMm == 0) ws.pastRainThreshMm = 5.0f;
  if (ws.forecastRainThreshMm == 0) ws.forecastRainThreshMm = 3.0f;
  return ws;
}

void Storage_SetWeather(const WeatherSettings &ws) {
  openNs("weather", true);
  prefs.putBytes("cfg", &ws, sizeof(ws));
  prefs.end();
}

SystemSettings Storage_GetSystem(void) {
  SystemSettings ss = {};
  openNs("system", false);
  prefs.getBytes("cfg", &ss, sizeof(ss));
  prefs.end();
  if (ss.ntpServer[0] == '\0') strlcpy(ss.ntpServer, NTP_SERVER, 64);
  if (ss.masterPreDelay == 0)  ss.masterPreDelay  = MASTER_VALVE_PRE_S;
  if (ss.masterPostDelay == 0) ss.masterPostDelay = MASTER_VALVE_POST_S;
  return ss;
}

void Storage_SetSystem(const SystemSettings &ss) {
  openNs("system", true);
  prefs.putBytes("cfg", &ss, sizeof(ss));
  prefs.end();
}

// ── Pauza zálivky (uložena jako samostatný klíč — neovlivní ostatní nastavení) ──
time_t Storage_GetPauseUntil(void) {
  openNs("pause", false);
  long val = prefs.getLong("until", 0);
  prefs.end();
  return (time_t)val;
}

void Storage_SetPauseUntil(time_t t) {
  openNs("pause", true);
  prefs.putLong("until", (long)t);
  prefs.end();
}
