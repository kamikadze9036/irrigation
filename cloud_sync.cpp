// ═══════════════════════════════════════════════════════════════
//  cloud_sync.cpp — vzdálený přístup přes cloud relay (Vercel)
//
//  Princip: ESP32 nemá žádný otevřený port ani veřejnou IP, takže appka na
//  Vercelu se k němu nemůže sama připojit. Místo toho ESP32 pravidelně
//  "pollne" appku (GET /api/device/poll), jestli tam čeká nějaký požadavek
//  od přihlášeného uživatele. Pokud ano, předá ho přímo handlerům v webui.cpp
//  (WebUI_Dispatch — stejný kód, jaký obslouží prohlížeč v domácí síti) a
//  výsledek pošle zpět (POST /api/device/response).
//
//  Dřív se požadavek přehrával přes HTTP na vlastní IP. To na ESP32 selhávalo
//  (connect timeout ~5 s → 502 v cloudu), proto se síťová vrstva obchází.
//
//  Díky tomu appka na Vercelu nemusí znát nic o zálivce — je to jen tunel.
//  Veškerá logika (zóny, rozvrhy, počasí...) zůstává v webui.cpp.
//
//  TLS: spojení se ověřuje proti kořenovým CA v cloud_ca.h. Bez ověření by
//  kdokoli "po cestě" (MITM) mohl odchytit device token a posílat vlastní
//  příkazy — token sám o sobě kanál nechrání. Ověření lze vypnout
//  (CLOUD_TLS_VERIFY false) jen pro ladění.
//
//  Poll interval je adaptivní: CLOUD_POLL_INTERVAL_MS chvíli po posledním
//  požadavku (uživatel má otevřený dashboard), jinak CLOUD_POLL_IDLE_MS.
//  Šetří to invokace funkcí i Redis příkazy na Vercelu/Upstash.
//  Každý poll používá čerstvé TLS spojení (bez keep-alive). Perzistentní
//  spojení se zkoušelo, ale odpovědi pollu se pak nespolehlivě četly; při
//  klidovém intervalu 12 s handshake nevadí. Při otevřeném dashboardu
//  odpověď na /api/device/response rovnou nese další požadavek, takže na
//  jeden požadavek připadá jen jedno spojení místo dvou (poll + odpověď).
// ═══════════════════════════════════════════════════════════════
#include "cloud_sync.h"
#include "config.h"
#include "cloud_ca.h"
#include "webui.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#ifndef CLOUD_TLS_VERIFY
#define CLOUD_TLS_VERIFY true
#endif
#ifndef CLOUD_POLL_IDLE_MS
#define CLOUD_POLL_IDLE_MS 12000
#endif
#ifndef CLOUD_ACTIVE_WINDOW_MS
#define CLOUD_ACTIVE_WINDOW_MS 180000   // 3 min rychlého pollování po posledním požadavku
#endif

static bool cloudConfigured(void) {
  return CLOUD_ENABLED && strlen(CLOUD_BASE_URL) > 0 && strlen(CLOUD_DEVICE_TOKEN) > 0;
}

static unsigned long lastOkMs      = 0;   // poslední úspěšný poll (pro lokální indikátor)
static unsigned long lastRequestMs = 0;   // poslední skutečný požadavek z appky
static bool          clientReady   = false;

static WiFiClientSecure secureClient;

bool CloudSync_IsConfigured(void) { return cloudConfigured(); }

bool CloudSync_IsOnline(void) {
  if (!cloudConfigured() || lastOkMs == 0) return false;
  return (millis() - lastOkMs) < 3UL * CLOUD_POLL_IDLE_MS;
}

void CloudSync_Init(void) {
  if (!cloudConfigured()) {
    Serial.println("[CLOUD] CLOUD_BASE_URL/CLOUD_DEVICE_TOKEN nenastaveny — vzdálený přístup vypnut");
    return;
  }
#if CLOUD_TLS_VERIFY
  secureClient.setCACert(CLOUD_CA_BUNDLE);
  Serial.printf("[CLOUD] Vzdálený přístup aktivní — relay: %s (TLS ověřeno)\n", CLOUD_BASE_URL);
#else
  secureClient.setInsecure();
  Serial.printf("[CLOUD] Vzdálený přístup aktivní — relay: %s (TLS BEZ ověření!)\n", CLOUD_BASE_URL);
#endif
  clientReady = true;
}

static unsigned long currentInterval(void) {
  bool active = lastRequestMs != 0 && (millis() - lastRequestMs) < CLOUD_ACTIVE_WINDOW_MS;
  return active ? CLOUD_POLL_INTERVAL_MS : CLOUD_POLL_IDLE_MS;
}

struct CloudReq {
  String id, method, path, body;
};

// Přečte požadavek z odpovědi pollu nebo /api/device/response (stejný tvar).
// false = nic nečeká, nebo odpověď nejde přečíst.
static bool parseRequest(const String &json, CloudReq &r) {
  JsonDocument doc;
  DeserializationError jerr = deserializeJson(doc, json);
  if (jerr != DeserializationError::Ok) {
    Serial.printf("[CLOUD] Odpověď appky nejde přečíst: %s (%d B): %.80s\n",
                  jerr.c_str(), (int)json.length(), json.c_str());
    return false;
  }
  const char *idC = doc["requestId"].as<const char*>();
  if (!idC || strlen(idC) == 0) return false;
  const char *methodC = doc["method"].as<const char*>();
  const char *pathC   = doc["path"].as<const char*>();
  const char *bodyC   = doc["body"].as<const char*>();
  r.id     = idC;
  r.method = methodC ? String(methodC) : "GET";
  r.path   = pathC   ? String(pathC)   : "/api/status";
  r.body   = bodyC   ? String(bodyC)   : "";
  return true;
}

// Pošle výsledek požadavku appce. Vrací tělo její odpovědi — obsahuje rovnou
// další čekající požadavek (nebo requestId:null) — a "" při chybě.
static String sendResponse(const CloudReq &r, int status, const String &body) {
  JsonDocument respDoc;
  respDoc["requestId"] = r.id;
  respDoc["status"]    = status;
  respDoc["body"]      = body;
  String respOut;
  serializeJson(respDoc, respOut);

  String respUrl = String(CLOUD_BASE_URL) + "/api/device/response";
  HTTPClient resp;
  resp.setReuse(false);
  resp.setTimeout(8000);
  secureClient.stop();
  if (!resp.begin(secureClient, respUrl)) {
    Serial.println("[CLOUD] Odpověď se nepodařilo odeslat — spojení selhalo");
    return "";
  }
  resp.addHeader("X-Device-Token", CLOUD_DEVICE_TOKEN);
  resp.addHeader("Content-Type", "application/json");
  int respCode = resp.POST(respOut);
  String out;
  if (respCode == 200) {
    out = resp.getString();
    lastOkMs = millis();
    Serial.printf("[CLOUD] Odpověď odeslána (%d, %d B)\n", status, (int)body.length());
  } else {
    Serial.printf("[CLOUD] Odpověď se nepodařilo odeslat, HTTP %d\n", respCode);
  }
  resp.end();
  return out;
}

void CloudSync_Tick(void) {
  if (!cloudConfigured() || !clientReady) { delay(5000); return; }
  if (WiFi.status() != WL_CONNECTED) { delay(2000); return; }  // jen v STA módu (AP nemá internet)

  // ── 1) Zeptej se appky, jestli na nás něco čeká ──────────────────
  String pollUrl = String(CLOUD_BASE_URL) + "/api/device/poll";
  HTTPClient http;
  http.setReuse(false);
  http.setTimeout(8000);
  secureClient.stop();   // vždy čerstvé spojení
  int pollCode = HTTPC_ERROR_CONNECTION_REFUSED;
  if (http.begin(secureClient, pollUrl)) {
    http.addHeader("X-Device-Token", CLOUD_DEVICE_TOKEN);
    pollCode = http.GET();
  }

  if (pollCode != 200) {
    if (pollCode > 0) Serial.printf("[CLOUD] Poll HTTP %d\n", pollCode);
    else              Serial.printf("[CLOUD] Poll selhal: %s\n", HTTPClient::errorToString(pollCode).c_str());
    http.end();
    delay(currentInterval());
    return;
  }
  String pollBody = http.getString();
  http.end();
  lastOkMs = millis();  // appka odpověděla — spojení funguje, bez ohledu na to, jestli něco čekalo

  CloudReq req;
  if (!parseRequest(pollBody, req)) {
    delay(currentInterval());  // nic nečeká
    return;
  }

  // ── 2) Obsluhuj požadavky, dokud appka posílá další ───────────────
  // Odpověď na /api/device/response nese rovnou další čekající požadavek,
  // takže při otevřeném dashboardu stačí jedno HTTPS spojení na požadavek.
  for (;;) {
    lastRequestMs = millis();
    Serial.printf("[CLOUD] Požadavek %s %s %s\n", req.id.c_str(), req.method.c_str(), req.path.c_str());

    int localStatus = 0;
    String localBody;
    bool restartAfter = false;
    if (!WebUI_Dispatch(req.method, req.path, req.body, localStatus, localBody, restartAfter)) {
      Serial.printf("[CLOUD] Neznámá cesta %s %s\n", req.method.c_str(), req.path.c_str());
    }

    String next = sendResponse(req, localStatus, localBody);

    if (restartAfter) {   // /api/restart nebo uložení WiFi s restartem
      Serial.println("[CLOUD] Restart na požadavek z cloudu");
      delay(500);
      ESP.restart();
    }

    // Odeslání selhalo, nebo appka další požadavek neposílá (starší verze
    // bez "requestId" v odpovědi) → hned zpátky na poll.
    if (next.indexOf("\"requestId\"") < 0) return;
    if (!parseRequest(next, req)) break;   // fronta je prázdná
  }
  delay(currentInterval());
}
