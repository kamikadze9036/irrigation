#pragma once
#include <Arduino.h>
#include "config.h"
#include "storage.h"

// ── Stav zálivky ────────────────────────────────────────────────
enum RunReason { RUN_MANUAL = 0, RUN_PROGRAM = 1, RUN_TEST = 2 };

struct RunState {
  bool      running;
  uint8_t   zone;          // 1–6
  uint16_t  durationMin;
  unsigned long startMs;   // millis() při startu
  unsigned long endMs;     // millis() kdy vypnout
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

void     Zones_Init(void);
bool     Zone_Start(uint8_t zone, uint16_t minutes, RunReason reason = RUN_MANUAL);
void     Zone_Stop(void);
void     Zones_Tick(void);         // volat v loop() — hlídá timeouty
RunState Zones_GetState(void);

// Master ventil
void     MasterValve_Set(bool on);

// Log
void       Log_Add(uint8_t zone, uint16_t dur, RunReason reason, const char* note);
int        Log_Count(void);
LogEntry   Log_Get(int idx);       // 0 = nejnovější
void       Log_Clear(void);
