#include "zones.h"

static RunState _state = {};

// ── In-memory log (kruhový buffer) ──────────────────────────────
static LogEntry _log[LOG_MAX_ENTRIES];
static int _logHead = 0;
static int _logCount = 0;

// ── Init ────────────────────────────────────────────────────────
void Zones_Init(void) {
  for (int i = 0; i < 8; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    digitalWrite(RELAY_PINS[i], LOW);  // Active HIGH → LOW = vypnuto
  }
  _state.running = false;
  Serial.println("[ZONES] Inicializováno — všechna relé OFF");
}

// ── Master ventil ────────────────────────────────────────────────
void MasterValve_Set(bool on) {
  digitalWrite(RELAY_PINS[MASTER_VALVE_IDX], on ? HIGH : LOW);
  Serial.printf("[ZONES] Master ventil: %s\n", on ? "ON" : "OFF");
}

// ── Spustit zónu ─────────────────────────────────────────────────
bool Zone_Start(uint8_t zone, uint16_t minutes, RunReason reason) {
  if (zone < 1 || zone > ZONE_COUNT) return false;
  if (minutes == 0 || minutes > 120) return false;

  // Zastav případně běžící zónu
  if (_state.running) Zone_Stop();

  SystemSettings ss = Storage_GetSystem();

  // Master ventil
  if (ss.masterValveEnabled) {
    MasterValve_Set(true);
    delay(ss.masterPreDelay * 1000UL);
  }

  // Spustit zónu (index 0-based)
  digitalWrite(RELAY_PINS[zone - 1], HIGH);

  _state.running    = true;
  _state.zone       = zone;
  _state.durationMin = minutes;
  _state.startMs    = millis();
  _state.endMs      = millis() + (unsigned long)minutes * 60000UL;
  _state.reason     = reason;

  Serial.printf("[ZONES] Zóna %d spuštěna na %d min (%s)\n",
    zone, minutes,
    reason == RUN_MANUAL ? "manuálně" : reason == RUN_PROGRAM ? "program" : "test");

  Log_Add(zone, minutes, reason, "Spuštěno");
  return true;
}

// ── Zastavit ─────────────────────────────────────────────────────
void Zone_Stop(void) {
  if (!_state.running) return;

  digitalWrite(RELAY_PINS[_state.zone - 1], LOW);

  SystemSettings ss = Storage_GetSystem();
  if (ss.masterValveEnabled) {
    delay(ss.masterPostDelay * 1000UL);
    MasterValve_Set(false);
  }

  uint16_t elapsed = (uint16_t)((millis() - _state.startMs) / 60000UL);
  Serial.printf("[ZONES] Zóna %d zastavena (běžela ~%d min)\n", _state.zone, elapsed);

  _state.running = false;
  _state.zone    = 0;
}

// ── Tick — hlídá timeout, volat v loop() ────────────────────────
void Zones_Tick(void) {
  if (_state.running && millis() >= _state.endMs) {
    Serial.printf("[ZONES] Zóna %d — čas vypršel\n", _state.zone);
    Zone_Stop();
  }
}

RunState Zones_GetState(void) { return _state; }

// ── Log ──────────────────────────────────────────────────────────
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

int        Log_Count(void)    { return _logCount; }
void       Log_Clear(void)    { _logCount = 0; _logHead = 0; }

LogEntry Log_Get(int idx) {   // 0 = nejnovější
  LogEntry empty = {};
  if (idx >= _logCount) return empty;
  int pos = (_logHead - 1 - idx + LOG_MAX_ENTRIES) % LOG_MAX_ENTRIES;
  return _log[pos];
}
