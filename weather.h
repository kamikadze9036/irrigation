#pragma once
#include <Arduino.h>

struct WeatherData {
  float  past24hRainMm;
  float  next24hRainMm;
  float  currentTempC;
  bool   dataValid;
  time_t lastUpdate;
  char   statusMsg[72];
};

void        Weather_Init(void);
void        Weather_Update(float lat, float lon);
WeatherData Weather_GetData(void);

// true = zálivku přeskočit. Volitelně vyplní důvod (pro log). Nic neloguje
// do Serialu — volá se každých pár sekund z /api/status.
bool        Weather_ShouldSkip(char *reason = nullptr, size_t reasonLen = 0);
String      Weather_StatusString(void);
