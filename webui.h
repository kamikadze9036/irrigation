#pragma once
#include <Arduino.h>
#include <WebServer.h>

void WebUI_Init(void);     // registrace routes + server.begin()
void WebUI_Handle(void);   // volat opakovaně z vlastního FreeRTOS tasku (core 0, viz irrigation.ino)
