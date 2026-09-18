#pragma once
#include <Arduino.h>

// Volat každých ~15 s z loop(). Každý naplánovaný start programu zpracuje
// právě jednou (spustí / zařadí do fronty / zaloguje přeskočení), s 5min
// catch-up oknem pro případ, že se hlavní smyčka na chvíli zablokovala.
void   Scheduler_Tick(void);
String Scheduler_NextRunString(void);   // "Okruh 2 — Pá 06:00 (20 min)" pro dashboard
