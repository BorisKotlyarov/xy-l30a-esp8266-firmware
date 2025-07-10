#include "WifiConnectionManager.h"
static DNSServer _SP_dnsServer;
#ifdef ESP8266
static ESP8266WebServer _SP_server(80);
#else
static WebServer _SP_server(80);
#endif

const char SP_connect_page[] PROGMEM = R"rawliteral(
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
</body></html>)rawliteral";

const char SP_data_received[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head>
<meta name="viewport" content="width=device-width, initial-scale=1">
</head><body>
<style type="text/css">.container {max-width:300px;margin:0 auto;}</style>
<div class="container"><h3>Data Received</h3></div>
</body></html>)rawliteral";

static bool _SP_started = false;
static byte _SP_status = 0;
PortalCfg portalCfg;

void SP_handleConnect()
{
  strcpy(portalCfg.SSID, _SP_server.arg("ssid").c_str());
  strcpy(portalCfg.pass, _SP_server.arg("pass").c_str());
  // strcpy(portalCfg.address, _SP_server.arg("address").c_str());
  _SP_server.send(204, "text/html", SP_data_received);
  portalCfg.mode = WIFI_STA;
  _SP_status = 1;
}

void SP_handleExit()
{
  _SP_status = 4;
}

void portalStart()
{
  WiFi.softAPdisconnect();
  WiFi.disconnect();
  IPAddress apIP(SP_AP_IP);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, subnet);
  WiFi.softAP(SP_AP_NAME);
  _SP_dnsServer.start(53, "*", apIP);

  _SP_server.onNotFound([]()
                        { _SP_server.send(200, "text/html", SP_connect_page); });
  _SP_server.on("/connect", HTTP_POST, SP_handleConnect);
  _SP_server.on("/exit", HTTP_POST, SP_handleExit);
  _SP_server.begin();
  _SP_started = true;
  _SP_status = 0;
}

void portalStop()
{
  WiFi.softAPdisconnect();
  _SP_server.stop();
  _SP_dnsServer.stop();
  _SP_started = false;
}

bool portalTick()
{
  if (_SP_started)
  {
    _SP_dnsServer.processNextRequest();
    _SP_server.handleClient();
    yield();
    if (_SP_status)
    {
      portalStop();
      return 1;
    }
  }
  return 0;
}

void portalRun(uint32_t prd)
{
  uint32_t tmr = millis();
  portalStart();
  while (!portalTick())
  {
    if (millis() - tmr > prd)
    {
      _SP_status = 5;
      portalStop();
      break;
    }
    yield();
  }
}

void portalRunInf()
{
  portalStart();
  while (!portalTick())
  {
    yield();
  }
}

byte portalStatus()
{
  return _SP_status;
}