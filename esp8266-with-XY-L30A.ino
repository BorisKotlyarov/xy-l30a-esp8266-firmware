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

// UART для XY-L30A
SoftwareSerial loraSerial(3, 1); // RX = GPIO3, TX = GPIO1
HttpConfigServer configServer(80, saveConfigToEEPROM, resetWiFiCredentials);

WiFiClient espClient;
PubSubClient mqttClient(espClient);

String WIFI_SSID, WIFI_PASSWORD;
String MQTT_SERVER, MQTT_USER, MQTT_PASS, MQTT_CLIENT_ID;
int MQTT_PORT = 1883;
String authUser, authPass;

// Прототипы
// prototypes.h

bool isSerialDebug = false; // включён режим отладки, UART (if==true:  loraSerial НЕ инициализируется)
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

  Serial.begin(115200);
  delay(1000);

  pinMode(LED_BUILTIN, OUTPUT);
  Serial.println("=== Let's  start===");

  digitalWrite(LED_BUILTIN, LOW);
  delay(1000);

  digitalWrite(LED_BUILTIN, HIGH);

  EEPROM.begin(EEPROM_SIZE);

  Serial.println("--------------------");

  String WIFI_SSID = readStringFromEEPROM(OFFSET_WIFI_SSID);
  String WIFI_PASSWORD = readStringFromEEPROM(OFFSET_WIFI_PASS);
  // WiFi.setSleepMode(WIFI_LIGHT_SLEEP);
  // WiFi.setOutputPower(12.0); // Можно зменшити піковий струм - за рахунок потужності 12 Dbm
  loadAuthFromEEPROM();

  if (!WIFI_SSID.length())
  {
    Serial.println("📡 SSID not found Wireless Connection Manager will start");
    initLogin();
  }
  else
  {
    connectToAP(WIFI_SSID.c_str(), WIFI_PASSWORD.c_str(), true);
  }

  configServer.setIsSerialDebug(isSerialDebug);
  if (!isSerialDebug)
  {
    loraSerial.begin(9600); // UART для XY-L30A активен, если НЕ Serial Debug
    configServer.setLoraSerial(&loraSerial);
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\n✅ Wi-Fi подключен");
    Serial.print("IP ESP: ");
    Serial.println(WiFi.localIP());
  }
  else
  {
    Serial.println("\n❌ Wi-Fi не удалось подключиться");
    // Можешь решать: перезапустить, ждать в loop и пытаться повторно
    ESP.restart();
    return;
  }
  loadConfigFromEEPROM();

  configServer.begin();

  Serial.print("MQTT IP: ");
  Serial.println(MQTT_SERVER);

  mqttClient.setServer(MQTT_SERVER.c_str(), (uint16_t)MQTT_PORT);
  mqttClient.setCallback(callback);
  Serial.print("MQTT PORT: ");
  Serial.println(MQTT_PORT);

  connectMQTT(true);
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

  if (!isSerialDebug)
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
  snprintf(uptimeStr, sizeof(uptimeStr), "%02d:%02d:%02d", hrs, min, sec);

  StaticJsonDocument<192> doc;
  doc["status"] = "online";
  doc["ip"] = WiFi.localIP().toString();
  doc["rssi"] = String(WiFi.RSSI());
  doc["uptime"] = String(uptimeStr);
  doc["device_id"] = MQTT_CLIENT_ID;

  String jsonOut;
  serializeJson(doc, jsonOut);

  mqttClient.publish("device/status", jsonOut.c_str(), true);
}

void resetWiFiCredentials()
{
  Serial.println("⚠️ Сброс Wi-Fi конфигурации...");

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

  Serial.println("✅ Wi-Fi конфигурация сброшена");

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

String readStringFromEEPROM(int addr)
{
  char buffer[64]; // максимум 63 символа + '\0'
  int i = 0;
  char ch;
  while ((ch = EEPROM.read(addr + i)) != '\0' && i < 63)
  {
    buffer[i++] = ch;
  }
  buffer[i] = '\0';
  return String(buffer);
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
      Serial.println("✅ Wi-Fi saved");
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

  authUser = readStringFromEEPROM(OFFSET_AUTH_USER);
  authPass = readStringFromEEPROM(OFFSET_AUTH_PASS);

  bool userEmpty = authUser.isEmpty() || !isAscii(authUser[0]);
  bool passEmpty = authPass.isEmpty() || !isAscii(authPass[0]);

  if (userEmpty)
  {
    authUser = DEFAULT_USER;
    // Serial.println("⚠️ Пароль в EEPROM не найден — используем логин по умолчанию");
  }
  if (passEmpty)
  {
    authPass = DEFAULT_PASS;
    // Serial.println("⚠️ Пароль в EEPROM не найден — используем пароль по умолчанию");
  }

  Serial.println("🔐 Авторизация:");
  Serial.println("User: [" + authUser + "]");
  Serial.println("Pass: [" + authPass + "]"); // Только для отладки

  configServer.setAuth(authUser, authPass);
}

void loadConfigFromEEPROM()
{
  EEPROM.begin(512);
  WIFI_SSID = readStringFromEEPROM(OFFSET_WIFI_SSID);
  WIFI_PASSWORD = readStringFromEEPROM(OFFSET_WIFI_PASS);
  MQTT_SERVER = readStringFromEEPROM(OFFSET_MQTT_SERVER);
  MQTT_PORT = readStringFromEEPROM(OFFSET_MQTT_PORT).toInt(); // Храним как строку
  MQTT_USER = readStringFromEEPROM(OFFSET_MQTT_USER);
  MQTT_PASS = readStringFromEEPROM(OFFSET_MQTT_PASS);
  MQTT_CLIENT_ID = readStringFromEEPROM(OFFSET_MQTT_CLIENT_ID);

  // раздаем всем нуждающимся
  configServer.setMQTT(MQTT_SERVER, String(MQTT_PORT), MQTT_USER, MQTT_PASS, MQTT_CLIENT_ID);
}

void saveConfigToEEPROM(const String &mqtt_ip, const String &mqtt_port,
                        const String &user, const String &mqtt_pass,
                        const String &client_id,
                        const String &auth_user, const String &auth_pass)
{

  saveStringToEEPROM(OFFSET_MQTT_SERVER, mqtt_ip);
  saveStringToEEPROM(OFFSET_MQTT_PORT, mqtt_port);
  saveStringToEEPROM(OFFSET_MQTT_USER, user);
  saveStringToEEPROM(OFFSET_MQTT_PASS, mqtt_pass);
  saveStringToEEPROM(OFFSET_MQTT_CLIENT_ID, client_id);
  if (!auth_user.isEmpty() && isAscii(auth_user[0]))
  {
    saveStringToEEPROM(OFFSET_AUTH_USER, auth_user);
    Serial.println("✅ Will be save to EEPROM with auth_user");
  }
  if (!auth_pass.isEmpty() && isAscii(auth_pass[0]))
  {
    saveStringToEEPROM(OFFSET_AUTH_PASS, auth_pass);

    Serial.println("✅ Will be save to EEPROM with auth_pass");
  }

  EEPROM.commit();

  Serial.println("✅ EEPROM saved");
}

void connectMQTT(bool force = false)
{
  loadConfigFromEEPROM();
  if (!MQTT_SERVER.length())
    return;

  if (WiFi.status() != WL_CONNECTED)
  {
    // Serial.println("🚫 Wi-Fi не подключён — MQTT не запускаем");
    return;
  }

  unsigned long now = millis();
  if (!force && now - lastMqttAttempt < mqttRetryInterval)
    return;

  lastMqttAttempt = now;

  Serial.println("MQTT соединение...");
  blink(100, 3);

  mqttClient.setServer(MQTT_SERVER.c_str(), MQTT_PORT);

  StaticJsonDocument<192> doc;
  doc["status"] = "offline";
  doc["device_id"] = MQTT_CLIENT_ID;
  String jsonOut;
  serializeJson(doc, jsonOut);

  if (mqttClient.connect(MQTT_CLIENT_ID.c_str(), MQTT_USER.c_str(), MQTT_PASS.c_str(), "device/status", 1, true, jsonOut.c_str()))
  {
    Serial.println("✅ MQTT Connected");
    configServer.setMqttConnected(true);
    mqttClient.subscribe("device/command");
  }
  else
  {
    Serial.print("❌ MQTT ERROR: ");
    Serial.println(mqttClient.state());
    configServer.setMqttConnected(false);
  }
}

void callback(char *topic, byte *payload, unsigned int length)
{
  Serial.print("topic:");
  Serial.println(topic);

  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, payload, length);

  if (error)
  {
    Serial.print("⚠️ Ошибка JSON: ");
    Serial.println(error.c_str());
    return;
  }

  const char *action = doc["action"];
  const char *value = doc["value"];
  const char *device_id = doc["reciever"];

  Serial.print("⚠️ device_id from JSON: ");
  Serial.println(device_id);

  Serial.print("⚠️ MQTT_CLIENT_ID: ");
  Serial.println(MQTT_CLIENT_ID);

  if (device_id && strcmp(device_id, MQTT_CLIENT_ID.c_str()) == 0)
  {
    Serial.print("📥 JSON MQTT Команда: ");
    Serial.print(action);
    Serial.print(" → ");
    Serial.println(value);

    handleMQTTCommand(String(action), String(value));
  }
}

void handleMQTTCommand(String action, String value)
{
  if (action == "restart")
  {
    ESP.restart();
  }
  else if (action == "blink")
  {
    blink(200, value.toInt());
  }
  else if (action == "uart_send")
  {
    loraSerial.print(value);
  }
  else if (action == "reset_wifi")
  {
    resetWiFiCredentials();
  }
  else
  {
    Serial.println("⚠️ Неизвестная команда: " + action);
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
  XYPacket packet;

  // 🧠 Попытка распарсить как data
  if (XYParser::parse(rawLine, packet))
  {
    StaticJsonDocument<192> doc;
    doc["type"] = "data";
    doc["voltage"] = packet.voltage;
    doc["percent"] = packet.percent;
    doc["time"] = String(packet.hours) + ":" + String(packet.minutes);
    doc["state"] = packet.state;
    doc["device_id"] = MQTT_CLIENT_ID;

    String jsonOut;
    serializeJson(doc, jsonOut);
    mqttClient.publish("esp/data", jsonOut.c_str());
    return;
  }

  // 🧠 Альтернатива — конфигурационные параметры
  StaticJsonDocument<256> doc;
  doc["type"] = "config";
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

    // Проверка на таймер (формат HH:MM)
    if (!matched && strchr(token, ':') && strlen(token) <= 5)
    {
      params["timer"] = token;
      hasAny = true;
    }

    token = strtok(nullptr, ",");
  }

  if (hasAny)
  {
    String jsonOut;
    serializeJson(doc, jsonOut);
    mqttClient.publish("esp/config", jsonOut.c_str());
  }
  else
  {
    StaticJsonDocument<128> rawDoc;
    rawDoc["type"] = "raw";
    rawDoc["line"] = rawLine;
    rawDoc["device_id"] = MQTT_CLIENT_ID;

    String jsonRaw;
    serializeJson(rawDoc, jsonRaw);
    mqttClient.publish("esp/raw", jsonRaw.c_str());
  }
}
