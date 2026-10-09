#pragma once
#include <Arduino.h>
#include <WebServer.h>

void WebUI_Init(void);     // registrace routes + server.begin()
void WebUI_Handle(void);   // volat opakovaně z vlastního FreeRTOS tasku (core 0, viz irrigation.ino)

// Zavolá API handler přímo, bez HTTP — pro cloud tunel (cloud_sync.cpp).
// Vrací false, když cesta/metoda neexistuje (status/out pak nesou 404).
// restartAfter=true → volající má po odeslání odpovědi zavolat ESP.restart().
bool WebUI_Dispatch(const String &method, const String &path, const String &body,
                    int &status, String &out, bool &restartAfter);
