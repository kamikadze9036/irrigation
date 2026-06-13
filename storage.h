#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "config.h"

// ── Datové struktury ────────────────────────────────────────────

struct ZoneProgram {
  bool     enabled;
  uint8_t  days;          // bit0=Po, bit1=Út, bit2=St, bit3=Čt, bit4=Pá, bit5=So, bit6=Ne
  uint8_t  startHour;
  uint8_t  startMinute;
  uint16_t durationMin;
};

struct ZoneConfig {
  char        name[32];
  bool        enabled;
  ZoneProgram programs[MAX_PROGRAMS_PER_ZONE];
};

struct WeatherSettings {
  bool  rainSkipEnabled;
  bool  forecastSkipEnabled;
  float pastRainThreshMm;
  float forecastRainThreshMm;
  float latitude;
  float longitude;
};

struct SystemSettings {
  bool    masterValveEnabled;
  uint8_t masterPreDelay;
  uint8_t masterPostDelay;
  char    ntpServer[64];
};

// ── API ─────────────────────────────────────────────────────────
void            Storage_Init(void);
ZoneConfig      Storage_GetZone(uint8_t zone);   // 1–6
void            Storage_SetZone(uint8_t zone, const ZoneConfig &cfg);
WeatherSettings Storage_GetWeather(void);
void            Storage_SetWeather(const WeatherSettings &ws);
SystemSettings  Storage_GetSystem(void);
void            Storage_SetSystem(const SystemSettings &ss);

// ── Pauza zálivky ────────────────────────────────────────────────
// pauseUntil = unix timestamp konce pauzy; 0 = pauza není aktivní
time_t          Storage_GetPauseUntil(void);
void            Storage_SetPauseUntil(time_t t);  // 0 = zrušit pauzu

// ── WiFi přihlašovací údaje (uložené přes web UI) ─────────────────
struct WiFiCredentials {
  char ssid[64];
  char password[64];
};
WiFiCredentials Storage_GetWiFiCreds(void);
void            Storage_SetWiFiCreds(const WiFiCredentials &creds);
void            Storage_ClearWiFiCreds(void);
bool            Storage_HasWiFiCreds(void);  // true pokud jsou uloženy
