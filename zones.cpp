// ═══════════════════════════════════════════════════════════════
//  zones.cpp — ovládání relé, sekvenční fronta, in-memory log
//  Podporuje paralelní spuštění více zón i sekvenční frontu
// ═══════════════════════════════════════════════════════════════
#include "zones.h"

// ── Per-zóna stav (1-indexed, [0] nevyužito) ────────────────────
static ZoneRunState _zones[ZONE_COUNT + 1] = {};

// ── Master ventil ────────────────────────────────────────────────
static bool _masterOpen = false;

// ── Sekvenční fronta ─────────────────────────────────────────────
#define QUEUE_MAX 8
static QueueEntry _queue[QUEUE_MAX];
static int _qHead  = 0;
static int _qTail  = 0;
static int _qCount = 0;

// ── In-memory log ────────────────────────────────────────────────
static LogEntry _log[LOG_MAX_ENTRIES];
static int _logHead  = 0;
static int _logCount = 0;

// ═══════════════════════════════════════════════════════════════
//  Init
// ═══════════════════════════════════════════════════════════════
void Zones_Init(void) {
  for (int i = 0; i < 8; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    digitalWrite(RELAY_PINS[i], LOW);   // Active HIGH → LOW = vypnuto
  }
  memset(_zones, 0, sizeof(_zones));
  _masterOpen = false;
  _qHead = _qTail = _qCount = 0;
  Serial.println("[ZONES] Inicializováno — všechna relé OFF");
}

// ═══════════════════════════════════════════════════════════════
//  Master ventil
// ═══════════════════════════════════════════════════════════════
void MasterValve_Set(bool on) {
  if (on == _masterOpen) return;           // zamezí zbytečným sepnutím
  _masterOpen = on;
  digitalWrite(RELAY_PINS[MASTER_VALVE_IDX], on ? HIGH : LOW);
  Serial.printf("[ZONES] Master ventil: %s\n", on ? "ON" : "OFF");
}

// ═══════════════════════════════════════════════════════════════
//  Dotazy na stav
// ═══════════════════════════════════════════════════════════════
ZoneRunState Zone_GetState(uint8_t zone) {
  if (zone < 1 || zone > ZONE_COUNT) return ZoneRunState{};
  return _zones[zone];
}

bool Zones_AnyRunning(void) {
  for (int z = 1; z <= ZONE_COUNT; z++)
    if (_zones[z].running) return true;
  return false;
}

int Zones_RunningCount(void) {
  int cnt = 0;
  for (int z = 1; z <= ZONE_COUNT; z++)
    if (_zones[z].running) cnt++;
  return cnt;
}

// ═══════════════════════════════════════════════════════════════
//  Sekvenční fronta
// ═══════════════════════════════════════════════════════════════
void Queue_Add(uint8_t zone, uint16_t minutes) {
  if (_qCount >= QUEUE_MAX) return;
  _queue[_qTail] = {zone, minutes};
  _qTail = (_qTail + 1) % QUEUE_MAX;
  _qCount++;
  Serial.printf("[ZONES] Fronta: přidána zóna %d (%d min), celkem %d\n",
                zone, minutes, _qCount);
}

void Queue_Clear(void) {
  _qHead = _qTail = _qCount = 0;
}

int Queue_Count(void) { return _qCount; }

static void queue_start_next(void) {
  if (_qCount == 0) return;
  QueueEntry e = _queue[_qHead];
  _qHead = (_qHead + 1) % QUEUE_MAX;
  _qCount--;
  Serial.printf("[ZONES] Sekvence: spouštím zónu %d (%d min), zbývá %d\n",
                e.zone, e.minutes, _qCount);
  Zone_Start(e.zone, e.minutes, RUN_SEQUENCE, true);
}

// ═══════════════════════════════════════════════════════════════
//  Zone_Start
// ═══════════════════════════════════════════════════════════════
bool Zone_Start(uint8_t zone, uint16_t minutes, RunReason reason, bool parallel) {
  if (zone < 1 || zone > ZONE_COUNT) return false;
  if (minutes == 0 || minutes > 120)  return false;

  // Pokud není paralelní → zastaví vše a vymaže frontu
  if (!parallel) {
    Zone_StopAll();
    Queue_Clear();
  }

  // Master ventil — otevři jen pokud ještě není otevřený
  SystemSettings ss = Storage_GetSystem();
  if (ss.masterValveEnabled && !_masterOpen) {
    MasterValve_Set(true);
    if (ss.masterPreDelay > 0) delay(ss.masterPreDelay * 1000UL);
  }

  // Spustit relé
  digitalWrite(RELAY_PINS[zone - 1], HIGH);

  _zones[zone].running     = true;
  _zones[zone].durationMin = minutes;
  _zones[zone].startMs     = millis();
  _zones[zone].endMs       = millis() + (unsigned long)minutes * 60000UL;
  _zones[zone].reason      = reason;

  const char *reasonStr =
    (reason == RUN_MANUAL)   ? "manuálně"  :
    (reason == RUN_PROGRAM)  ? "program"   :
    (reason == RUN_SEQUENCE) ? "sekvence"  : "test";
  Serial.printf("[ZONES] Zóna %d spuštěna na %d min (%s%s)\n",
    zone, minutes, reasonStr, parallel ? ", paralelně" : "");

  Log_Add(zone, minutes, reason, parallel ? "Spuštěno (paralelně)" : "Spuštěno");
  return true;
}

// ═══════════════════════════════════════════════════════════════
//  Zone_Stop — zastaví konkrétní zónu
// ═══════════════════════════════════════════════════════════════
void Zone_Stop(uint8_t zone) {
  if (zone < 1 || zone > ZONE_COUNT) return;
  if (!_zones[zone].running) return;

  digitalWrite(RELAY_PINS[zone - 1], LOW);
  uint16_t elapsed = (uint16_t)((millis() - _zones[zone].startMs) / 60000UL);
  Serial.printf("[ZONES] Zóna %d zastavena (~%d min)\n", zone, elapsed);
  _zones[zone].running = false;

  // Master ventil — zavři jen pokud žádná jiná zóna neběží a fronta je prázdná
  SystemSettings ss = Storage_GetSystem();
  if (ss.masterValveEnabled && _masterOpen && !Zones_AnyRunning() && _qCount == 0) {
    if (ss.masterPostDelay > 0) delay(ss.masterPostDelay * 1000UL);
    MasterValve_Set(false);
  }
}

// ═══════════════════════════════════════════════════════════════
//  Zone_StopAll — zastaví všechny zóny a frontu
// ═══════════════════════════════════════════════════════════════
void Zone_StopAll(void) {
  bool wasSomething = false;
  for (int z = 1; z <= ZONE_COUNT; z++) {
    if (_zones[z].running) {
      digitalWrite(RELAY_PINS[z - 1], LOW);
      _zones[z].running = false;
      Serial.printf("[ZONES] Zóna %d zastavena (StopAll)\n", z);
      wasSomething = true;
    }
  }
  Queue_Clear();

  SystemSettings ss = Storage_GetSystem();
  if (ss.masterValveEnabled && _masterOpen) {
    if (wasSomething && ss.masterPostDelay > 0) delay(ss.masterPostDelay * 1000UL);
    MasterValve_Set(false);
  }
}

// ═══════════════════════════════════════════════════════════════
//  Zones_Tick — hlídá timeouty, spouští frontu
// ═══════════════════════════════════════════════════════════════
void Zones_Tick(void) {
  unsigned long now = millis();
  bool anyTimedOut  = false;

  for (int z = 1; z <= ZONE_COUNT; z++) {
    if (_zones[z].running && now >= _zones[z].endMs) {
      Serial.printf("[ZONES] Zóna %d — čas vypršel\n", z);
      digitalWrite(RELAY_PINS[z - 1], LOW);
      _zones[z].running = false;
      anyTimedOut = true;
    }
  }

  if (!anyTimedOut) return;

  // Fronta čeká a žádná zóna neběží → spusť další
  if (_qCount > 0 && !Zones_AnyRunning()) {
    queue_start_next();
    return;
  }

  // Fronta prázdná a nic neběží → zavři master ventil
  if (!Zones_AnyRunning() && _qCount == 0) {
    SystemSettings ss = Storage_GetSystem();
    if (ss.masterValveEnabled && _masterOpen) {
      if (ss.masterPostDelay > 0) delay(ss.masterPostDelay * 1000UL);
      MasterValve_Set(false);
    }
  }
}

// ═══════════════════════════════════════════════════════════════
//  Log
// ═══════════════════════════════════════════════════════════════
void Log_Add(uint8_t zone, uint16_t dur, RunReason reason, const char* note) {
  LogEntry &e = _log[_logHead];
  e.timestamp   = time(nullptr);
  e.zone        = zone;
  e.trigger     = (uint8_t)reason;
  e.durationMin = dur;
  strlcpy(e.note, note ? note : "", sizeof(e.note));
  _logHead = (_logHead + 1) % LOG_MAX_ENTRIES;
  if (_logCount < LOG_MAX_ENTRIES) _logCount++;
}

int      Log_Count(void)    { return _logCount; }
void     Log_Clear(void)    { _logCount = 0; _logHead = 0; }

LogEntry Log_Get(int idx) {
  LogEntry empty = {};
  if (idx >= _logCount) return empty;
  int pos = (_logHead - 1 - idx + LOG_MAX_ENTRIES) % LOG_MAX_ENTRIES;
  return _log[pos];
}
