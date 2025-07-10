#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <SoftwareSerial.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include "XYParser.h"
#include "WifiConnectionManager.h"
#include "config.h"

// UART для XY-L30A
SoftwareSerial loraSerial(3, 1); // RX = GPIO3, TX = GPIO1
ESP8266WebServer server(80);
WiFiClient espClient;
PubSubClient mqttClient(espClient);

String WIFI_SSID, WIFI_PASSWORD;
String MQTT_SERVER, MQTT_USER, MQTT_PASS, MQTT_CLIENT_ID;
int MQTT_PORT = 1883;
String authUser, authPass;

// Прототипы
void handleSendCommand();
void handleRoot();
void loraReader();
void handleXYResponse(const char *line);
void callback(char *topic, byte *payload, unsigned int length);
void connectMQTT(bool force);
void loadConfigFromEEPROM();
void saveConfigToEEPROM(const String &mqtt_ip, const String &mqtt_port,
                        const String &user, const String &mqtt_pass,
                        const String &client_id,
                        const String &auth_user, const String &auth_pass);
void handleSaveConfig();
void handleConfigPage();
void loadAuthFromEEPROM();
void saveWiFiConfig(const String &ssid, const String &pass);
void saveStringToEEPROM(int addr, const String &value);
String readStringFromEEPROM(int addr);
void connectToAP(const char *ssid, const char *pass, bool isCheckAttempt);
void debugEEPROM(int start, int len);
void blink(int _delay, int _num);
void resetWiFiCredentials();
void handleMQTTCommand(String action, String value);

bool isSerialDebug = false; // включён режим отладки, UART (if==true:  loraSerial НЕ инициализируется)
unsigned long lastMqttAttempt = 0;
const unsigned long mqttRetryInterval = 5000; // в мс
unsigned int WifiattemptReconnect = 0;
unsigned int MAX_ATTEMPT_TO_RECONNECT = 10; // arter that device will reboot
bool mqttConnected = false;

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
  Serial.println("=== Начинаем ===");
  EEPROM.begin(EEPROM_SIZE);
  pinMode(LED_BUILTIN, OUTPUT);

  Serial.print("📦 MQTT IP в EEPROM @128: ");
  debugEEPROM(OFFSET_MQTT_SERVER, 32);

  Serial.print("📦 EEPROM байты:");
  for (int i = 128; i < 144; i++)
  {
    Serial.print(" ");
    Serial.print(EEPROM.read(i), HEX);
  }
  Serial.println();

  Serial.println("--------------------");

  String WIFI_SSID = readStringFromEEPROM(OFFSET_WIFI_SSID);
  String WIFI_PASSWORD = readStringFromEEPROM(OFFSET_WIFI_PASS);

  loadAuthFromEEPROM();

  if (!WIFI_SSID.length())
  {
    Serial.println("📡 SSID не найден — запускаем портал конфигурации");
    initLogin();
  }
  else
  {
    connectToAP(WIFI_SSID.c_str(), WIFI_PASSWORD.c_str(), true);
  }

  if (!isSerialDebug)
  {
    loraSerial.begin(9600); // UART для XY-L30A активен, если НЕ Serial Debug
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
  server.on("/config", HTTP_GET, handleConfigPage);
  server.on("/config", HTTP_POST, handleSaveConfig);
  server.on("/", handleRoot);
  server.on("/send", handleSendCommand);
  server.on("/status", HTTP_GET, []()
            {
    String json = "{ \"mqtt\": \"" + String(mqttConnected ? "connected" : "disconnected") + "\" }";
    server.send(200, "application/json", json); });

  server.begin();
  Serial.println("HTTP сервер запущен");

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

  server.handleClient();

  if (!isSerialDebug)
  {
    loraReader(); // или любую функцию чтения UART
  }

  if (!mqttClient.connected())
  {
    connectMQTT(false);
    mqttConnected = false;
  }
  else
  {
    mqttConnected = true;
    mqttClient.loop();
  }
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

void debugEEPROM(int start, int len)
{
  for (int i = start; i < start + len; ++i)
  {
    char c = EEPROM.read(i);
    if (c >= 32 && c <= 126)
      Serial.print(c);
    else
      Serial.print(".");
  }

  Serial.println();
}

void saveStringToEEPROM(int addr, const String &value)
{
  Serial.print("💾 Пишем в EEPROM @");
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
  portalRunInf();

  Serial.println(portalStatus());

  if (portalStatus() == SP_SUBMIT)
  {
    connectToAP(portalCfg.SSID, portalCfg.pass, true);

    if (WiFi.status() != WL_CONNECTED)
    {
      initLogin();
    }
    else if (WiFi.status() == WL_CONNECTED)
    {
      saveWiFiConfig(portalCfg.SSID, portalCfg.pass); // 💾 сохраняем
      Serial.println("✅ Wi-Fi подключение установлено и сохранено");
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

  // Set time via NTP, as required for x.509 validation
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

bool isAuthorized()
{
  Serial.println("📦 EEPROM:");
  Serial.println("authUser: [" + authUser + "]");
  Serial.println("authPass: [" + authPass + "]");

  Serial.println("🧪 DEFAULT_USER = " + String(DEFAULT_USER));
  Serial.println("🧪 DEFAULT_PASS = " + String(DEFAULT_PASS));

  return server.authenticate(authUser.c_str(), authPass.c_str());
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
}

void saveConfigToEEPROM(const String &mqtt_ip, const String &mqtt_port,
                        const String &user, const String &mqtt_pass,
                        const String &client_id,
                        const String &auth_user, const String &auth_pass)
{

  Serial.println("📨 Получено для сохранения saveConfigToEEPROM");
  Serial.println("mqtt_ip: " + mqtt_ip);
  Serial.println("mqtt_port: " + mqtt_port);
  Serial.println("user: " + user);           // временно!
  Serial.println("mqtt_pass: " + mqtt_pass); // временно!
  Serial.println("client_id: " + client_id); // временно!

  saveStringToEEPROM(OFFSET_MQTT_SERVER, mqtt_ip);
  saveStringToEEPROM(OFFSET_MQTT_PORT, mqtt_port);
  saveStringToEEPROM(OFFSET_MQTT_USER, user);
  saveStringToEEPROM(OFFSET_MQTT_PASS, mqtt_pass);
  saveStringToEEPROM(OFFSET_MQTT_CLIENT_ID, client_id);
  saveStringToEEPROM(OFFSET_AUTH_USER, auth_user); // 👈 авторизационный логин
  saveStringToEEPROM(OFFSET_AUTH_PASS, auth_pass); // 👈 авторизационный пароль
  EEPROM.commit();
  Serial.println("✅ EEPROM сохранено с авторизацией");
}

void handleSaveConfig()
{
  Serial.println("📨 handleSaveConfig() вызван!");

  if (!server.authenticate(authUser.c_str(), authPass.c_str()))
  {
    return server.requestAuthentication();
  }

  String mqtt_ip = server.arg("mqtt_ip");
  String mqtt_port = server.arg("mqtt_port");
  String mqtt_user = server.arg("mqtt_user");
  String mqtt_pass = server.arg("mqtt_pass");
  String client_id = server.arg("client_id");

  Serial.println("📨 Получено из формы:");
  Serial.println("mqtt_ip: " + server.arg("mqtt_ip"));
  Serial.println("mqtt_user: " + server.arg("mqtt_user"));
  Serial.println("mqtt_pass: " + server.arg("mqtt_pass")); // временно!
  Serial.println("client_id: " + server.arg("client_id")); // временно!
  // Чтение новых авторизационных данных, если есть
  String newAuthUser = server.hasArg("auth_user") ? server.arg("auth_user") : authUser;
  String newAuthPass = server.hasArg("auth_pass") ? server.arg("auth_pass") : authPass;

  // EEPROM сохранение
  saveConfigToEEPROM(mqtt_ip, mqtt_port,
                     mqtt_user, mqtt_pass, client_id,
                     newAuthUser, newAuthPass);

  server.send(200, "application/json", "{\"status\":\"saved\"}");
}

void handleConfigPage()
{
  if (!isAuthorized())
  {
    return server.requestAuthentication();
  }

  String html = R"=====(<!DOCTYPE html>
  <html>
  <head>
    <meta charset="utf-8">
    <title>Настройки ESP</title>
    <style>
      body { font-family: sans-serif; margin: 20px; }
      input, button { padding: 8px; margin: 6px 0; width: 100%; }
      label { font-weight: bold; display: block; margin-top: 12px; }
    </style>
  </head>
  <body>
    <header><a href="/">XY-L30A Control</a></header>
    <h2>⚙️ Налаштування</h2>
    <form method="POST" action="/config" id="configForm">
      <label>MQTT Server IP:</label>
      <input type="text" name="mqtt_ip">

      <label>MQTT Port:</label>
      <input type="number" name="mqtt_port" value="1883">

      <label>MQTT User:</label>
      <input type="text" name="mqtt_user">

      <label>MQTT Password:</label>
      <input type="text" name="mqtt_pass">

      <label>MQTT Client ID:</label>
      <input type="text" name="client_id">

      <label>New login:</label>
      <input type="text" name="auth_user" >

      <label>New password:</label>
      <input type="text" name="auth_pass" >

      <button type="submit">💾 Save</button>
    </form>

    <div id="status" style="margin-top:20px;"></div>

    <script>
      document.getElementById('configForm').addEventListener('submit', function(ev) {
        ev.preventDefault();
        const form = ev.target;
        const formData = new FormData(form);

        fetch(form.action, {
          method: 'POST',
          body: formData
        })
        .then(res => res.json())
        .then(data => {
          document.getElementById('status').textContent = "✅ Настройки сохранены. Перезагрузка...";
          setTimeout(() => location.reload(), 3000);
        })
        .catch(err => {
          document.getElementById('status').textContent = "❌ Ошибка: " + err;
        });
      });
    </script>
  </body>
  </html>)=====";

  server.sendHeader("Content-Type", "text/html; charset=UTF-8");
  server.send(200, "text/html", html);
}

void handleRoot()
{
  String authHeader = server.header("Authorization");

  Serial.println("🔎 Authorization header при открытии /:");
  Serial.println(authHeader);

  if (!isAuthorized())
  {
    return server.requestAuthentication();
  }

  String html = R"=====(<!DOCTYPE html>
  <html>
  <head>
    <meta charset="utf-8">
    <title>XY-L30A Control</title>
    <style>
      body { font-family: Arial; margin: 20px; }
      input, button { padding: 10px; margin: 5px; }
    </style>
  </head>
  <body>
    <header>
    <a href="/config">Налаштування</a>
    <div id="mqttStatus" style="font-weight:bold; margin-top:10px;"></div>

    </header>
    <h1>XY-L30A — Controll</h1>
    
    <button onclick="sendCommand('on', event)">on</button>
    <button onclick="sendCommand('off', event)">off</button>
    <button onclick="sendCommand('read', event)">read</button>
    <button onclick="sendCommand('start', event)">start</button>
    <button onclick="sendCommand('stop', event)">stop</button>

    <div>
      <input type="number" step="0.1" id="dw">
      <button onclick="setDW(event)">dw</button>
    </div>

    <div>
      <input type="number" step="0.1" id="up">
      <button onclick="setUP(event)">up</button>
    </div>

    <div>
      <input type="time" id="timer">
      <button onclick="setTimer(event)">timer</button>
    </div>

    <form name="publish">
      <input type="text" name="message">
      <input type="submit" value="Send">
    </form>

    <div id="messages" style="min-height:30px; border:solid 1px black; margin-top:10px; padding:5px;"></div>

    <script>

    function updateMQTTStatus() {
      const el = document.getElementById("mqttStatus");
      el.innerHTML = 'MQTT: <span style="color:gray">Please wait...</span>';
      fetch('/status')
        .then(r => r.json())
        .then(data => {          
          if (data.mqtt === "connected") {
            el.innerHTML = 'MQTT: <span style="color:green">Online</span>';
          } else {
            el.innerHTML = 'MQTT: <span style="color:red">Offline</span>';
          }
        });
    }

      setInterval(updateMQTTStatus, 5000);

      function sendCommand(cmd, ev) {
    if (ev) {
      ev.preventDefault();
      ev.stopPropagation();
    }

    return fetch('/send?command=' + encodeURIComponent(cmd))
      .then(res => res.json())
      .then(data => {
        const msg = document.createElement('div');
        msg.innerHTML = `<b>${data.cmd}</b> → <pre>${data.response}</pre>`;
        document.getElementById('messages').prepend(msg);

        // 🔍 Если команда была "read" — парсим параметры
        if (data.cmd === "read" && typeof data.response === "string") {
          const clean = data.response
            .replace("dw", "")
            .replace("up", "")
            .replace("\n", "")
            .trim();

          const [dw, up, timer] = clean.split(",");

          console.log("🎯 Распознано:", dw, up, timer);

          if (dw) document.getElementById('dw').value = dw;
          if (up) document.getElementById('up').value = up;
          if (timer) document.getElementById('timer').value = timer;
          
        }

        // 🚨 Если response содержит "DOWN" — отправляем повторный read
        if (typeof data.response === "string" && data.response.includes("DOWN")) {
          console.warn("🚨 Обнаружено DOWN — повторный read...");
          sendCommand("read"); // рекурсивный вызов
        }

        return data;
      })
      .catch(err => {
        const error = document.createElement('div');
        error.textContent = "Ошибка: " + err;
        document.getElementById('messages').prepend(error);
        throw err;
      });
  }

    // ⏳ Автозагрузка при открытии страницы
    window.addEventListener('DOMContentLoaded', () => {
      sendCommand("read");
      updateMQTTStatus();
    });


      function setDW(ev) {
        const val = document.getElementById('dw').value;
        sendCommand("dw" + val, ev);
      }

      function setUP(ev) {
        const val = document.getElementById('up').value;
        sendCommand("up" + val, ev);
      }

      function setTimer(ev) {
        const val = document.getElementById('timer').value;
        sendCommand(val, ev);
      }

      document.forms.publish.onsubmit = function(ev) {
        const text = this.message.value;
        sendCommand(text, ev);
      };
    </script>
  </body>
  </html>)=====";

  server.sendHeader("Content-Type", "text/html; charset=UTF-8");
  server.send(200, "text/html", html);
}

void connectMQTT(bool force = false)
{
  loadConfigFromEEPROM();
  if (!MQTT_SERVER.length())
    return;

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("🚫 Wi-Fi не подключён — MQTT не запускаем");
    return;
  }

  unsigned long now = millis();
  if (!force && now - lastMqttAttempt < mqttRetryInterval)
    return;

  lastMqttAttempt = now;

  Serial.println("MQTT соединение...");
  blink(100, 3);

  mqttClient.setServer(MQTT_SERVER.c_str(), MQTT_PORT);
  mqttClient.setWill("device/status", "{\"status\":\"offline\"}", true, 1);
  if (mqttClient.connect(MQTT_CLIENT_ID.c_str(), MQTT_USER.c_str(), MQTT_PASS.c_str()))
  {
    Serial.println("✅ MQTT подключен");
    mqttConnected = true;
    mqttClient.subscribe("device/command");
  }
  else
  {
    Serial.print("❌ MQTT ошибка: ");
    Serial.println(mqttClient.state());
    mqttConnected = false;
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

void handleSendCommand()
{
  if (!isAuthorized())
  {
    return server.requestAuthentication();
  }

  if (isSerialDebug)
  {
    server.send(200, "application/json", "{\"error\":\"UART выключен (режим отладки)\"}");
    return;
  }

  String command = server.arg("command");
  if (!command.length())
  {
    server.send(400, "application/json", "{\"error\":\"пустая команда\"}");
    return;
  }

  loraSerial.print(command);
  String response = "";
  unsigned long start = millis();
  while (millis() - start < 500)
  {
    while (loraSerial.available())
    {
      response += (char)loraSerial.read();
    }
  }

  StaticJsonDocument<256> doc;
  doc["cmd"] = command;
  doc["response"] = response;

  String jsonOut;
  serializeJson(doc, jsonOut);
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", jsonOut);
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
    yield(); // 👈 обязательно!
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

    String jsonOut;
    serializeJson(doc, jsonOut);
    mqttClient.publish("esp/data", jsonOut.c_str());
    return;
  }

  // 🧠 Альтернатива — конфигурационные параметры
  StaticJsonDocument<256> doc;
  doc["type"] = "config";
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

    String jsonRaw;
    serializeJson(rawDoc, jsonRaw);
    mqttClient.publish("esp/raw", jsonRaw.c_str());
  }
}
