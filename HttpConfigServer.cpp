#include "HttpConfigServer.h"

HttpConfigServer::HttpConfigServer(int port,
                                   std::function<void(const char *, const char *, const char *, const char *,
                                                      const char *, const char *, const char *)>
                                       credCb,
                                   std::function<void()> rstCredCb)
    : server(port),
      saveCallback(credCb),
      resetCredentialsCallback(rstCredCb) {}

void HttpConfigServer::begin()
{
  if (isSerialDebug)
  {
    Serial.println("Starting HTTP server...");
  }

  server.on("/", HTTP_GET, [this]()
            { handleRoot(); });
  server.on("/send", HTTP_GET, [this]()
            { handleSendCommand(); });
  server.on("/config", HTTP_GET, [this]()
            { handleConfigPage(); });
  server.on("/config", HTTP_POST, [this]()
            { handleSaveConfig(); });
  server.on("/status", HTTP_GET, [this]()
            { handleStatus(); });
  server.onNotFound([this]()
                    { handleNotFound(); });
  server.begin();
  if (isSerialDebug)
  {
    Serial.println("HTTP server started");
  }
}

void HttpConfigServer::loop()
{
  server.handleClient();
}

void HttpConfigServer::handleSendCommand()
{
  if (!isAuthorized())
  {
    return server.requestAuthentication();
  }
  char command[32] = {0};
  strncpy(command, server.arg("command").c_str(), sizeof(command) - 1);

  // Check for 'reset_wifi' command
  if (strcmp(command, "reset_wifi") == 0)
  {
    resetCredentialsCallback();
    return;
  }

  if (strlen(command) == 0)
  {
    server.send(400, "application/json", FPSTR(ERROR_EMPTY_COMMAND));
    return;
  }

  if (isSerialDebug)
  {
    server.send(423, "application/json", FPSTR(ERROR_UART_IS_SHUTDOWN));
    return;
  }

  char response[32] = {0};
  size_t response_index = 0;

  loraSerial->print(command);

  // Receive response with 500ms timeout
  unsigned long start = millis();
  while (millis() - start < 500 && response_index < sizeof(response) - 1)
  {
    if (loraSerial->available())
    {
      char c = loraSerial->read();
      response[response_index++] = c;
    }
  }

  response[response_index] = '\0'; // Force null-termination

  StaticJsonDocument<128> doc;
  doc["cmd"] = command;
  doc["response"] = response;
  doc["device_id"] = _client_id;

  char jsonOut[128] = {0};
  serializeJson(doc, jsonOut, sizeof(jsonOut));
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", jsonOut);
}

void HttpConfigServer::handleRoot()
{

  if (!isAuthorized())
  {
    return server.requestAuthentication();
  }

  // 1. Headers preparation (without auto closing the connection)
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Content-Type", "text/html; charset=UTF-8");
  server.send(200, "text/html", "");
  // 2. Send HTML in chunks
  sendChunk(HTML_HEADER);
  sendChunk(ROOT_HTML);
}

void HttpConfigServer::handleConfigPage()
{
  if (!isAuthorized())
  {
    return server.requestAuthentication();
  }

  char port_str[6];
  snprintf(port_str, sizeof(port_str), "%u", _mqtt_port);

  // 1. Headers preparation (without auto closing the connection)
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Content-Type", "text/html; charset=UTF-8");
  server.send(200, "text/html", "");
  // 2. Send HTML in chunks
  sendChunk(HTML_HEADER);
  sendChunk(HTML_SETTINGS_START);

  // insert intput for _mqtt_ip
  sendChunk(HTML_SETTINGS_INPUT_MQTT_SERVER);
  sendChunk(_mqtt_ip);
  sendChunk(HTML_SETTINGS_INPUT_END);
  // End insert intput for _mqtt_ip

  // insert intput for _mqtt_port
  sendChunk(HTML_SETTINGS_MQTT_PORT_LABEL);
  sendChunk(HTML_SETTINGS_INPUT_MQTT_PORT);
  sendChunk(port_str);
  sendChunk(HTML_SETTINGS_INPUT_END);
  // End insert intput for _mqtt_port

  // insert intput for _mqtt_user
  sendChunk(HTML_SETTINGS_MQTT_USER_LABEL);
  sendChunk(HTML_SETTINGS_MQTT_USER);
  sendChunk(_mqtt_user);
  sendChunk(HTML_SETTINGS_INPUT_END);
  // End insert intput for _mqtt_user

  // insert intput for _mqtt_pass
  sendChunk(HTML_SETTINGS_MQTT_PASS_LABEL);
  sendChunk(HTML_SETTINGS_MQTT_PASS);
  sendChunk(_mqtt_pass);
  sendChunk(HTML_SETTINGS_INPUT_END);
  // End insert intput for _mqtt_pass

  // insert intput for _client_id
  sendChunk(HTML_SETTINGS_MQTT_CLIENT_ID_LABEL);
  sendChunk(HTML_SETTINGS_MQTT_CLIENT_ID);
  sendChunk(_client_id);
  sendChunk(HTML_SETTINGS_INPUT_END);
  // End insert intput for _client_id

  sendChunk(HTML_SETTINGS_HTML_END);
}

void HttpConfigServer::handleSaveConfig()
{
  if (!isAuthorized())
  {
    return server.requestAuthentication();
  }

  // Data buffers
  char mqtt_ip[64] = {0};
  char mqtt_port[6] = {0};
  char mqtt_user[64] = {0};
  char mqtt_pass[64] = {0};
  char client_id[64] = {0};
  char newAuthUser[32] = {0};
  char newAuthPass[32] = {0};

  // Get data with oversize protection
  strlcpy(mqtt_ip, server.arg("mqtt_ip").c_str(), sizeof(mqtt_ip));
  strlcpy(mqtt_port, server.arg("mqtt_port").c_str(), sizeof(mqtt_port));
  strlcpy(mqtt_user, server.arg("mqtt_user").c_str(), sizeof(mqtt_user));
  strlcpy(mqtt_pass, server.arg("mqtt_pass").c_str(), sizeof(mqtt_pass));
  strlcpy(client_id, server.arg("client_id").c_str(), sizeof(client_id));

  // Process new credentials
  if (server.hasArg("auth_user"))
  {
    strlcpy(newAuthUser, server.arg("auth_user").c_str(), sizeof(newAuthUser));
  }
  else
  {
    strlcpy(newAuthUser, authUser, sizeof(newAuthUser));
  }

  if (server.hasArg("auth_pass"))
  {
    strlcpy(newAuthPass, server.arg("auth_pass").c_str(), sizeof(newAuthPass));
  }
  else
  {
    strlcpy(newAuthPass, authPass, sizeof(newAuthPass));
  }

  // Debug logging only
  if (isSerialDebug)
  {
    Serial.println(F("📨 Получено из формы:"));
    Serial.printf_P(PSTR("mqtt_ip: %s\n"), mqtt_ip);
    Serial.printf_P(PSTR("mqtt_user: %s\n"), mqtt_user);
    Serial.printf_P(PSTR("client_id: %s\n"), client_id);
  }

  // Save to EEPROM
  saveCallback(mqtt_ip, mqtt_port, mqtt_user, mqtt_pass, client_id, newAuthUser, newAuthPass);

  // Response (using PROGMEM)
  server.send(200, "application/json", FPSTR(R"({"status":"saved"})"));
}

void HttpConfigServer::handleNotFound()
{
  server.send(404, "text/plain", F("404 Not Found"));
}

void HttpConfigServer::handleStatus()
{
  char jsonBuffer[sizeof(MQTT_CONN_STATUS_JSON) + 16];

  snprintf_P(jsonBuffer, sizeof(jsonBuffer),
             MQTT_CONN_STATUS_JSON,
             mqttConnected ? STATUS_CONNECTED : STATUS_DISCONNECTED);

  server.send(200, FPSTR("application/json"), jsonBuffer);
}

void HttpConfigServer::setMqttConnected(bool state)
{
  mqttConnected = state;
}

void HttpConfigServer::setLoraSerial(SoftwareSerial *serial)
{
  loraSerial = serial;
}

bool HttpConfigServer::isAuthorized()
{
  return server.authenticate(authUser, authPass);
}

void HttpConfigServer::setAuth(const char *user, const char *pwd)
{
  // Login copy with length check
  strncpy(authUser, user, sizeof(authUser) - 1);
  authUser[sizeof(authUser) - 1] = '\0';

  // Password copy
  strncpy(authPass, pwd, sizeof(authPass) - 1);
  authPass[sizeof(authPass) - 1] = '\0';
}

void HttpConfigServer::setIsSerialDebug(bool isDebug)
{
  isSerialDebug = isDebug;
}

void HttpConfigServer::setMQTT(const char *mqtt_ip, uint16_t mqtt_port,
                               const char *mqtt_user, const char *mqtt_pass,
                               const char *client_id)
{
  strlcpy(_mqtt_ip, mqtt_ip, sizeof(_mqtt_ip));
  strlcpy(_mqtt_user, mqtt_user, sizeof(_mqtt_user));
  strlcpy(_mqtt_pass, mqtt_pass, sizeof(_mqtt_pass));
  strlcpy(_client_id, client_id, sizeof(_client_id));
  _mqtt_port = mqtt_port;
}

void HttpConfigServer::sendChunk(const char *data)
{
  char buf[128];
  size_t len = strlen_P(data);
  size_t sent = 0;

  while (sent < len)
  {
    size_t chunk = min(sizeof(buf), len - sent);
    memcpy_P(buf, data + sent, chunk);
    server.sendContent(buf, chunk);
    sent += chunk;
  }
}