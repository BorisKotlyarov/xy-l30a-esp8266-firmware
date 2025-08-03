#ifndef WIFI_CONNECTION_MANAGER_H
#define WIFI_CONNECTION_MANAGER_H
#pragma once
#include <DNSServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

/**
 * WiFi connection manager for ESP8266.
 * Handles AP mode, captive portal, and STA configuration.
 */
class WiFiConnectionManager
{
public:
  struct Config
  {
    char SSID[32];     // Max SSID length: 32 bytes
    char password[64]; // Max password length: 64 bytes
    uint8_t mode;      // WIFI_AP or WIFI_STA
  };

  WiFiConnectionManager(
      const char *apName = "XY-LX0A-Config",
      const IPAddress &apIP = IPAddress(192, 168, 1, 1),
      const IPAddress &subnet = IPAddress(255, 255, 255, 0));

  void begin();       // Starts AP + WebServer
  void stop();        // Stops AP + WebServer
  bool process();     // Handles client requests (returns true if new config received)
  void runBlocking(); // Blocks until WiFi is configured
  byte getStatus() const;

  const Config &getConfig() const { return config; }

private:
  void handleConnect(); // Handles POST /connect
  void handleExit();    // Handles POST /exit

  DNSServer dnsServer;
  ESP8266WebServer webServer{80};
  Config config;
  bool isRunning = false;
  byte status = 0;

  const char *apName;
  const IPAddress apIP;
  const IPAddress subnet;

  // HTML page stored in PROGMEM
  static const char connectPage[] PROGMEM;
};
#endif