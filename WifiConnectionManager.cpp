#include "WifiConnectionManager.h"
static DNSServer _WCM_dnsServer;
static ESP8266WebServer _WCM_server(80);

const char WCM_connect_page[] PROGMEM = R"=====(
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
    <input type="text" name="ssid" placeholder="SSID"/>
    <input type="text" name="pass" placeholder="Pass"/>
    <input type="submit" value="Submit">
</form>
</div>
</body></html>)=====";


static bool _WCM_started = false;
static byte _WCM_status = 0;
WCMConfig wcmConfig;

void WCM_handleConnect()
{
  strcpy(wcmConfig.SSID, _WCM_server.arg("ssid").c_str());
  strcpy(wcmConfig.pass, _WCM_server.arg("pass").c_str());
  wcmConfig.mode = WIFI_STA;
  _WCM_status = 1;
}

void WCM_handleExit()
{
  _WCM_status = 4;
}

void WCMStart()
{
  WiFi.softAPdisconnect();
  WiFi.disconnect();
  IPAddress apIP(WCM_AP_IP);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, subnet);
  WiFi.softAP(WCM_AP_NAME);
  _WCM_dnsServer.start(53, "*", apIP);

  _WCM_server.onNotFound([]()
                        { _WCM_server.send(200, "text/html", WCM_connect_page); });
  _WCM_server.on("/connect", HTTP_POST, WCM_handleConnect);
  _WCM_server.on("/exit", HTTP_POST, WCM_handleExit);
  _WCM_server.begin();
  _WCM_started = true;
  _WCM_status = 0;
}

void WCMStop()
{
  WiFi.softAPdisconnect();
  _WCM_server.stop();
  _WCM_dnsServer.stop();
  _WCM_started = false;
}

bool WCMTick()
{
  if (_WCM_started)
  {
    _WCM_dnsServer.processNextRequest();
    _WCM_server.handleClient();
    yield();
    if (_WCM_status)
    {
      WCMStop();
      return 1;
    }
  }
  return 0;
}

void WCMRun()
{
  WCMStart();
  while (!WCMTick())
  {
    yield();
  }
}

byte WCMStatus()
{
  return _WCM_status;
}