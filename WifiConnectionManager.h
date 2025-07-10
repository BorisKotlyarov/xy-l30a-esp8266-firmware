/*
    Простой менеджер WiFi для esp8266 для задания логина-пароля WiFi
    GitHub: https://github.com/BorisKotlyarov/WifiConnectionManager

    forked from (AlexGyver, alex@alexgyver.ru)
    MIT License
*/

#define SP_AP_NAME "XY-L30A-Config" // название точки
#define SP_AP_IP 192, 168, 1, 1     // IP точки

#ifndef _SimplePortal_h
#define _SimplePortal_h
#include <DNSServer.h>
#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#else
#include <WiFi.h>
#include <WebServer.h>
#endif

#define SP_ERROR 0
#define SP_SUBMIT 1
#define SP_SWITCH_AP 2
#define SP_SWITCH_LOCAL 3
#define SP_EXIT 4
#define SP_TIMEOUT 5

struct PortalCfg
{
  char SSID[32] = "";
  char pass[32] = "";
  // char address[32] = "";
  uint8_t mode = WIFI_AP; // (1 WIFI_STA, 2 WIFI_AP)
};
extern PortalCfg portalCfg;

void portalStart();                   // запустить портал
void portalStop();                    // остановить портал
bool portalTick();                    // вызывать в цикле
void portalRun(uint32_t prd = 60000); // блокирующий вызов
void portalRunInf();                  // блокирующий бесконечный вызов
byte portalStatus();                  // статус: 1 connect, 2 ap, 3 local, 4 exit, 5 timeout

void SP_handleConnect();
void SP_handleExit();
#endif