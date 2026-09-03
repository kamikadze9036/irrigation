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
// ═══════════════════════════════════════════════════════════════
#include "cloud_sync.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

static bool cloudConfigured(void) {
  return CLOUD_ENABLED && strlen(CLOUD_BASE_URL) > 0 && strlen(CLOUD_DEVICE_TOKEN) > 0;
}

void CloudSync_Init(void) {
  if (!cloudConfigured()) {
    Serial.println("[CLOUD] CLOUD_BASE_URL/CLOUD_DEVICE_TOKEN nenastaveny — vzdálený přístup vypnut");
    return;
  }
  Serial.printf("[CLOUD] Vzdálený přístup aktivní — relay: %s\n", CLOUD_BASE_URL);
}

// Přehraje jeden požadavek na vlastním lokálním webserveru (stejná cesta,
// jakou by použil prohlížeč v domácí WiFi) — žádná změna webui.cpp potřeba.
static bool replayLocal(const String &method, const String &path, const String &body,
                         int &outStatus, String &outBody) {
  String url = "http://" + WiFi.localIP().toString() + path;
  HTTPClient http;
  if (!http.begin(url)) return false;
  http.setTimeout(8000);

  if (method == "POST") {
    http.addHeader("Content-Type", "application/json");
    outStatus = http.POST(body);
  } else {
    outStatus = http.GET();
  }
  outBody = http.getString();
  http.end();
  return outStatus > 0;
}

void CloudSync_Tick(void) {
  if (!cloudConfigured()) { delay(5000); return; }
  if (WiFi.status() != WL_CONNECTED) { delay(2000); return; }  // jen v STA módu (AP nemá internet)

  WiFiClientSecure secureClient;
  secureClient.setInsecure();  // bez ověření CA řetězce — kanál je chráněný device tokenem,
                                // ne TLS identitou; embedovat CA root appky by přidalo
                                // křehkost bez reálného přínosu proti tomuto modelu hrozeb

  // ── 1) Zeptej se appky, jestli na nás něco čeká ──────────────────
  HTTPClient poll;
  String pollUrl = String(CLOUD_BASE_URL) + "/api/device/poll";
  if (!poll.begin(secureClient, pollUrl)) { delay(CLOUD_POLL_INTERVAL_MS); return; }
  poll.addHeader("X-Device-Token", CLOUD_DEVICE_TOKEN);
  poll.setTimeout(8000);
  int pollCode = poll.GET();

  if (pollCode != 200) {
    if (pollCode > 0) Serial.printf("[CLOUD] Poll HTTP %d\n", pollCode);
    poll.end();
    delay(CLOUD_POLL_INTERVAL_MS);
    return;
  }
  String pollBody = poll.getString();
  poll.end();

  JsonDocument doc;
  if (deserializeJson(doc, pollBody) != DeserializationError::Ok) {
    delay(CLOUD_POLL_INTERVAL_MS);
    return;
  }

  const char *reqIdC = doc["requestId"].as<const char*>();
  if (!reqIdC || strlen(reqIdC) == 0) {
    delay(CLOUD_POLL_INTERVAL_MS);  // nic nečeká
    return;
  }
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

  HTTPClient resp;
  String respUrl = String(CLOUD_BASE_URL) + "/api/device/response";
  if (resp.begin(secureClient, respUrl)) {
    resp.addHeader("X-Device-Token", CLOUD_DEVICE_TOKEN);
    resp.addHeader("Content-Type", "application/json");
    resp.setTimeout(8000);
    int respCode = resp.POST(respOut);
    if (respCode != 200) Serial.printf("[CLOUD] Odpověď se nepodařilo odeslat, HTTP %d\n", respCode);
    resp.end();
  }

  // Hned zkus další — pokud čeká víc požadavků (např. víc otevřených tabů),
  // nečekej na ně celý CLOUD_POLL_INTERVAL_MS.
}
