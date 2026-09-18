#include "storage.h"

static Preferences prefs;

// NVS (Preferences) je sdílené mezi Core 0 (WebServer task) a Core 1
// (hlavní loop / WiFi state machine). Bez zámku může souběžný přístup
// z obou jader poškodit interní handle Preferences knihovny.
// Všechny přístupy proto procházejí přes tento mutex.
static SemaphoreHandle_t storageMutex = nullptr;

static void openNs(const char *ns, bool rw) { prefs.begin(ns, !rw); }

// RAII zámek — drží mutex po dobu života objektu.
// Rekurzivní mutex: Storage_Init() volá Storage_SetZone/Weather/System
// zatímco už zámek drží, takže musí jít zamknout opakovaně ze stejného tasku.
struct StorageLock {
  StorageLock()  { if (storageMutex) xSemaphoreTakeRecursive(storageMutex, portMAX_DELAY); }
  ~StorageLock() { if (storageMutex) xSemaphoreGiveRecursive(storageMutex); }
};

// Struktury se ukládají jako raw bytes. Pokud se v nové verzi firmware změní
// jejich layout, uložená délka nesedí — v tom případě se použijí výchozí
// hodnoty místo načtení smetí. (Délka záznamu tak funguje jako verze.)
// Díky tomu je zároveň možné uložit legitimní nulu (prodleva 0 s, práh 0 mm) —
// výchozí hodnoty se doplňují jen když záznam chybí, ne když je pole nulové.
static bool readBlob(const char *ns, void *out, size_t len) {
  openNs(ns, false);
  bool ok = prefs.getBytesLength("cfg") == len && prefs.getBytes("cfg", out, len) == len;
  prefs.end();
  return ok;
}

static void writeBlob(const char *ns, const void *data, size_t len) {
  openNs(ns, true);
  prefs.putBytes("cfg", data, len);
  prefs.end();
}

// ── Výchozí hodnoty ──────────────────────────────────────────────
static ZoneConfig defaultZone(uint8_t z) {
  ZoneConfig cfg = {};
  snprintf(cfg.name, sizeof(cfg.name), "Okruh %d", z);
  cfg.enabled = true;
  for (int p = 0; p < MAX_PROGRAMS_PER_ZONE; p++) cfg.programs[p] = {false, 0, 6, 0, 10};
  return cfg;
}

static WeatherSettings defaultWeather(void) {
  return WeatherSettings{true, true, 5.0f, 3.0f, WEATHER_LAT, WEATHER_LON};
}

static SystemSettings defaultSystem(void) {
  SystemSettings ss = {};
  ss.masterValveEnabled = true;
  ss.masterPreDelay  = MASTER_VALVE_PRE_S;
  ss.masterPostDelay = MASTER_VALVE_POST_S;
  strlcpy(ss.ntpServer, NTP_SERVER, sizeof(ss.ntpServer));
  return ss;
}

void Storage_Init(void) {
  if (!storageMutex) storageMutex = xSemaphoreCreateRecursiveMutex();
  StorageLock lock;

  openNs("sys", false);
  uint8_t magic = prefs.getUChar("magic", 0);
  prefs.end();
  if (magic != 0xAB) {
    Serial.println("[STOR] První spuštění — nahrávám výchozí nastavení");
    for (uint8_t z = 1; z <= ZONE_COUNT; z++) Storage_SetZone(z, defaultZone(z));
    Storage_SetWeather(defaultWeather());
    Storage_SetSystem(defaultSystem());
    openNs("sys", true);
    prefs.putUChar("magic", 0xAB);
    prefs.end();
  } else {
    Serial.println("[STOR] Flash OK");
  }
}

static void zoneNs(uint8_t z, char *buf, size_t len) { snprintf(buf, len, "zone%d", z); }

ZoneConfig Storage_GetZone(uint8_t z) {
  if (z < 1 || z > ZONE_COUNT) return ZoneConfig{};
  StorageLock lock;
  char ns[8]; zoneNs(z, ns, sizeof(ns));
  ZoneConfig cfg;
  if (!readBlob(ns, &cfg, sizeof(cfg))) cfg = defaultZone(z);
  cfg.name[sizeof(cfg.name) - 1] = '\0';
  return cfg;
}

void Storage_SetZone(uint8_t z, const ZoneConfig &cfg) {
  if (z < 1 || z > ZONE_COUNT) return;
  StorageLock lock;
  char ns[8]; zoneNs(z, ns, sizeof(ns));
  writeBlob(ns, &cfg, sizeof(cfg));
}

WeatherSettings Storage_GetWeather(void) {
  StorageLock lock;
  WeatherSettings ws;
  if (!readBlob("weather", &ws, sizeof(ws))) ws = defaultWeather();
  return ws;
}

void Storage_SetWeather(const WeatherSettings &ws) {
  StorageLock lock;
  writeBlob("weather", &ws, sizeof(ws));
}

SystemSettings Storage_GetSystem(void) {
  StorageLock lock;
  SystemSettings ss;
  if (!readBlob("system", &ss, sizeof(ss))) ss = defaultSystem();
  ss.ntpServer[sizeof(ss.ntpServer) - 1] = '\0';
  if (ss.ntpServer[0] == '\0') strlcpy(ss.ntpServer, NTP_SERVER, sizeof(ss.ntpServer));
  return ss;
}

void Storage_SetSystem(const SystemSettings &ss) {
  StorageLock lock;
  writeBlob("system", &ss, sizeof(ss));
}

// ── Pauza zálivky (uložena jako samostatný klíč — neovlivní ostatní nastavení) ──
time_t Storage_GetPauseUntil(void) {
  StorageLock lock;
  openNs("pause", false);
  long val = prefs.getLong("until", 0);
  prefs.end();
  return (time_t)val;
}

void Storage_SetPauseUntil(time_t t) {
  StorageLock lock;
  openNs("pause", true);
  prefs.putLong("until", (long)t);
  prefs.end();
}

// ── WiFi přihlašovací údaje ──────────────────────────────────────
WiFiCredentials Storage_GetWiFiCreds(void) {
  StorageLock lock;
  WiFiCredentials creds;
  if (!readBlob("wificred", &creds, sizeof(creds))) creds = WiFiCredentials{};
  creds.ssid[sizeof(creds.ssid) - 1] = '\0';
  creds.password[sizeof(creds.password) - 1] = '\0';
  return creds;
}

void Storage_SetWiFiCreds(const WiFiCredentials &creds) {
  StorageLock lock;
  writeBlob("wificred", &creds, sizeof(creds));
}

void Storage_ClearWiFiCreds(void) {
  StorageLock lock;
  openNs("wificred", true);
  prefs.clear();
  prefs.end();
}

bool Storage_HasWiFiCreds(void) {
  // Storage_GetWiFiCreds() si zámek bere samo (rekurzivní mutex to umožní)
  WiFiCredentials c = Storage_GetWiFiCreds();
  return c.ssid[0] != '\0';
}
