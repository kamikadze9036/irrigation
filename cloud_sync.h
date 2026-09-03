#pragma once
#include <Arduino.h>

// Vzdálený přístup přes cloud relay (viz /cloud). Volitelné — pokud
// CLOUD_BASE_URL v config.h zůstane prázdné, obě funkce jsou no-op.
void CloudSync_Init(void);
void CloudSync_Tick(void);   // volat opakovaně z vlastního FreeRTOS tasku (blokující HTTP)
