#pragma once
#include <Arduino.h>

void   Scheduler_Tick(void);         // volat každou minutu
String Scheduler_NextRunString(void);
