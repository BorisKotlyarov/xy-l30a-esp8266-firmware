#include "esp8266withXYL30A.h"
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266WebServer.h>
#include <SoftwareSerial.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <user_interface.h>
#include <WiFiSetupManager.h>
#include "XYParser.h"
#include "config.h"
#include "HttpConfigServer.h"
#include "EEPROMConfigManager.h"

WiFiSetupManager wifiManager("XY-LXXA-Config", IPAddress(192, 168, 1, 1));
// UART for XY-L10A/XY-L30A
SoftwareSerial loraSerial(3, 1); // RX = GPIO3, TX = GPIO1
HttpConfigServer configServer(80, saveConfigToEEPROM, resetWiFiCredentials);

WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);
X509List cert(IRG_Root_X1);

char WIFI_SSID[64] = {0};
char WIFI_PASSWORD[64] = {0};
char MQTT_SERVER[64] = {0};
char MQTT_USER[64] = {0};
char MQTT_PASS[64] = {0};
char MQTT_CLIENT_ID[64] = {0};
char authUser[32] = {0};
char authPass[32] = {0};

uint16_t MQTT_PORT = 1883;

unsigned long lastMqttAttempt = 0;
const unsigned long mqttRetryInterval = 5000; // в мс
unsigned int WifiattemptReconnect = 0;
unsigned int MAX_ATTEMPT_TO_RECONNECT = 10; // arter that device will reboot

EEPROMConfigManager eeprom;

void setup()
{

  Serial.begin(115200);
  delay(1000);

  pinMode(LED_BUILTIN, OUTPUT);
  Serial.println(PSTR("=== Let's start ==="));
  delay(5000);

  eeprom.begin();
  eeprom.loadWiFiConfig(WIFI_SSID, WIFI_PASSWORD);
  loadAuthFromEepromOrUseDefault();

  if (strlen(WIFI_SSID) == 0)
  {
    initLogin();
  }
  else
  {
    connectToAP(WIFI_SSID, WIFI_PASSWORD, true);
  }

  configServer.setIsSerialDebug(IS_SERIAL_DEBUG);

  if (!IS_SERIAL_DEBUG)
  {
    loraSerial.begin(9600); // UART for XY-L10A/XY-L30A is active if NOT Serial Debug
    configServer.setLoraSerial(&loraSerial);
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println(PSTR("\nWi-Fi connected"));
  }
  else
  {
    Serial.println(PSTR("\nWi-Fi connection fail"));
    delay(100);
    ESP.restart();
    return;
  }
  espClient.setTrustAnchors(new X509List(IRG_Root_X1));
  // espClient.setInsecure(); // (DANGER!!! insecure use it only for tests)

  // Load config for
  eeprom.loadMQTTConfig(MQTT_SERVER, &MQTT_PORT, MQTT_USER, MQTT_PASS, MQTT_CLIENT_ID);

  // setup MQTTConfig to http server (for edit)
  configServer.setMQTT(
      MQTT_SERVER,
      MQTT_PORT,
      MQTT_USER,
      MQTT_PASS,
      MQTT_CLIENT_ID);

  // start the http server
  configServer.begin();

  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(callback);

  connectMQTT(true);
}

void loop()
{

  if (!wifiConnectionCheckAndRenew())
  {
    // after bad MAX_ATTEMPT_TO_RECONNECT ESP8266 will restart
    if (WifiattemptReconnect >= MAX_ATTEMPT_TO_RECONNECT)
    {
      delay(2000);
      ESP.restart();
    }

    return;
  }
  else
  {
    WifiattemptReconnect = 0; // reset attempt
  }

  configServer.loop();

  if (!IS_SERIAL_DEBUG)
  {
    // read data from XY-L10A/XY-L30A UART
    loraReader();
  }

  if (!mqttClient.connected())
  {
    configServer.setMqttConnected(false);
    connectMQTT(false);
  }
  else
  {
    configServer.setMqttConnected(true);
    mqttClient.loop();
    publishStatus();
  }
}

// Each 5 sec send the status to MQTT server
void publishStatus()
{
  static unsigned long lastMqttStatusTime = 0;
  const unsigned long statusInterval = 5000;

  unsigned long now = millis();
  if (now - lastMqttStatusTime < statusInterval)
    return;
  lastMqttStatusTime = now;

  // create uptime
  unsigned long uptimeSec = millis() / 1000;
  int hrs = uptimeSec / 3600;
  int min = (uptimeSec % 3600) / 60;
  int sec = uptimeSec % 60;

  char uptimeStr[12];
  snprintf_P(uptimeStr, sizeof(uptimeStr), PSTR("%02d:%02d:%02d"), hrs, min, sec);

  // format IP
  const IPAddress &ip = WiFi.localIP();
  char ipStr[15];
  snprintf_P(ipStr, sizeof(ipStr), PSTR("%u.%u.%u.%u"),
             ip[0], ip[1], ip[2], ip[3]);

  StaticJsonDocument<192> doc;
  doc["status"] = "online";
  doc["ip"] = ipStr;
  doc["rssi"] = WiFi.RSSI();
  doc["uptime"] = uptimeStr;
  doc["device_id"] = MQTT_CLIENT_ID;

  char jsonOut[128] = {0};
  serializeJson(doc, jsonOut, sizeof(jsonOut));

  mqttClient.publish("device/status", jsonOut, MQTT_RETAIN);
}

// reset wifi ssid and wifi password
void resetWiFiCredentials()
{

  eeprom.resetWiFiCredentials();
  WiFi.disconnect(true);
  delay(100);

  WiFi.mode(WIFI_OFF);
  delay(1000);

  ESP.restart();
}

void blink(int _delay = 500, int num = 1)
{
  for (int i = 0; i < num; i++)
  {
    digitalWrite(LED_BUILTIN, LOW); // Turn on (LOW — active level for ESP)
    delay(_delay);
    digitalWrite(LED_BUILTIN, HIGH); // Turn oof
    delay(_delay);
  }
}

// check the wifi connection if it is lose function will try to reconnect
bool wifiConnectionCheckAndRenew()
{
  static unsigned long lastWiFiRetry = 0;
  const unsigned long wifiRetryInterval = 5000;

  if (WiFi.status() != WL_CONNECTED)
  {
    unsigned long _now = millis();
    if (_now - lastWiFiRetry > wifiRetryInterval)
    {
      // Wi-Fi is lose try to reconnect
      WiFi.reconnect();
      lastWiFiRetry = _now;
      blink(50);
    }
    return false;
  }

  return true;
}

// start wifi (as WIFI_AP mode) and
// start local http server for provide posability
// to add credentials for wifi router
void initLogin()
{
  while (true)
  {
    wifiManager.runBlocking();
    byte status = wifiManager.getStatus();

    if (status == 1)
    { // has data (SSID & password), try to coonect
      const WiFiSetupManager::Config &config = wifiManager.getConfig();
      connectToAP(config.SSID, config.password, true);

      if (WiFi.status() == WL_CONNECTED)
      {
        // if data (SSID & password) is correct. then save it to eeprom
        eeprom.saveWiFiConfig(config.SSID, config.password);
        Serial.println("Wi-Fi saved!");
        break;
      }
      else
      {
        Serial.println("Connection failed! Retrying...");
      }
    }
    else if (status == 4)
    {
      Serial.println("Setup canceled.");
      break;
    }
  }
}

void connectToAP(const char *ssid, const char *pass, bool isCheckAttempt = false)
{
  if (WiFi.status() == WL_CONNECTED)
  {
    return;
  }
  int tryCount = 0;
  int ATTEMPT = 1000; // each ATTEMPT == 1000ms

  // Отримуємо MAC-адресу
  uint8_t mac[6];       // MAC (6 bytes)
  WiFi.macAddress(mac); // Записуємо MAC у масив

  Serial.print(F("MAC-address: "));
  // format to XX:XX:XX:XX:XX:XX
  Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);



  // Connect to Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.hostname(WIFI_HOSTNAME);
  WiFi.begin(ssid, pass);

  while (true)
  {
    wl_status_t status = WiFi.status();
    if (status == WL_CONNECTED)
    {

      break;
    }
    blink(50);
    Serial.print('.');
    if (isCheckAttempt)
    {
      tryCount++;
      if (status == WL_CONNECT_FAILED || tryCount >= ATTEMPT)
      {
        return;
      }
    }
    delay(1000);
  }
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  // get correct time
  configTime(3 * 3600, 0, "pool.ntp.org", "time.nist.gov");

  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2)
  {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }

  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  Serial.println(asctime(&timeinfo));

}

void loadAuthFromEepromOrUseDefault()
{
  eeprom.loadAuth(authUser, authPass);

  if (strlen(authUser) == 0 || !isAscii(authUser[0]))
  {
    strncpy(authUser, DEFAULT_USER, sizeof(authUser));
  }
  if (strlen(authPass) == 0 || !isAscii(authPass[0]))
  {
    strncpy(authPass, DEFAULT_PASS, sizeof(authPass));
  }

  configServer.setAuth(authUser, authPass);
}

void saveConfigToEEPROM(const char *mqtt_ip, const char *mqtt_port,
                        const char *user, const char *mqtt_pass,
                        const char *client_id,
                        const char *auth_user, const char *auth_pass)
{

  uint16_t port = atoi(mqtt_port);
  eeprom.saveMQTTConfig(mqtt_ip, port, user, mqtt_pass, client_id);
  eeprom.saveAuth(auth_user, auth_pass);
}

void connectMQTT(bool force = false)
{

  char willTopic[32];
  char commandTopic[32];
  strncpy_P(willTopic, STATUS_TOPIC, sizeof(willTopic));
  strncpy_P(commandTopic, COMMAND_TOPIC, sizeof(commandTopic));

  if (!force && (strlen(MQTT_SERVER) == 0 ||
                 WiFi.status() != WL_CONNECTED ||
                 millis() - lastMqttAttempt < mqttRetryInterval))
  {
    return;
  }

  lastMqttAttempt = millis();
  Serial.println(F("MQTT connect..."));
  blink(100, 3);

  // Create Last Will using template
  char willPayload[128];
  snprintf_P(willPayload, sizeof(willPayload),
             LAST_WILL_JSON,
             OFFLINE_STATUS,
             MQTT_CLIENT_ID);

  // connect to Mqtt
  if (mqttClient.connect(
          MQTT_CLIENT_ID,
          MQTT_USER,
          MQTT_PASS,
          willTopic,
          MQTT_QOS,
          MQTT_RETAIN,
          willPayload))
  {
    // MQTT Connected is connected
    configServer.setMqttConnected(true);
    // subscribe to topic
    mqttClient.subscribe(commandTopic);
  }
  else
  {
    // MQTT ERROR:
    Serial.print(F("❌ MQTT ERROR: "));
    Serial.println(mqttClient.state());
    configServer.setMqttConnected(false);
  }
}

void callback(char *topic, byte *payload, unsigned int length)
{
  char logBuffer[128];
  char formatBuffer[64];

  // 1. JSON parse
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, payload, length);

  if (error)
  {
    strncpy_P(formatBuffer, MSG_JSON_ERROR, sizeof(formatBuffer));
    snprintf(logBuffer, sizeof(logBuffer), formatBuffer, error.c_str());
    Serial.println(logBuffer);
    return;
  }

  // 2. data extract
  const char *action = doc["action"];
  const char *value = doc["value"];
  const char *device_id = doc["receiver"];

  // 3. check device_id
  strncpy_P(formatBuffer, MSG_DEVICE_ID, sizeof(formatBuffer));
  snprintf(logBuffer, sizeof(logBuffer), formatBuffer,
           device_id ? device_id : "null", MQTT_CLIENT_ID);
  Serial.println(logBuffer);

  if (device_id && strcmp(device_id, MQTT_CLIENT_ID) == 0)
  {
    // 4. log command
    strncpy_P(formatBuffer, MSG_MQTT_CMD, sizeof(formatBuffer));
    snprintf(logBuffer, sizeof(logBuffer), formatBuffer,
             action ? action : "null", value ? value : "null");
    Serial.println(logBuffer);

    // 5. handle command
    handleMQTTCommand(action, value);
  }
}

void handleMQTTCommand(const char *action, const char *value)
{
  if (!action)
    return;

  char logBuffer[64];

  if (strcmp(action, "restart") == 0)
  {
    ESP.restart();
  }
  else if (strcmp(action, "blink") == 0 && value)
  {
    blink(200, atoi(value));
  }
  else if (strcmp(action, "uart_send") == 0 && value)
  {
    loraSerial.print(value);
  }
  else if (strcmp(action, "reset_wifi") == 0)
  {
    resetWiFiCredentials();
  }
  else
  {
    strncpy_P(logBuffer, MSG_UNKNOWN_CMD, sizeof(logBuffer));
    snprintf(logBuffer, sizeof(logBuffer), logBuffer, action);
    Serial.println(logBuffer);
  }
}

void loraReader()
{
  static char loraBuffer[64];
  static byte index = 0;

  while (loraSerial.available())
  {
    char c = loraSerial.read();
    if (c == '\n')
    {
      loraBuffer[index] = '\0';
      if (index > 0)
        handleXYResponse(loraBuffer);
      index = 0;
    }
    else if (index < sizeof(loraBuffer) - 1)
    {
      loraBuffer[index++] = c;
    }
    else
    {
      index = 0;
    }
    yield();
  }
}

void handleXYResponse(const char *rawLine)
{

  if (!mqttClient.connected())
  {
    return;
  }

  char jsonBuffer[256] = {0};
  char JsonTypeData[] = "data";
  char JsonTypeConfig[] = "config";
  char JsonTypeRaw[] = "raw";

  XYPacket packet;

  // data parsing
  if (XYParser::parse(rawLine, packet))
  {

    char timeStr[6] = {0};
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", packet.hours, packet.minutes);

    StaticJsonDocument<192> doc;
    doc["type"] = JsonTypeData;
    doc["voltage"] = packet.voltage;
    doc["percent"] = packet.percent;
    doc["time"] = timeStr;
    doc["state"] = packet.state;
    doc["device_id"] = MQTT_CLIENT_ID;

    serializeJson(doc, jsonBuffer, sizeof(jsonBuffer));

    char topic[32];
    strncpy_P(topic, TOPIC_XY_DATA, sizeof(topic));
    mqttClient.publish(topic, jsonBuffer);

    return;
  }

  // config parsing
  StaticJsonDocument<256> doc;
  doc["type"] = JsonTypeConfig;
  doc["device_id"] = MQTT_CLIENT_ID;
  JsonObject params = doc.createNestedObject("params");

  const char *knownKeys[] = {"dw", "up", "th", "st", "et", nullptr};
  char buffer[64];
  strncpy(buffer, rawLine, sizeof(buffer) - 1);
  buffer[sizeof(buffer) - 1] = '\0';

  char *token = strtok(buffer, ",");
  bool hasAny = false;

  while (token)
  {
    bool matched = false;
    for (int i = 0; knownKeys[i]; ++i)
    {
      size_t len = strlen(knownKeys[i]);
      if (strncmp(token, knownKeys[i], len) == 0)
      {
        const char *value = token + len;
        params[knownKeys[i]] = value;
        matched = true;
        hasAny = true;
        break;
      }
    }

    if (!matched && strchr(token, ':') && strlen(token) <= 5)
    {
      params["timer"] = token;
      hasAny = true;
    }

    token = strtok(nullptr, ",");
  }

  if (hasAny)
  {
    char jsonBufferConf[256] = {0};
    char topic[32];
    strncpy_P(topic, TOPIC_XY_CONFIG, sizeof(topic));
    serializeJson(doc, jsonBufferConf, sizeof(jsonBufferConf));
    mqttClient.publish(topic, jsonBufferConf);
  }
  else
  {
    StaticJsonDocument<128> rawDoc;
    rawDoc["type"] = JsonTypeRaw;
    rawDoc["line"] = rawLine;
    rawDoc["device_id"] = MQTT_CLIENT_ID;
    char jsonBufferRaw[256] = {0};
    serializeJson(rawDoc, jsonBufferRaw, sizeof(jsonBufferRaw));
    char topic[32];
    strncpy_P(topic, TOPIC_XY_RAW, sizeof(topic));
    mqttClient.publish(topic, jsonBufferRaw);
  }
}
