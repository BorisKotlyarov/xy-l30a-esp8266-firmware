#define WCM_AP_NAME "XY-L30A-Config"
#define WCM_AP_IP 192, 168, 1, 1

#ifndef WIFI_CONNECTION_MANAGER_H
#define WIFI_CONNECTION_MANAGER_H
#include <DNSServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>


#define WCM_ERROR 0
#define WCM_SUBMIT 1
#define WCM_SWITCH_AP 2
#define WCM_SWITCH_LOCAL 3
#define WCM_EXIT 4
#define WCM_TIMEOUT 5

struct WCMConfig
{
  char SSID[32] = "";
  char pass[32] = "";
  uint8_t mode = WIFI_AP;
};
extern WCMConfig wcmConfig;

void WCMStart();
void WCMStop();
bool WCMTick(); 
void WCMRun();
byte WCMStatus();

void WCM_handleConnect();
void WCM_handleExit();
#endif