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
bool        Weather_ShouldSkip(void);
String      Weather_StatusString(void);
