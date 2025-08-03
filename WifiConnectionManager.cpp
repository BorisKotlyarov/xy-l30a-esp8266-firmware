#include "WiFiConnectionManager.h"

const char WiFiConnectionManager::connectPage[] PROGMEM = R"=====(
<!DOCTYPE HTML><html><head>
<meta name="viewport" content="width=device-width, initial-scale=1">
</head><body>
<style type="text/css">
    input[type="text"] {margin-bottom:8px;font-size:20px;}
    input[type="submit"] {width:180px; height:60px;margin-bottom:8px;font-size:20px;}
    .container {max-width:300px;margin:0 auto;}
</style>
<div class="container">
<h3>WiFi settings</h3>
<form action="/connect" method="POST">
    <input type="text" name="ssid" placeholder="SSID" maxlength="31"/>
    <input type="text" name="pass" placeholder="Password" maxlength="63"/>
    <input type="submit" value="Submit">
</form>
</div>
</body></html>
)=====";

WiFiConnectionManager::WiFiConnectionManager(const char *apName, const IPAddress &apIP, const IPAddress &subnet)
    : apName(apName), apIP(apIP), subnet(subnet) {}

void WiFiConnectionManager::begin()
{
  WiFi.softAPdisconnect();
  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, subnet);
  WiFi.softAP(apName);

  dnsServer.start(53, "*", apIP);

  webServer.onNotFound([this]()
                       { webServer.send(200, "text/html", FPSTR(connectPage)); });

  webServer.on("/connect", HTTP_POST, [this]()
               { handleConnect(); });
  webServer.on("/exit", HTTP_POST, [this]()
               { handleExit(); });

  webServer.begin();
  isRunning = true;
  status = 0;
}

void WiFiConnectionManager::stop()
{
  dnsServer.stop();
  webServer.stop();
  WiFi.softAPdisconnect();
  isRunning = false;
}

bool WiFiConnectionManager::process()
{
  if (!isRunning)
    return false;

  dnsServer.processNextRequest();
  webServer.handleClient();
  yield();

  if (status == 1)
  { // New config received
    stop();
    return true;
  }
  return false;
}

void WiFiConnectionManager::runBlocking()
{
  begin();
  while (!process())
  {
    yield();
  }
}

byte WiFiConnectionManager::getStatus() const
{
  return status;
}

// --- Private methods ---
void WiFiConnectionManager::handleConnect()
{
  strncpy(config.SSID, webServer.arg("ssid").c_str(), sizeof(config.SSID) - 1);
  strncpy(config.password, webServer.arg("pass").c_str(), sizeof(config.password) - 1);
  config.SSID[sizeof(config.SSID) - 1] = '\0';
  config.password[sizeof(config.password) - 1] = '\0';
  config.mode = WIFI_STA;
  status = 1;
}

void WiFiConnectionManager::handleExit()
{
  status = 4; // Exit requested
}