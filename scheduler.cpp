#include "scheduler.h"
#include "storage.h"
#include "zones.h"
#include "weather.h"
#include <time.h>

// Konvence dnů: bit0=Po, bit1=Út, bit2=St, bit3=Čt, bit4=Pá, bit5=So, bit6=Ne
// tm_wday: 0=Ne, 1=Po, ..., 6=So → bit = (wday==0) ? 6 : wday-1

void Scheduler_Tick(void) {
  // Pokud zóna běží, scheduler nespouští další
  if (Zones_GetState().running) return;

  // Čas musí být synchonizován
  struct tm t;
  if (!getLocalTime(&t)) {
    Serial.println("[SCH] Tick přeskočen — čas není synchronizován");
    return;
  }

  // Kontrola počasí
  if (Weather_ShouldSkip()) return;

  // Den v týdnu → bit
  int wday = t.tm_wday;
  int bit  = (wday == 0) ? 6 : wday - 1;  // 0=Po .. 6=Ne

  for (uint8_t z = 1; z <= ZONE_COUNT; z++) {
    ZoneConfig zc = Storage_GetZone(z);
    if (!zc.enabled) continue;

    for (int p = 0; p < MAX_PROGRAMS_PER_ZONE; p++) {
      ZoneProgram &pg = zc.programs[p];
      if (!pg.enabled)            continue;
      if (!(pg.days & (1 << bit))) continue;
      if (pg.startHour   != t.tm_hour)  continue;
      if (pg.startMinute != t.tm_min)   continue;
      if (pg.durationMin == 0)          continue;
      // Jen první sekundy minuty (ochrana proti double-trigger)
      if (t.tm_sec > 10)                continue;

      Serial.printf("[SCH] Program %d zóny %d — spouštím (%02d:%02d)\n",
        p+1, z, t.tm_hour, t.tm_min);
      Zone_Start(z, pg.durationMin, RUN_PROGRAM);
      return;  // jen jedna zóna najednou
    }
  }
}

String Scheduler_NextRunString(void) {
  const char *dayNames[] = {"Ne","Po","Út","St","Čt","Pá","So"};
  struct tm now;
  if (!getLocalTime(&now)) return "Čas není synchronizován";

  uint16_t minDiff = 0xFFFF;
  uint8_t  nextZone = 0, nextProg = 0, nextWday = 0;
  ZoneProgram bestPg = {};

  for (uint8_t z = 1; z <= ZONE_COUNT; z++) {
    ZoneConfig zc = Storage_GetZone(z);
    if (!zc.enabled) continue;
    for (int p = 0; p < MAX_PROGRAMS_PER_ZONE; p++) {
      ZoneProgram &pg = zc.programs[p];
      if (!pg.enabled || !pg.days || !pg.durationMin) continue;

      for (int d = 0; d < 7; d++) {
        int checkWday = (now.tm_wday + d) % 7;
        int checkBit  = (checkWday == 0) ? 6 : checkWday - 1;
        if (!(pg.days & (1 << checkBit))) continue;

        int diff = d * 1440 + pg.startHour * 60 + pg.startMinute
                   - (now.tm_hour * 60 + now.tm_min);
        if (diff <= 0) diff += 1440;
        if ((uint16_t)diff < minDiff) {
          minDiff   = diff;
          nextZone  = z;
          nextProg  = p;
          nextWday  = checkWday;
          bestPg    = pg;
          (void)nextProg;
        }
        break;
      }
    }
  }

  if (!nextZone) return "Žádný program nastaven";
  ZoneConfig zc = Storage_GetZone(nextZone);
  char buf[80];
  snprintf(buf, sizeof(buf), "%s — %s %02d:%02d (%d min)",
    zc.name, dayNames[nextWday], bestPg.startHour, bestPg.startMinute, bestPg.durationMin);
  return String(buf);
}
