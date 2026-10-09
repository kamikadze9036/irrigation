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
//  Každý poll používá čerstvé TLS spojení (bez keep-alive). Perzistentní
//  spojení se zkoušelo, ale odpovědi pollu se pak nespolehlivě četly; při
//  klidovém intervalu 12 s handshake nevadí.
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

// Přehraje jeden požadavek na vlastním lokálním webserveru (stejná cesta,
// jakou by použil prohlížeč v domácí WiFi) — žádná změna webui.cpp potřeba.
// Zkouší nejdřív loopback (nezávisí na WiFi rozhraní), pak vlastní IP.
// Důvod selhání se loguje — dřív se z "502" nedalo poznat, co se stalo.
static bool replayLocalOn(const String &host, const String &method, const String &path,
                          const String &body, int &outStatus, String &outBody) {
  HTTPClient local;
  local.setReuse(false);
  local.setConnectTimeout(3000);
  local.setTimeout(8000);
  if (!local.begin("http://" + host + path)) {
    Serial.printf("[CLOUD] Lokálně %s: begin() selhal\n", host.c_str());
    return false;
  }
  if (method == "POST") {
    local.addHeader("Content-Type", "application/json");
    outStatus = local.POST(body);
  } else {
    outStatus = local.GET();
  }
  if (outStatus <= 0) {
    Serial.printf("[CLOUD] Lokálně %s: %s (%d)\n", host.c_str(),
                  HTTPClient::errorToString(outStatus).c_str(), outStatus);
    local.end();
    return false;
  }
  outBody = local.getString();
  local.end();
  return true;
}

static bool replayLocal(const String &method, const String &path, const String &body,
                         int &outStatus, String &outBody) {
  if (!path.startsWith("/")) return false;
  if (replayLocalOn("127.0.0.1", method, path, body, outStatus, outBody)) return true;
  return replayLocalOn(WiFi.localIP().toString(), method, path, body, outStatus, outBody);
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

  JsonDocument doc;
  DeserializationError jerr = deserializeJson(doc, pollBody);
  if (jerr != DeserializationError::Ok) {
    Serial.printf("[CLOUD] Odpověď pollu nejde přečíst: %s (%d B): %.80s\n",
                  jerr.c_str(), (int)pollBody.length(), pollBody.c_str());
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
  HTTPClient resp;
  resp.setReuse(false);
  resp.setTimeout(8000);
  secureClient.stop();
  if (resp.begin(secureClient, respUrl)) {
    resp.addHeader("X-Device-Token", CLOUD_DEVICE_TOKEN);
    resp.addHeader("Content-Type", "application/json");
    int respCode = resp.POST(respOut);
    if (respCode == 200) Serial.printf("[CLOUD] Odpověď odeslána (%d, %d B)\n", localStatus, (int)localBody.length());
    else                 Serial.printf("[CLOUD] Odpověď se nepodařilo odeslat, HTTP %d\n", respCode);
    resp.end();
  } else {
    Serial.println("[CLOUD] Odpověď se nepodařilo odeslat — spojení selhalo");
  }

  // Hned zkus další — pokud čeká víc požadavků (např. víc otevřených tabů),
  // nečekej na ně celý interval.
}
