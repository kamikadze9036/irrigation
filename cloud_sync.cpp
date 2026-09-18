// ═══════════════════════════════════════════════════════════════
//  cloud_sync.cpp — vzdálený přístup přes cloud relay (Vercel)
//
//  Princip: ESP32 nemá žádný otevřený port ani veřejnou IP, takže appka na
//  Vercelu se k němu nemůže sama připojit. Místo toho ESP32 pravidelně
//  "pollne" appku (GET /api/device/poll), jestli tam čeká nějaký požadavek
//  od přihlášeného uživatele. Pokud ano, přehraje ho sám na sobě — pošle
//  stejný HTTP požadavek na vlastní lokální IP, jaký by normálně poslal
//  prohlížeč v domácí síti — a výsledek pošle zpět (POST /api/device/response).
//
//  Díky tomu appka na Vercelu nemusí znát nic o zálivce — je to jen tunel.
//  Veškerá logika (zóny, rozvrhy, počasí...) zůstává v webui.cpp beze změny.
//
//  TLS: spojení se ověřuje proti kořenovým CA v cloud_ca.h. Bez ověření by
//  kdokoli "po cestě" (MITM) mohl odchytit device token a posílat vlastní
//  příkazy — token sám o sobě kanál nechrání. Ověření lze vypnout
//  (CLOUD_TLS_VERIFY false) jen pro ladění.
//
//  Poll interval je adaptivní: CLOUD_POLL_INTERVAL_MS chvíli po posledním
//  požadavku (uživatel má otevřený dashboard), jinak CLOUD_POLL_IDLE_MS.
//  Šetří to invokace funkcí i Redis příkazy na Vercelu/Upstash.
//  TLS spojení a HTTPClient jsou perzistentní (keep-alive) — dřív se každé
//  4 s dělal nový TLS handshake (~1 s CPU + desítky kB heapu).
// ═══════════════════════════════════════════════════════════════
#include "cloud_sync.h"
#include "config.h"
#include "cloud_ca.h"
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
static HTTPClient       http;

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
  http.setReuse(true);
  http.setTimeout(8000);
  clientReady = true;
}

static unsigned long currentInterval(void) {
  bool active = lastRequestMs != 0 && (millis() - lastRequestMs) < CLOUD_ACTIVE_WINDOW_MS;
  return active ? CLOUD_POLL_INTERVAL_MS : CLOUD_POLL_IDLE_MS;
}

// Přehraje jeden požadavek na vlastním lokálním webserveru (stejná cesta,
// jakou by použil prohlížeč v domácí WiFi) — žádná změna webui.cpp potřeba.
static bool replayLocal(const String &method, const String &path, const String &body,
                         int &outStatus, String &outBody) {
  if (!path.startsWith("/")) return false;
  String url = "http://" + WiFi.localIP().toString() + path;
  HTTPClient local;
  if (!local.begin(url)) return false;
  local.setTimeout(8000);

  if (method == "POST") {
    local.addHeader("Content-Type", "application/json");
    outStatus = local.POST(body);
  } else {
    outStatus = local.GET();
  }
  outBody = local.getString();
  local.end();
  return outStatus > 0;
}

void CloudSync_Tick(void) {
  if (!cloudConfigured() || !clientReady) { delay(5000); return; }
  if (WiFi.status() != WL_CONNECTED) { delay(2000); return; }  // jen v STA módu (AP nemá internet)

  // ── 1) Zeptej se appky, jestli na nás něco čeká ──────────────────
  String pollUrl = String(CLOUD_BASE_URL) + "/api/device/poll";
  int pollCode = 0;
  for (int attempt = 0; attempt < 2; attempt++) {
    if (!http.begin(secureClient, pollUrl)) { pollCode = HTTPC_ERROR_CONNECTION_REFUSED; break; }
    http.addHeader("X-Device-Token", CLOUD_DEVICE_TOKEN);
    pollCode = http.GET();
    // Keep-alive spojení mohl server mezitím zavřít — jednou zkus znovu s čistým spojením
    if (pollCode == HTTPC_ERROR_CONNECTION_LOST || pollCode == HTTPC_ERROR_SEND_HEADER_FAILED ||
        pollCode == HTTPC_ERROR_NOT_CONNECTED) {
      http.end();
      secureClient.stop();
      continue;
    }
    break;
  }

  if (pollCode != 200) {
    if (pollCode > 0) Serial.printf("[CLOUD] Poll HTTP %d\n", pollCode);
    else              Serial.printf("[CLOUD] Poll selhal: %s\n", HTTPClient::errorToString(pollCode).c_str());
    http.end();
    secureClient.stop();   // po chybě spojení začít příště čistě
    delay(currentInterval());
    return;
  }
  String pollBody = http.getString();
  http.end();
  lastOkMs = millis();  // appka odpověděla — spojení funguje, bez ohledu na to, jestli něco čekalo

  JsonDocument doc;
  if (deserializeJson(doc, pollBody) != DeserializationError::Ok) {
    delay(currentInterval());
    return;
  }

  const char *reqIdC = doc["requestId"].as<const char*>();
  if (!reqIdC || strlen(reqIdC) == 0) {
    delay(currentInterval());  // nic nečeká
    return;
  }
  lastRequestMs = millis();
  String reqId = reqIdC;

  const char *methodC = doc["method"].as<const char*>();
  const char *pathC   = doc["path"].as<const char*>();
  const char *bodyC   = doc["body"].as<const char*>();
  String method = methodC ? String(methodC) : "GET";
  String path   = pathC   ? String(pathC)   : "/api/status";
  String body   = bodyC   ? String(bodyC)   : "";

  Serial.printf("[CLOUD] Požadavek %s %s %s\n", reqId.c_str(), method.c_str(), path.c_str());

  // ── 2) Přehraj lokálně ────────────────────────────────────────────
  int localStatus = 0;
  String localBody;
  if (!replayLocal(method, path, body, localStatus, localBody)) {
    localStatus = 502;
    localBody   = "{\"ok\":false,\"error\":\"Lokální požadavek selhal\"}";
  }

  // ── 3) Pošli odpověď zpátky do fronty ─────────────────────────────
  JsonDocument respDoc;
  respDoc["requestId"] = reqId;
  respDoc["status"]    = localStatus;
  respDoc["body"]      = localBody;
  String respOut;
  serializeJson(respDoc, respOut);

  String respUrl = String(CLOUD_BASE_URL) + "/api/device/response";
  if (http.begin(secureClient, respUrl)) {
    http.addHeader("X-Device-Token", CLOUD_DEVICE_TOKEN);
    http.addHeader("Content-Type", "application/json");
    int respCode = http.POST(respOut);
    if (respCode != 200) Serial.printf("[CLOUD] Odpověď se nepodařilo odeslat, HTTP %d\n", respCode);
    http.end();
  }

  // Hned zkus další — pokud čeká víc požadavků (např. víc otevřených tabů),
  // nečekej na ně celý interval.
}
