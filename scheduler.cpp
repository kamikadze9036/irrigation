#include "scheduler.h"
#include "storage.h"
#include "zones.h"
#include "weather.h"
#include <time.h>

// Konvence dnů: bit0=Po, bit1=Út, bit2=St, bit3=Čt, bit4=Pá, bit5=So, bit6=Ne
// tm_wday: 0=Ne, 1=Po, ..., 6=So → bit = (wday==0) ? 6 : wday-1

// Jak dlouho po naplánovaném čase ještě program spustíme. Hlavní smyčka se
// umí na desítky sekund zablokovat (WiFi reconnect, NTP, počasí), takže
// přesná shoda minuty nestačí — spuštění by se minulo.
#define SCHED_CATCHUP_S  300

// Pro každý program si pamatujeme naplánovaný start (unix čas), který jsme
// naposledy zpracovali — spustili, zařadili do fronty nebo přeskočili.
// Start je pro každý den unikátní, takže program se nikdy nespustí dvakrát
// a zároveň se druhý den normálně spustí znovu (dřívější varianta
// porovnávala jen HH:MM a denní program tak proběhl jen jednou).
static time_t _handled[ZONE_COUNT + 1][MAX_PROGRAMS_PER_ZONE] = {};

static inline int wdayToBit(int wday) { return (wday == 0) ? 6 : wday - 1; }

void Scheduler_Tick(void) {
  struct tm t;
  if (!getLocalTime(&t, 0)) {   // 0 = neblokovat, když čas není synchronizován
    static unsigned long lastWarn = 0;
    if (millis() - lastWarn > 600000UL) {   // varovat jen jednou za 10 min
      lastWarn = millis();
      Serial.println("[SCH] Tick přeskočen — čas není synchronizován");
    }
    return;
  }
  time_t nowT = time(nullptr);

  // Půlnoc dnešního dne v lokálním čase (tm_isdst=-1 → mktime vyřeší DST)
  struct tm mid = t;
  mid.tm_hour = mid.tm_min = mid.tm_sec = 0;
  mid.tm_isdst = -1;
  time_t midnight = mktime(&mid);

  // Pauza (dovolená mód)
  time_t pauseUntil = Storage_GetPauseUntil();
  bool paused = (pauseUntil > 0 && nowT < pauseUntil);
  if (pauseUntil > 0 && !paused) {
    Storage_SetPauseUntil(0);
    Serial.println("[SCH] Pauza zálivky skončila — obnovuji normální provoz");
  }

  int  bit = wdayToBit(t.tm_wday);
  bool weatherChecked = false, weatherSkip = false;
  char weatherReason[48] = "";

  for (uint8_t z = 1; z <= ZONE_COUNT; z++) {
    ZoneConfig zc = Storage_GetZone(z);
    if (!zc.enabled) continue;

    for (int p = 0; p < MAX_PROGRAMS_PER_ZONE; p++) {
      const ZoneProgram &pg = zc.programs[p];
      if (!pg.enabled || !(pg.days & (1 << bit)) || pg.durationMin == 0) continue;

      time_t sched = midnight + pg.startHour * 3600L + pg.startMinute * 60L;
      if (nowT < sched || nowT - sched >= SCHED_CATCHUP_S) continue;   // mimo okno
      if (_handled[z][p] == sched) continue;                           // už zpracováno
      _handled[z][p] = sched;

      if (paused) {
        Serial.printf("[SCH] Program %d zóny %d přeskočen — pauza zálivky\n", p + 1, z);
        Log_Add(z, pg.durationMin, RUN_PROGRAM, "Přeskočeno — pauza zálivky");
        continue;
      }
      if (!weatherChecked) {
        weatherChecked = true;
        weatherSkip = Weather_ShouldSkip(weatherReason, sizeof(weatherReason));
      }
      if (weatherSkip) {
        Serial.printf("[SCH] Program %d zóny %d přeskočen — %s\n", p + 1, z, weatherReason);
        Log_Add(z, pg.durationMin, RUN_PROGRAM, weatherReason);
        continue;
      }

      // Něco už běží (jiný program, manuální zálivka, test) → zařadit za něj,
      // místo tichého zahození programu.
      if (Zones_AnyRunning() || Queue_Count() > 0 || Zones_TestRunning()) {
        Serial.printf("[SCH] Program %d zóny %d — zařazen do fronty (%02d:%02d)\n",
                      p + 1, z, pg.startHour, pg.startMinute);
        if (Queue_Add(z, pg.durationMin, RUN_PROGRAM))
          Log_Add(z, pg.durationMin, RUN_PROGRAM, "Zařazeno do fronty");
        else
          Log_Add(z, pg.durationMin, RUN_PROGRAM, "Fronta plná — přeskočeno");
        continue;
      }

      Serial.printf("[SCH] Program %d zóny %d — spouštím (%02d:%02d, sekunda %d)\n",
                    p + 1, z, pg.startHour, pg.startMinute, t.tm_sec);
      Zone_Start(z, pg.durationMin, RUN_PROGRAM, false);
    }
  }
}

String Scheduler_NextRunString(void) {
  const char *dayNames[] = {"Ne","Po","Út","St","Čt","Pá","So"};
  struct tm now;
  if (!getLocalTime(&now, 0)) return "Čas není synchronizován";

  int nowMin = now.tm_hour * 60 + now.tm_min;
  long bestDiff = -1;
  uint8_t nextZone = 0, nextWday = 0;
  ZoneProgram bestPg = {};

  for (uint8_t z = 1; z <= ZONE_COUNT; z++) {
    ZoneConfig zc = Storage_GetZone(z);
    if (!zc.enabled) continue;
    for (int p = 0; p < MAX_PROGRAMS_PER_ZONE; p++) {
      const ZoneProgram &pg = zc.programs[p];
      if (!pg.enabled || !pg.days || !pg.durationMin) continue;

      // Projdi dnešek + 7 dní dopředu a vezmi první povolený den, jehož
      // start je ještě v budoucnosti (d=7 pokryje "dnes, ale už proběhlo").
      for (int d = 0; d <= 7; d++) {
        int checkWday = (now.tm_wday + d) % 7;
        if (!(pg.days & (1 << wdayToBit(checkWday)))) continue;
        long diff = d * 1440L + pg.startHour * 60 + pg.startMinute - nowMin;
        if (diff <= 0) continue;
        if (bestDiff < 0 || diff < bestDiff) {
          bestDiff = diff;
          nextZone = z;
          nextWday = checkWday;
          bestPg   = pg;
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
