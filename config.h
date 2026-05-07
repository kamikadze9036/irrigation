#pragma once

// ═══════════════════════════════════════════════════════════════
//  IRRIGATION CONTROLLER — config.h
//  ESP32-WROOM + 8-kanálové relé (Active HIGH)
// ═══════════════════════════════════════════════════════════════

// ── WiFi — připojení k domácí síti ──────────────────────────────
#define WIFI_SSID        "pfkamikadze"
#define WIFI_PASSWORD    "ledenunor12"
#define WIFI_HOSTNAME    "irrigation"      // → http://irrigation.local

// ── WiFi AP — záložní přístupový bod (když domácí WiFi není dostupná) ──
// Telefon se připojí na tuto síť a otevře http://192.168.4.1
#define WIFI_AP_SSID     "Zavlaha-AP"
#define WIFI_AP_PASSWORD "zavlaha123"      // min. 8 znaků; "" = otevřená síť

// ── GPIO — 8 relé, ACTIVE HIGH ──────────────────────────────────
// Uprav piny dle svého konkrétního relay board
// Relay 1–6 = zóny, Relay 7 = master ventil/čerpadlo, Relay 8 = rezerva
static const int RELAY_PINS[8] = {13, 12, 14, 27, 26, 25, 33, 32};

#define ZONE_COUNT        6     // zóny 1–6 → RELAY_PINS[0..5]
#define MASTER_VALVE_IDX  6     // master ventil → RELAY_PINS[6]

// ── NTP / časová zóna ────────────────────────────────────────────
#define NTP_SERVER       "pool.ntp.org"
#define NTP_TIMEZONE     "CET-1CEST,M3.5.0,M10.5.0/3"

// ── Počasí (Open-Meteo, bez API klíče) ─────────────────────────
#define WEATHER_LAT      49.7469f
#define WEATHER_LON      13.3731f
#define WEATHER_UPDATE_MIN  60

// ── Scheduler ───────────────────────────────────────────────────
#define MAX_PROGRAMS_PER_ZONE   3
#define MASTER_VALVE_PRE_S      2
#define MASTER_VALVE_POST_S     3

// ── Log (in-memory kruhový buffer) ──────────────────────────────
#define LOG_MAX_ENTRIES  40

#define FW_VERSION  "1.0.0"
