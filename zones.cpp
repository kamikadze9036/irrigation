// ═══════════════════════════════════════════════════════════════
//  zones.cpp — ovládání relé, sekvenční fronta, in-memory log
//
//  Návrh:
//   • Veškerý stav chrání rekurzivní mutex — funkce se volají z core 0
//     (WebServer, CloudSync) i z core 1 (loop). Bez zámku hrozilo, že
//     Zones_Tick uvidí zónu s running=true, ale ještě starým endMs, a
//     okamžitě ji vypne.
//   • Nic tu neblokuje. Prodlevy master ventilu (pre/post) a test relé
//     jsou stavové automaty odbavované v Zones_Tick(). HTTP handlery tak
//     odpovídají ihned a cloud tunel nevrací timeouty.
//   • Porovnání času přes (long)(now - t) >= 0 je odolné vůči přetečení
//     millis() (~49,7 dne).
// ═══════════════════════════════════════════════════════════════
#include "zones.h"

// ── Zámek ────────────────────────────────────────────────────────
static SemaphoreHandle_t zonesMutex = nullptr;
struct ZoneLock {
  ZoneLock()  { if (zonesMutex) xSemaphoreTakeRecursive(zonesMutex, portMAX_DELAY); }
  ~ZoneLock() { if (zonesMutex) xSemaphoreGiveRecursive(zonesMutex); }
};

static inline bool timeReached(unsigned long now, unsigned long t) {
  return (long)(now - t) >= 0;
}

// ── Per-zóna stav (1-indexed, [0] nevyužito) ────────────────────
static ZoneRunState _zones[ZONE_COUNT + 1] = {};

// ── Master ventil ────────────────────────────────────────────────
static bool          _masterOpen = false;
static bool          _masterCloseScheduled = false;   // čeká se na post-delay
static unsigned long _masterCloseAtMs = 0;

// ── Sekvenční fronta ─────────────────────────────────────────────
#define QUEUE_MAX 8
static QueueEntry _queue[QUEUE_MAX];
static int _qHead  = 0;
static int _qTail  = 0;
static int _qCount = 0;

// ── Test relé ────────────────────────────────────────────────────
#define TEST_ON_MS   3000
#define TEST_GAP_MS  300
static bool          _testActive  = false;
static uint8_t       _testZone    = 0;      // aktuálně testovaná zóna (1..ZONE_COUNT)
static bool          _testRelayOn = false;
static unsigned long _testNextMs  = 0;

// ── In-memory log ────────────────────────────────────────────────
static LogEntry _log[LOG_MAX_ENTRIES];
static int _logHead  = 0;
static int _logCount = 0;

static inline void relayWrite(uint8_t zone, bool on) {
  digitalWrite(RELAY_PINS[zone - 1], on ? HIGH : LOW);
}

// ═══════════════════════════════════════════════════════════════
//  Init
// ═══════════════════════════════════════════════════════════════
void Zones_Init(void) {
  if (!zonesMutex) zonesMutex = xSemaphoreCreateRecursiveMutex();
  ZoneLock lock;
  for (int i = 0; i < 8; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    digitalWrite(RELAY_PINS[i], LOW);   // Active HIGH → LOW = vypnuto
  }
  memset(_zones, 0, sizeof(_zones));
  _masterOpen = false;
  _masterCloseScheduled = false;
  _testActive = false;
  _qHead = _qTail = _qCount = 0;
  Serial.println("[ZONES] Inicializováno — všechna relé OFF");
}

// ═══════════════════════════════════════════════════════════════
//  Master ventil
// ═══════════════════════════════════════════════════════════════
void MasterValve_Set(bool on) {
  ZoneLock lock;
  _masterCloseScheduled = false;           // explicitní změna ruší čekající zavření
  if (on == _masterOpen) return;           // zamezí zbytečným sepnutím
  _masterOpen = on;
  digitalWrite(RELAY_PINS[MASTER_VALVE_IDX], on ? HIGH : LOW);
  Serial.printf("[ZONES] Master ventil: %s\n", on ? "ON" : "OFF");
}

bool MasterValve_IsOpen(void) { ZoneLock lock; return _masterOpen; }

// Naplánuje zavření master ventilu po post-delay (nebo zavře hned, je-li 0).
// Idempotentní — volá se i opakovaně z Zones_Tick, dokud je systém v klidu.
static void scheduleMasterClose(void) {
  if (!_masterOpen || _masterCloseScheduled) return;
  SystemSettings ss = Storage_GetSystem();
  uint8_t post = ss.masterValveEnabled ? ss.masterPostDelay : 0;
  if (post == 0) { MasterValve_Set(false); return; }
  _masterCloseScheduled = true;
  _masterCloseAtMs = millis() + post * 1000UL;
  Serial.printf("[ZONES] Master ventil se zavře za %d s\n", post);
}

// ═══════════════════════════════════════════════════════════════
//  Dotazy na stav
// ═══════════════════════════════════════════════════════════════
ZoneRunState Zone_GetState(uint8_t zone) {
  if (zone < 1 || zone > ZONE_COUNT) return ZoneRunState{};
  ZoneLock lock;
  return _zones[zone];
}

static bool anyRunningUnlocked(void) {
  for (int z = 1; z <= ZONE_COUNT; z++)
    if (_zones[z].running) return true;
  return false;
}

bool Zones_AnyRunning(void) { ZoneLock lock; return anyRunningUnlocked(); }

int Zones_RunningCount(void) {
  ZoneLock lock;
  int cnt = 0;
  for (int z = 1; z <= ZONE_COUNT; z++)
    if (_zones[z].running) cnt++;
  return cnt;
}

// ═══════════════════════════════════════════════════════════════
//  Sekvenční fronta
// ═══════════════════════════════════════════════════════════════
bool Queue_Add(uint8_t zone, uint16_t minutes, RunReason reason) {
  if (zone < 1 || zone > ZONE_COUNT || minutes == 0 || minutes > 120) return false;
  ZoneLock lock;
  if (_qCount >= QUEUE_MAX) return false;
  _queue[_qTail] = {zone, minutes, reason};
  _qTail = (_qTail + 1) % QUEUE_MAX;
  _qCount++;
  Serial.printf("[ZONES] Fronta: přidána zóna %d (%d min), celkem %d\n",
                zone, minutes, _qCount);
  return true;
}

void Queue_Clear(void) {
  ZoneLock lock;
  _qHead = _qTail = _qCount = 0;
}

int Queue_Count(void) { ZoneLock lock; return _qCount; }

static void queueStartNext(void) {
  if (_qCount == 0) return;
  QueueEntry e = _queue[_qHead];
  _qHead = (_qHead + 1) % QUEUE_MAX;
  _qCount--;
  Serial.printf("[ZONES] Fronta: spouštím zónu %d (%d min), zbývá %d\n",
                e.zone, e.minutes, _qCount);
  Zone_Start(e.zone, e.minutes, e.reason, true);
}

// ═══════════════════════════════════════════════════════════════
//  Zone_Start
// ═══════════════════════════════════════════════════════════════
static void stopAllRelaysUnlocked(void) {
  for (int z = 1; z <= ZONE_COUNT; z++) {
    if (_zones[z].running)
      Serial.printf("[ZONES] Zóna %d zastavena (StopAll)\n", z);
    relayWrite(z, false);
    _zones[z].running = false;
    _zones[z].relayOn = false;
  }
  _qHead = _qTail = _qCount = 0;
  _testActive  = false;
  _testRelayOn = false;
}

bool Zone_Start(uint8_t zone, uint16_t minutes, RunReason reason, bool parallel) {
  if (zone < 1 || zone > ZONE_COUNT) return false;
  if (minutes == 0 || minutes > 120)  return false;
  ZoneLock lock;

  // Nepararelní start = přepnutí: ostatní zóny a fronta skončí, ale master
  // ventil zůstává otevřený (čerpadlo se zbytečně necykluje OFF→ON).
  if (!parallel) stopAllRelaysUnlocked();

  SystemSettings ss = Storage_GetSystem();
  unsigned long now     = millis();
  unsigned long startAt = now;
  if (ss.masterValveEnabled) {
    if (!_masterOpen) {
      MasterValve_Set(true);
      startAt = now + ss.masterPreDelay * 1000UL;   // relé sepne až po pre-delay (v Tick)
    } else {
      _masterCloseScheduled = false;                // zrušit čekající zavření
    }
  }

  ZoneRunState st = {};
  st.running     = true;
  st.relayOn     = (startAt == now);
  st.durationMin = minutes;
  st.startMs     = startAt;
  st.endMs       = startAt + (unsigned long)minutes * 60000UL;
  st.reason      = reason;
  _zones[zone]   = st;
  if (st.relayOn) relayWrite(zone, true);

  const char *reasonStr =
    (reason == RUN_MANUAL)   ? "manuálně"  :
    (reason == RUN_PROGRAM)  ? "program"   :
    (reason == RUN_SEQUENCE) ? "sekvence"  : "test";
  Serial.printf("[ZONES] Zóna %d spuštěna na %d min (%s%s%s)\n",
    zone, minutes, reasonStr, parallel ? ", paralelně" : "",
    st.relayOn ? "" : ", čekám na master ventil");

  Log_Add(zone, minutes, reason, parallel ? "Spuštěno (paralelně)" : "Spuštěno");
  return true;
}

// ═══════════════════════════════════════════════════════════════
//  Zone_Stop — zastaví konkrétní zónu
// ═══════════════════════════════════════════════════════════════
void Zone_Stop(uint8_t zone) {
  if (zone < 1 || zone > ZONE_COUNT) return;
  ZoneLock lock;
  if (!_zones[zone].running) return;

  relayWrite(zone, false);
  unsigned long now = millis();
  uint16_t elapsed = timeReached(now, _zones[zone].startMs)
                       ? (uint16_t)((now - _zones[zone].startMs) / 60000UL) : 0;
  Serial.printf("[ZONES] Zóna %d zastavena (~%d min)\n", zone, elapsed);
  _zones[zone].running = false;
  _zones[zone].relayOn = false;

  // Nic neběží → buď pokračuje fronta, nebo se zavře master ventil.
  // (Dřív zastavení jedné zóny uprostřed sekvence nechalo frontu stát
  //  a master ventil otevřený.)
  if (!anyRunningUnlocked()) {
    if (_qCount > 0) queueStartNext();
    else             scheduleMasterClose();
  }
}

// ═══════════════════════════════════════════════════════════════
//  Zone_StopAll — zastaví všechny zóny, frontu i test
// ═══════════════════════════════════════════════════════════════
void Zone_StopAll(void) {
  ZoneLock lock;
  bool wasSomething = anyRunningUnlocked() || _testActive;
  stopAllRelaysUnlocked();
  if (_masterOpen) {
    if (wasSomething) scheduleMasterClose();
    else              MasterValve_Set(false);
  }
}

// ═══════════════════════════════════════════════════════════════
//  Test relé — stavový automat
// ═══════════════════════════════════════════════════════════════
bool Zones_StartTest(void) {
  ZoneLock lock;
  if (_testActive) return false;
  stopAllRelaysUnlocked();

  SystemSettings ss = Storage_GetSystem();
  unsigned long now = millis();
  _testActive  = true;
  _testZone    = 1;
  _testRelayOn = false;
  _testNextMs  = now;
  if (ss.masterValveEnabled) {
    if (!_masterOpen) {
      MasterValve_Set(true);
      _testNextMs = now + ss.masterPreDelay * 1000UL;
    } else {
      _masterCloseScheduled = false;
    }
  }
  Serial.println("[ZONES] Test relé spuštěn");
  Log_Add(0, 0, RUN_TEST, "Test relé spuštěn");
  return true;
}

bool Zones_TestRunning(void) { ZoneLock lock; return _testActive; }

static void tickTest(unsigned long now) {
  if (!timeReached(now, _testNextMs)) return;
  if (_testRelayOn) {
    relayWrite(_testZone, false);
    _testRelayOn = false;
    _testZone++;
    _testNextMs = now + TEST_GAP_MS;
    if (_testZone > ZONE_COUNT) {
      _testActive = false;
      Serial.println("[ZONES] Test relé dokončen");
      Log_Add(0, 0, RUN_TEST, "Test relé dokončen");
    }
  } else {
    Serial.printf("[ZONES] Test: zóna %d\n", _testZone);
    relayWrite(_testZone, true);
    _testRelayOn = true;
    _testNextMs  = now + TEST_ON_MS;
  }
}

// ═══════════════════════════════════════════════════════════════
//  Zones_Tick — pre-delay, timeouty, fronta, post-delay, test
// ═══════════════════════════════════════════════════════════════
void Zones_Tick(void) {
  ZoneLock lock;
  unsigned long now = millis();

  if (_testActive) tickTest(now);

  for (int z = 1; z <= ZONE_COUNT; z++) {
    ZoneRunState &st = _zones[z];
    if (!st.running) continue;
    if (!st.relayOn) {
      if (timeReached(now, st.startMs)) {       // pre-delay master ventilu uplynul
        relayWrite(z, true);
        st.relayOn = true;
        Serial.printf("[ZONES] Zóna %d — relé sepnuto (po pre-delay)\n", z);
      }
      continue;
    }
    if (timeReached(now, st.endMs)) {
      Serial.printf("[ZONES] Zóna %d — čas vypršel\n", z);
      relayWrite(z, false);
      st.running = false;
      st.relayOn = false;
    }
  }

  if (!anyRunningUnlocked() && !_testActive) {
    if (_qCount > 0) queueStartNext();
    else             scheduleMasterClose();       // no-op, pokud už je zavřený/naplánovaný
  }

  if (_masterCloseScheduled && timeReached(now, _masterCloseAtMs)) {
    if (!anyRunningUnlocked() && _qCount == 0 && !_testActive) MasterValve_Set(false);
    else _masterCloseScheduled = false;           // mezitím něco začalo → nezavírat
  }
}

// ═══════════════════════════════════════════════════════════════
//  Log
// ═══════════════════════════════════════════════════════════════
void Log_Add(uint8_t zone, uint16_t dur, RunReason reason, const char* note) {
  ZoneLock lock;
  LogEntry &e = _log[_logHead];
  e.timestamp   = time(nullptr);
  e.zone        = zone;
  e.trigger     = (uint8_t)reason;
  e.durationMin = dur;
  strlcpy(e.note, note ? note : "", sizeof(e.note));
  _logHead = (_logHead + 1) % LOG_MAX_ENTRIES;
  if (_logCount < LOG_MAX_ENTRIES) _logCount++;
}

int  Log_Count(void) { ZoneLock lock; return _logCount; }
void Log_Clear(void) { ZoneLock lock; _logCount = 0; _logHead = 0; }

LogEntry Log_Get(int idx) {
  ZoneLock lock;
  LogEntry empty = {};
  if (idx < 0 || idx >= _logCount) return empty;
  int pos = (_logHead - 1 - idx + LOG_MAX_ENTRIES) % LOG_MAX_ENTRIES;
  return _log[pos];
}
