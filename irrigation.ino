// ═══════════════════════════════════════════════════════════════
//  IRRIGATION CONTROLLER
//  ESP32-WROOM + 8-kanálové relé (Active HIGH)
//  v1.0.0
//
//  Potřebné knihovny (Arduino Library Manager):
//    - ArduinoJson  (Benoit Blanchon)
//
//  Board: "ESP32 Dev Module"  —  Tools → Board → esp32 → ESP32 Dev Module
//  Upload Speed: 115200 nebo 460800
//  Serial Monitor: 115200 baud
// ═══════════════════════════════════════════════════════════════

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <time.h>

#include "config.h"
#include "storage.h"
#include "zones.h"
#include "scheduler.h"
#include "weather.h"
#include "webui.h"

// ── Stav sítě ───────────────────────────────────────────────────
static bool   wifiConnected          = false;  // připojeni k domácí WiFi (STA)
static bool   apActive               = false;  // vlastní AP je aktivní
static String wifiIP                 = "--";
static bool   ntpSynced              = false;
       bool   isWeatherRefreshNeeded = false;  // čteno z webui.cpp (extern)

// ── Časovače millis() ────────────────────────────────────────────
static unsigned long lastSchedulerTick  = 0;   // každou minutu
static unsigned long lastWeatherUpdate  = 0;   // každých WEATHER_UPDATE_MIN minut
static unsigned long lastStatusPrint    = 0;   // každých 60 s do Serial
static unsigned long lastWifiCheck      = 0;   // každých 30 s (STA) / 5 min (AP)

// ── Extern API pro webui.cpp ─────────────────────────────────────
String getSystemIP(void) {
  if (wifiConnected) return wifiIP;
  if (apActive)      return "AP: 192.168.4.1";
  return "Nepřipojeno";
}
String getWiFiSSID(void) {
  if (wifiConnected) return WiFi.SSID();
  if (apActive)      return String("AP: ") + WIFI_AP_SSID;
  return "Nepřipojeno";
}
void triggerWeatherUpdate(void) { isWeatherRefreshNeeded = true; }

// ═══════════════════════════════════════════════════════════════
//  NTP sync
// ═══════════════════════════════════════════════════════════════
static void syncNTP() {
  if (!wifiConnected) return;
  SystemSettings ss = Storage_GetSystem();
  configTzTime(NTP_TIMEZONE, ss.ntpServer);
  Serial.printf("[NTP] Synchronizuji s %s...\n", ss.ntpServer);

  struct tm ti;
  int retries = 0;
  while (!getLocalTime(&ti, 1000) && retries++ < 20) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();

  if (retries < 20) {
    ntpSynced = true;
    char buf[32];
    strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M:%S", &ti);
    Serial.printf("[NTP] ✓ Čas nastaven: %s\n", buf);
  } else {
    Serial.println("[NTP] ⚠️  Nepodařilo se synchronizovat čas");
  }
}

// ═══════════════════════════════════════════════════════════════
//  WiFi AP — záložní přístupový bod
// ═══════════════════════════════════════════════════════════════
static void startAP() {
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
  apActive      = true;
  wifiConnected = false;
  wifiIP        = "192.168.4.1";
  Serial.println("[WiFi] ── AP mód spuštěn ──────────────────────────");
  Serial.printf( "[WiFi]   SSID:  %s\n", WIFI_AP_SSID);
  Serial.printf( "[WiFi]   Heslo: %s\n", WIFI_AP_PASSWORD);
  Serial.println("[WiFi]   Admin: http://192.168.4.1");
  Serial.println("[WiFi]   (zkouším domácí WiFi každých 5 minut)");
  Serial.println("[WiFi] ─────────────────────────────────────────────");
}

// ═══════════════════════════════════════════════════════════════
//  WiFi STA — připojení k domácí síti (blokující max 15 s)
//  Vrací true pokud se podařilo připojit
// ═══════════════════════════════════════════════════════════════
static bool wifiConnectSTA() {
  Serial.printf("[WiFi] Připojuji k '%s'...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(WIFI_HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries++ < 30) {  // max 15 s
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    wifiIP        = WiFi.localIP().toString();
    wifiConnected = true;
    apActive      = false;
    Serial.printf("[WiFi] ✓ Připojeno! IP: %s\n", wifiIP.c_str());
    Serial.printf("[WiFi]   Admin: http://%s.local  nebo  http://%s\n",
                  WIFI_HOSTNAME, wifiIP.c_str());
    return true;
  }
  return false;
}

// ═══════════════════════════════════════════════════════════════
//  WiFi init — STA, při neúspěchu spustí AP
// ═══════════════════════════════════════════════════════════════
static void wifiInit() {
  if (!wifiConnectSTA()) {
    Serial.println("[WiFi] ⚠️  Domácí WiFi nedostupná — spouštím záložní AP");
    startAP();
  }
}

// ═══════════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println();
  Serial.println("╔══════════════════════════════════════════╗");
  Serial.println("║  IRRIGATION CONTROLLER  v" FW_VERSION "          ║");
  Serial.println("║  ESP32-WROOM + 8-kanálové relé           ║");
  Serial.println("╚══════════════════════════════════════════╝");
  Serial.println();

  // GPIO — relé, vše OFF
  Zones_Init();
  Serial.println("[INIT] GPIO výstupy OK — všechna relé OFF");

  // Flash storage (NVS)
  Storage_Init();
  Serial.println("[INIT] NVS storage OK");

  // Weather init
  Weather_Init();

  // WiFi — STA nebo záložní AP
  wifiInit();

  // mDNS (funguje jen v STA módu)
  if (wifiConnected) {
    if (MDNS.begin(WIFI_HOSTNAME)) {
      MDNS.addService("http", "tcp", 80);
      Serial.printf("[mDNS] Dostupný jako: http://%s.local\n", WIFI_HOSTNAME);
    }
  }

  // NTP (jen v STA módu — AP nemá přístup k internetu)
  if (wifiConnected) {
    syncNTP();
  }

  // Web server
  WebUI_Init();

  // První weather update (odloženo — čekáme na síť)
  lastWeatherUpdate  = millis() - (unsigned long)(WEATHER_UPDATE_MIN - 1) * 60000UL;
  lastSchedulerTick  = millis();
  lastWifiCheck      = millis();

  Serial.println("[INIT] ✓ Inicializace dokončena — vstupuji do smyčky");
  Serial.println();
}

// ═══════════════════════════════════════════════════════════════
//  LOOP
// ═══════════════════════════════════════════════════════════════
void loop() {
  unsigned long now = millis();

  // ── Web server ──────────────────────────────────────────────
  WebUI_Handle();

  // ── Zones timeout (hlídá automatické vypnutí) ───────────────
  Zones_Tick();

  // ── WiFi správa ─────────────────────────────────────────────
  {
    // Interval: 30 s v STA módu, 5 min v AP módu (nechceme přerušovat AP klienty)
    unsigned long checkInterval = apActive ? 300000UL : 30000UL;

    if (now - lastWifiCheck >= checkInterval) {
      lastWifiCheck = now;

      if (apActive) {
        // Jsme v AP módu — zkus se tiše připojit k domácí WiFi
        Serial.println("[WiFi] AP mód: zkouším domácí WiFi...");
        if (wifiConnectSTA()) {
          // Přepnuto zpět do STA — spusť mDNS a NTP
          apActive = false;
          if (MDNS.begin(WIFI_HOSTNAME)) {
            MDNS.addService("http", "tcp", 80);
            Serial.printf("[mDNS] Dostupný jako: http://%s.local\n", WIFI_HOSTNAME);
          }
          if (!ntpSynced) syncNTP();
        } else {
          // Stále nedostupná — zpátky do AP
          Serial.println("[WiFi] Domácí WiFi stále nedostupná — zůstávám v AP módu");
          startAP();
        }
      } else {
        // STA mód — zkontroluj spojení
        if (WiFi.status() != WL_CONNECTED) {
          wifiConnected = false;
          Serial.println("[WiFi] Spojení ztraceno — pokouším se znovu připojit...");
          if (!wifiConnectSTA()) {
            Serial.println("[WiFi] Nepodařilo se — přepínám do AP módu");
            startAP();
          } else if (!ntpSynced) {
            syncNTP();
          }
        } else {
          wifiIP = WiFi.localIP().toString();  // obnov IP (DHCP lease)
        }
      }
    }
  }

  // ── Scheduler tick (každých 15 s — zajistí spuštění v každé minutě) ──
  if (now - lastSchedulerTick >= 15000UL) {
    lastSchedulerTick = now;
    Scheduler_Tick();
  }

  // ── Weather update (každých WEATHER_UPDATE_MIN minut, jen STA) ─
  bool weatherDue = (now - lastWeatherUpdate >= (unsigned long)WEATHER_UPDATE_MIN * 60000UL);
  if ((weatherDue || isWeatherRefreshNeeded) && wifiConnected && !apActive) {
    Serial.println("[LOOP] Aktualizuji data počasí...");
    WeatherSettings ws = Storage_GetWeather();
    Weather_Update(ws.latitude, ws.longitude);
    lastWeatherUpdate      = now;
    isWeatherRefreshNeeded = false;
  } else if (isWeatherRefreshNeeded && apActive) {
    isWeatherRefreshNeeded = false;  // nelze — nejsme online
  }

  // ── Denní NTP resync — každých 24 h (jen STA) ────────────────
  static unsigned long lastNtpResync = 0;
  if (now - lastNtpResync >= 24UL * 3600UL * 1000UL && wifiConnected && !apActive) {
    lastNtpResync = now;
    syncNTP();
  }

  // ── Periodický výpis stavu do Serial monitoru (každých 60 s) ─
  if (now - lastStatusPrint >= 60000UL) {
    lastStatusPrint = now;
    struct tm ti;
    bool anyRunning = Zones_AnyRunning();
    int  runCount   = Zones_RunningCount();

    String stavStr = anyRunning
      ? (String(runCount) + " zón" + (runCount == 1 ? "a" : "y") + " běží")
      : "Klidový stav";

    if (getLocalTime(&ti)) {
      char buf[20];
      strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M", &ti);
      Serial.printf("[STATUS] %s | %s:%s | %s\n",
        buf,
        apActive ? "AP" : "WiFi",
        wifiConnected || apActive ? wifiIP.c_str() : "off",
        stavStr.c_str());
    } else {
      Serial.printf("[STATUS] Čas nesync | %s:%s | %s\n",
        apActive ? "AP" : "WiFi",
        wifiConnected || apActive ? wifiIP.c_str() : "off",
        stavStr.c_str());
    }
    if (Queue_Count() > 0)
      Serial.printf("[STATUS] Fronta: %d zón čeká\n", Queue_Count());
    Serial.printf("[STATUS] Počasí: %s\n", Weather_StatusString().c_str());
    Serial.printf("[STATUS] Příští zálivka: %s\n", Scheduler_NextRunString().c_str());
  }

  delay(5);   // uvolni CPU, aby WebServer zvládal paralelní requesty
}
