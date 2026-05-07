#pragma once
#include <Arduino.h>
#include "config.h"
#include "storage.h"

// ── Důvod spuštění ───────────────────────────────────────────────
enum RunReason { RUN_MANUAL = 0, RUN_PROGRAM = 1, RUN_TEST = 2, RUN_SEQUENCE = 3 };

// ── Stav jedné zóny ─────────────────────────────────────────────
struct ZoneRunState {
  bool          running;
  uint16_t      durationMin;
  unsigned long startMs;
  unsigned long endMs;
  RunReason     reason;
};

// ── Položka sekvenční fronty ─────────────────────────────────────
struct QueueEntry {
  uint8_t  zone;
  uint16_t minutes;
};

// ── Log záznam (in-memory) ───────────────────────────────────────
struct LogEntry {
  time_t   timestamp;
  uint8_t  zone;
  uint8_t  trigger;
  uint16_t durationMin;
  char     note[48];
};

// ── Ovládání zón ─────────────────────────────────────────────────
void         Zones_Init(void);
void         Zones_Tick(void);            // volat v loop() — hlídá timeouty + frontu

// parallel=false (výchozí) → zastaví ostatní zóny a frontu, spustí tuto
// parallel=true             → spustí vedle případně běžících zón
bool         Zone_Start(uint8_t zone, uint16_t minutes,
                        RunReason reason = RUN_MANUAL, bool parallel = false);
void         Zone_Stop(uint8_t zone);
void         Zone_StopAll(void);

// ── Dotazy na stav ───────────────────────────────────────────────
ZoneRunState Zone_GetState(uint8_t zone);  // 1–6
bool         Zones_AnyRunning(void);
int          Zones_RunningCount(void);

// ── Master ventil ─────────────────────────────────────────────────
void         MasterValve_Set(bool on);

// ── Sekvenční fronta ─────────────────────────────────────────────
void         Queue_Add(uint8_t zone, uint16_t minutes);
void         Queue_Clear(void);
int          Queue_Count(void);

// ── Log ───────────────────────────────────────────────────────────
void         Log_Add(uint8_t zone, uint16_t dur, RunReason reason, const char* note);
int          Log_Count(void);
LogEntry     Log_Get(int idx);    // 0 = nejnovější
void         Log_Clear(void);
