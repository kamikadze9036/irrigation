#pragma once
#include <Arduino.h>
#include "config.h"
#include "storage.h"

// ── Důvod spuštění ───────────────────────────────────────────────
enum RunReason { RUN_MANUAL = 0, RUN_PROGRAM = 1, RUN_TEST = 2, RUN_SEQUENCE = 3 };

// ── Stav jedné zóny ─────────────────────────────────────────────
// running  = zóna je "aktivní" (od Zone_Start do konce / zastavení)
// relayOn  = relé fyzicky sepnuté; false během čekání na pre-delay master ventilu
// startMs  = okamžik sepnutí relé (může být v budoucnu, pokud se čeká na master)
struct ZoneRunState {
  bool          running;
  bool          relayOn;
  uint16_t      durationMin;
  unsigned long startMs;
  unsigned long endMs;
  RunReason     reason;
};

// ── Položka sekvenční fronty ─────────────────────────────────────
struct QueueEntry {
  uint8_t   zone;
  uint16_t  minutes;
  RunReason reason;
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
// Všechny funkce jsou thread-safe (rekurzivní mutex) — volají se z hlavní
// smyčky (core 1) i z WebServer / CloudSync tasků (core 0). Žádná z nich
// neblokuje: prodlevy master ventilu i test relé řeší stavový automat
// v Zones_Tick(), takže HTTP handlery odpovídají okamžitě.
void         Zones_Init(void);
void         Zones_Tick(void);            // volat co nejčastěji z loop()

// parallel=false (výchozí) → zastaví ostatní zóny a frontu, spustí tuto
//                            (master ventil zůstane otevřený, žádné zbytečné cyklování)
// parallel=true             → spustí vedle případně běžících zón
bool         Zone_Start(uint8_t zone, uint16_t minutes,
                        RunReason reason = RUN_MANUAL, bool parallel = false);
void         Zone_Stop(uint8_t zone);     // zastaví zónu; pokud čeká fronta, spustí další
void         Zone_StopAll(void);          // zastaví vše, vymaže frontu, ukončí test

// ── Dotazy na stav ───────────────────────────────────────────────
ZoneRunState Zone_GetState(uint8_t zone);  // 1–ZONE_COUNT
bool         Zones_AnyRunning(void);
int          Zones_RunningCount(void);

// ── Master ventil ─────────────────────────────────────────────────
void         MasterValve_Set(bool on);
bool         MasterValve_IsOpen(void);

// ── Sekvenční fronta ─────────────────────────────────────────────
bool         Queue_Add(uint8_t zone, uint16_t minutes, RunReason reason = RUN_SEQUENCE);
void         Queue_Clear(void);
int          Queue_Count(void);

// ── Test relé (neblokující — každá zóna ~3 s, postupně) ──────────
bool         Zones_StartTest(void);       // false = test už běží
bool         Zones_TestRunning(void);

// ── Log ───────────────────────────────────────────────────────────
void         Log_Add(uint8_t zone, uint16_t dur, RunReason reason, const char* note);
int          Log_Count(void);
LogEntry     Log_Get(int idx);    // 0 = nejnovější
void         Log_Clear(void);
