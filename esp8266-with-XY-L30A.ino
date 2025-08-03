#include "esp8266withXYL30A.h"
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <SoftwareSerial.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <user_interface.h>
#include "XYParser.h"
#include "WifiConnectionManager.h"
#include "config.h"
#include "HttpConfigServer.h"

#define IS_SERIAL_DEBUG true // включён режим отладки, UART (if==true:  loraSerial НЕ инициализируется)
// UART для XY-L30A
SoftwareSerial loraSerial(3, 1); // RX = GPIO3, TX = GPIO1
HttpConfigServer configServer(80, saveConfigToEEPROM, resetWiFiCredentials);

WiFiClient espClient;
PubSubClient mqttClient(espClient);

char WIFI_SSID[64] = {0};
char WIFI_PASSWORD[64] = {0};
char MQTT_SERVER[64] = {0};
char MQTT_USER[64] = {0};
char MQTT_PASS[64] = {0};
char MQTT_CLIENT_ID[64] = {0};
char authUser[32] = {0};
char authPass[32] = {0};

uint16_t MQTT_PORT = 1883;

// Прототипы
// prototypes.h

unsigned long lastMqttAttempt = 0;
const unsigned long mqttRetryInterval = 5000; // в мс
unsigned int WifiattemptReconnect = 0;
unsigned int MAX_ATTEMPT_TO_RECONNECT = 10; // arter that device will reboot

const int EEPROM_SIZE = 512;

const int OFFSET_WIFI_SSID = 0;
const int OFFSET_WIFI_PASS = 64;
const int OFFSET_MQTT_SERVER = 128;
const int OFFSET_MQTT_PORT = 192;
const int OFFSET_MQTT_USER = 200;
const int OFFSET_MQTT_PASS = 264;
const int OFFSET_MQTT_CLIENT_ID = 328;
const int OFFSET_AUTH_USER = 448;
const int OFFSET_AUTH_PASS = 480;

void setup()
{
  // debugHeap("begin setup");
  Serial.begin(115200);
  delay(1000);

  pinMode(LED_BUILTIN, OUTPUT);
  Serial.println(PSTR("=== Let's start ==="));

  EEPROM.begin(EEPROM_SIZE);

  // Чтение конфигурации
  readStringFromEEPROM(OFFSET_WIFI_SSID, WIFI_SSID, sizeof(WIFI_SSID));
  readStringFromEEPROM(OFFSET_WIFI_PASS, WIFI_PASSWORD, sizeof(WIFI_PASSWORD));
  loadAuthFromEEPROM();

  if (strlen(WIFI_SSID) == 0)
  {
    Serial.println("SSID not found. Starting Wireless Connection Manager");
    initLogin();
  }
  else
  {
    Serial.println("Begin connect to wifi before connectToAP");
    connectToAP(WIFI_SSID, WIFI_PASSWORD, true);
    Serial.println("After connectToAP");
    delay(100);
  }

  Serial.println("Before configServer.setIsSerialDebug");
  delay(100);
  configServer.setIsSerialDebug(IS_SERIAL_DEBUG);
  Serial.println("after configServer.setIsSerialDebug before condition");
  delay(100);

  if (!IS_SERIAL_DEBUG)
  {
    Serial.print(PSTR("loraSerial turn on"));
    delay(100);
    loraSerial.begin(9600); // UART для XY-L30A/XY-L10A активен, если НЕ Serial Debug
    Serial.print(PSTR("loraSerial begin"));
    delay(3000);
    configServer.setLoraSerial(&loraSerial);
    Serial.print(PSTR("after  configServer.setLoraSerial"));
    delay(100);
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\nWi-Fi подключен");
    delay(100);
    Serial.print("IP ESP: ");
    Serial.println(WiFi.localIP());
    delay(100);
  }
  else
  {
    Serial.println("\nWi-Fi не удалось подключиться");
    delay(100);
    // Можешь решать: перезапустить, ждать в loop и пытаться повторно
    ESP.restart();
    return;
  }

  loadConfigFromEEPROM();

  configServer.begin();

  Serial.print("MQTT IP: ");
  Serial.println(MQTT_SERVER);

  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(callback);
  Serial.print("MQTT PORT: ");
  Serial.println(MQTT_PORT);

  connectMQTT(true);
  // debugHeap("after setup");
}

void loop()
{

  if (!wifiConnectionCheckAndRenew())
  {
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
    loraReader();
  }

  if (!mqttClient.connected())
  {
    connectMQTT(false);
    configServer.setMqttConnected(false);
  }
  else
  {
    configServer.setMqttConnected(true);
    mqttClient.loop();
    publishStatus();
  }
}

/** Optimazed */
void publishStatus()
{
  static unsigned long lastMqttStatusTime = 0;
  const unsigned long statusInterval = 5000;

  unsigned long now = millis();
  if (now - lastMqttStatusTime < statusInterval)
    return;
  lastMqttStatusTime = now;

  // Формируем uptime
  unsigned long uptimeSec = millis() / 1000;
  int hrs = uptimeSec / 3600;
  int min = (uptimeSec % 3600) / 60;
  int sec = uptimeSec % 60;

  char uptimeStr[12];
  snprintf_P(uptimeStr, sizeof(uptimeStr), PSTR("%02d:%02d:%02d"), hrs, min, sec);

  // Формируем IP
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

void resetWiFiCredentials()
{
  Serial.println("Сброс Wi-Fi конфигурации...");

  // Очистим EEPROM области, где хранятся SSID и пароль
  for (int i = OFFSET_WIFI_SSID; i < OFFSET_WIFI_PASS + 64; i++)
  {
    EEPROM.write(i, 0); // Обнуляем байты
  }
  EEPROM.commit();

  // Отключаемся от сети и сбрасываем авто-подключение
  WiFi.disconnect(true); // true = сброс и сохранения
  delay(100);

  WiFi.mode(WIFI_OFF);
  delay(100);

  Serial.println("Wi-Fi конфигурация сброшена");

  // Перезагрузим для применения
  ESP.restart();
}

void blink(int _delay = 500, int num = 1)
{
  for (int i = 0; i < num; i++)
  {
    digitalWrite(LED_BUILTIN, LOW); // Включить (LOW — активный уровень на ESP)
    delay(_delay);
    digitalWrite(LED_BUILTIN, HIGH); // Выключить
    delay(_delay);
  }
}

bool wifiConnectionCheckAndRenew()
{
  static unsigned long lastWiFiRetry = 0;
  const unsigned long wifiRetryInterval = 5000;

  if (WiFi.status() != WL_CONNECTED)
  {
    unsigned long _now = millis();
    if (_now - lastWiFiRetry > wifiRetryInterval)
    {
      Serial.println("🔄 Wi-Fi потерян. Переподключение...");
      WiFi.reconnect();
      lastWiFiRetry = _now;
      blink(50);
    }
    return false;
  }

  return true;
}

void saveStringToEEPROM(int addr, const String &value)
{
  Serial.print("💾 Save to EEPROM @");
  Serial.print(addr);
  Serial.print(": [");
  Serial.print(value);
  Serial.println("]");

  for (int i = 0; i < value.length(); ++i)
  {
    EEPROM.write(addr + i, value[i]);
  }
  EEPROM.write(addr + value.length(), '\0'); // завершающий 0
  EEPROM.commit();
}

void readStringFromEEPROM(int addr, char *buffer, size_t maxLen)
{
  int i = 0;
  char ch;
  while ((ch = EEPROM.read(addr + i)) != '\0' && i < maxLen - 1)
  {
    buffer[i++] = ch;
  }
  buffer[i] = '\0';
}

// Запись char[] в EEPROM
void saveStringToEEPROM(int addr, const char *value)
{
  Serial.print("💾 Save to EEPROM @");
  Serial.print(addr);
  Serial.print(": [");
  Serial.print(value);
  Serial.println("]");

  int i = 0;
  while (value[i] != '\0' && i < 63)
  {
    EEPROM.write(addr + i, value[i]);
    i++;
  }
  EEPROM.write(addr + i, '\0');
  EEPROM.commit();
}

void initLogin()
{
  WCMRun();

  Serial.println(WCMStatus());

  if (WCMStatus() == WCM_SUBMIT)
  {
    connectToAP(wcmConfig.SSID, wcmConfig.pass, true);

    if (WiFi.status() != WL_CONNECTED)
    {
      initLogin();
    }
    else if (WiFi.status() == WL_CONNECTED)
    {
      saveWiFiConfig(wcmConfig.SSID, wcmConfig.pass);
      Serial.println("Wi-Fi saved");
      return;
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
  int ATTEMPT = 100; // each ATTEMPT == 1000ms

  // Connect to Wi-Fi
  WiFi.mode(WIFI_STA);
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

void saveWiFiConfig(const String &ssid, const String &pass)
{
  saveStringToEEPROM(OFFSET_WIFI_SSID, ssid);
  saveStringToEEPROM(OFFSET_WIFI_PASS, pass);
}

void loadAuthFromEEPROM()
{
  EEPROM.begin(512);

  readStringFromEEPROM(OFFSET_AUTH_USER, authUser, sizeof(authUser));
  readStringFromEEPROM(OFFSET_AUTH_PASS, authPass, sizeof(authPass));

  if (strlen(authUser) == 0 || !isAscii(authUser[0]))
  {
    strncpy(authUser, DEFAULT_USER, sizeof(authUser));
  }
  if (strlen(authPass) == 0 || !isAscii(authPass[0]))
  {
    strncpy(authPass, DEFAULT_PASS, sizeof(authPass));
  }

  Serial.println(F("🔐 Авторизация:"));
  Serial.print(F("User: ["));
  Serial.print(authUser);
  Serial.println(F("]"));

  // Временное решение для совместимости:
  configServer.setAuth(authUser, authPass);
}

void loadConfigFromEEPROM()
{
  EEPROM.begin(512);

  readStringFromEEPROM(OFFSET_WIFI_SSID, WIFI_SSID, sizeof(WIFI_SSID));
  readStringFromEEPROM(OFFSET_WIFI_PASS, WIFI_PASSWORD, sizeof(WIFI_PASSWORD));
  readStringFromEEPROM(OFFSET_MQTT_SERVER, MQTT_SERVER, sizeof(MQTT_SERVER));

  char portStr[6] = {0};
  readStringFromEEPROM(OFFSET_MQTT_PORT, portStr, sizeof(portStr));
  MQTT_PORT = atoi(portStr);

  readStringFromEEPROM(OFFSET_MQTT_USER, MQTT_USER, sizeof(MQTT_USER));
  readStringFromEEPROM(OFFSET_MQTT_PASS, MQTT_PASS, sizeof(MQTT_PASS));
  readStringFromEEPROM(OFFSET_MQTT_CLIENT_ID, MQTT_CLIENT_ID, sizeof(MQTT_CLIENT_ID));

  // Временное решение для совместимости:
  configServer.setMQTT(
      MQTT_SERVER,
      MQTT_PORT,
      MQTT_USER,
      MQTT_PASS,
      MQTT_CLIENT_ID);
}

void saveConfigToEEPROM(const char *mqtt_ip, const char *mqtt_port,
                        const char *user, const char *mqtt_pass,
                        const char *client_id,
                        const char *auth_user, const char *auth_pass)
{

  saveStringToEEPROM(OFFSET_MQTT_SERVER, mqtt_ip);
  saveStringToEEPROM(OFFSET_MQTT_PORT, mqtt_port);
  saveStringToEEPROM(OFFSET_MQTT_USER, user);
  saveStringToEEPROM(OFFSET_MQTT_PASS, mqtt_pass);
  saveStringToEEPROM(OFFSET_MQTT_CLIENT_ID, client_id);

  if (auth_user && strlen(auth_user) > 0 && isAscii(auth_user[0]))
  {
    saveStringToEEPROM(OFFSET_AUTH_USER, auth_user);
    Serial.println("Will be save to EEPROM with auth_user");
  }

  if (auth_pass && strlen(auth_pass) > 0 && isAscii(auth_pass[0]))
  {
    saveStringToEEPROM(OFFSET_AUTH_PASS, auth_pass);
    Serial.println("Will be save to EEPROM with auth_pass");
  }

  EEPROM.commit();
  Serial.println("EEPROM saved");
}

/** Optimazed */
void connectMQTT(bool force = false)
{
  // Буферы для PROGMEM строк
  char willTopic[32];
  char commandTopic[32];
  strncpy_P(willTopic, STATUS_TOPIC, sizeof(willTopic));
  strncpy_P(commandTopic, COMMAND_TOPIC, sizeof(commandTopic));

  // Проверка условий
  if (!force && (strlen(MQTT_SERVER) == 0 ||
                 WiFi.status() != WL_CONNECTED ||
                 millis() - lastMqttAttempt < mqttRetryInterval))
  {
    return;
  }

  lastMqttAttempt = millis();
  Serial.println(F("MQTT connect..."));
  blink(100, 3);

  // Формируем Last Will через шаблон
  char willPayload[128];
  snprintf_P(willPayload, sizeof(willPayload),
             LAST_WILL_JSON,
             OFFLINE_STATUS,
             MQTT_CLIENT_ID);

  // Подключение
  if (mqttClient.connect(
          MQTT_CLIENT_ID,
          MQTT_USER,
          MQTT_PASS,
          willTopic,
          MQTT_QOS,
          MQTT_RETAIN,
          willPayload))
  {
    Serial.println(F("✅ MQTT Connected"));
    configServer.setMqttConnected(true);

    // Подписка с проверкой
    if (!mqttClient.subscribe(commandTopic))
    {
      Serial.println(F("⚠️ Subscribe failed"));
    }
  }
  else
  {
    Serial.print(F("❌ MQTT ERROR: "));
    Serial.println(mqttClient.state());
    configServer.setMqttConnected(false);
  }
  // debugHeap("after connectMQTT");
}
/** Optimazed */
void callback(char *topic, byte *payload, unsigned int length)
{
  // Буферы (в стеке)
  char logBuffer[128];   // Для готовых сообщений
  char formatBuffer[64]; // Для шаблонов из PROGMEM

  // 1. Логирование топика
  strncpy_P(formatBuffer, MSG_TOPIC, sizeof(formatBuffer));
  snprintf(logBuffer, sizeof(logBuffer), formatBuffer, topic);
  Serial.println(logBuffer);

  // 2. Парсинг JSON
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, payload, length);

  if (error)
  {
    strncpy_P(formatBuffer, MSG_JSON_ERROR, sizeof(formatBuffer));
    snprintf(logBuffer, sizeof(logBuffer), formatBuffer, error.c_str());
    Serial.println(logBuffer);
    return;
  }

  // 3. Извлечение данных
  const char *action = doc["action"];
  const char *value = doc["value"];
  const char *device_id = doc["receiver"];

  // 4. Проверка device_id
  strncpy_P(formatBuffer, MSG_DEVICE_ID, sizeof(formatBuffer));
  snprintf(logBuffer, sizeof(logBuffer), formatBuffer,
           device_id ? device_id : "null", MQTT_CLIENT_ID);
  Serial.println(logBuffer);

  if (device_id && strcmp(device_id, MQTT_CLIENT_ID) == 0)
  {
    // 5. Логирование команды
    strncpy_P(formatBuffer, MSG_MQTT_CMD, sizeof(formatBuffer));
    snprintf(logBuffer, sizeof(logBuffer), formatBuffer,
             action ? action : "null", value ? value : "null");
    Serial.println(logBuffer);

    // 6. Обработка команды
    handleMQTTCommand(action, value);
  }
  // debugHeap("after callback");
}
/** Optimazed */
void handleMQTTCommand(const char *action, const char *value)
{
  if (!action)
    return;

  // Буфер для сообщений
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
  // debugHeap("after handleMQTTCommand");
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

  // 🧠 Попытка распарсить как data
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

    // Serial.println(jsonBuffer);
    return;
  }

  // 🧠 Альтернатива — конфигурационные параметры
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
