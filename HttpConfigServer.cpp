#include "HttpConfigServer.h"

HttpConfigServer::HttpConfigServer(int port,
  std::function<void(const String&, const String&, const String&, const String&, const String&, const String&, const String&)> credCb,
  std::function<void()> rstCredCb)
  : server(port),
    saveCallback(credCb),
    resetCredentialsCallback(rstCredCb) {}



void HttpConfigServer::begin() {
    server.on("/", HTTP_GET, [this]() { handleRoot(); } );
    server.on("/send", HTTP_GET, [this]() { handleSendCommand(); } );
    server.on("/config", HTTP_GET, [this]() { handleConfigPage(); } );
    server.on("/config", HTTP_POST,[this]() { handleSaveConfig(); }  );
    server.on("/status", HTTP_GET, [this]() { handleStatus(); });
    server.onNotFound([this]() {  handleNotFound(); });    
    server.begin();
    Serial.println("HTTP Started");
}

void HttpConfigServer::loop() {
    server.handleClient();
}

void HttpConfigServer::handleSendCommand()
{
  if (!isAuthorized())
  {
    return server.requestAuthentication();
  }
 String command = server.arg("command");
  if(command == "reset_wifi")
  {
    resetCredentialsCallback();
  }

    if (!command.length())
  {
    StaticJsonDocument<192> doc;
    doc["error"] = "Empty command";
    String jsonOut;
    serializeJson(doc, jsonOut);
    server.send(400, "application/json", jsonOut);
    return;
  }

  if (isSerialDebug)
  {
    StaticJsonDocument<192> doc;
    doc["error"] = "UART is shut down (debug mode)";
    String jsonOut;
    serializeJson(doc, jsonOut);
    server.send(200, "application/json", jsonOut);
    return;
  }



  loraSerial->print(command);
  String response = "";
  unsigned long start = millis();
  while (millis() - start < 500)
  {
    while (loraSerial->available())
    {
      response += (char)loraSerial->read();
    }
  }

  StaticJsonDocument<256> doc;
  doc["cmd"] = command;
  doc["response"] = response;
  doc["device_id"] = _client_id;

  String jsonOut;
  serializeJson(doc, jsonOut);
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", jsonOut);
}

void HttpConfigServer::handleRoot() {
    
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
      .btn {
        background-color: #5a6463;
        color: white;
        border: none;
        text-transform: uppercase;
        font-weight: 700;
      }

      .btn.danger {
        background-color: #e90000;
      }
    </style>
  </head>
  <body>
    <header>
    <a href="/config">Settings</a>
    <div id="mqttStatus" style="font-weight:bold; margin-top:10px;"></div>

    </header>
    <h1>XY-L30A — Controll</h1>
    
    <button class="btn" onclick="sendCommand('on', event)">on</button>
    <button class="btn" onclick="sendCommand('off', event)">off</button>
    <button class="btn" onclick="sendCommand('read', event)">read</button>
    <button class="btn" onclick="sendCommand('start', event)">start</button>
    <button class="btn" onclick="sendCommand('stop', event)">stop</button>
    
    <div>
      <input type="number" step="0.1" id="dw">
      <button class="btn" onclick="setDW(event)">dw</button>
    </div>

    <div>
      <input type="number" step="0.1" id="up">
      <button class="btn" onclick="setUP(event)">up</button>
    </div>

    <div>
      <input type="time" id="timer">
      <button class="btn" onclick="setTimer(event)">timer</button>
    </div>

    <form name="publish">
      <input type="text" name="message">
      <input type="submit" value="Send">
    </form>
    <button class="btn danger" onclick="confirmResetWifi(event)">Reset WIFI auth</button>
    <div id="messages" style="min-height:30px; border:solid 1px black; margin-top:10px; padding:5px;"></div>

    <script>

    function updateMQTTStatus() {
      const el = document.getElementById("mqttStatus");      
      fetch('/status', { credentials: 'include' })
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

        if (data.cmd === "read" && typeof data.response === "string") {
          const clean = data.response
            .replace("dw", "")
            .replace("up", "")
            .replace("\n", "")
            .trim();

          const [dw, up, timer] = clean.split(",");

          if (dw) document.getElementById('dw').value = dw;
          if (up) document.getElementById('up').value = up;
          if (timer) document.getElementById('timer').value = timer;
          
        }

        
        if (typeof data.response === "string" && data.response.includes("DOWN")) {
          sendCommand("read"); 
        }

        return data;
      })
      .catch(err => {
        const error = document.createElement('div');
        error.textContent = "Error: " + err;
        document.getElementById('messages').prepend(error);
        throw err;
      });
  }

    function confirmResetWifi(event) {
      if (window.confirm("Are you sure you want to reset wifi credentials?")) {
        sendCommand('reset_wifi', event)
      }
    }

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

void HttpConfigServer::handleConfigPage() {
  if (!isAuthorized()) {
      return server.requestAuthentication();
  }

  String html = R"=====(<!DOCTYPE html>
  <html>
  <head>
    <meta charset="utf-8">
    <title>Settings</title>
    <style>
      body { font-family: sans-serif; margin: 20px; }
      input, button { padding: 8px; margin: 6px 0; width: 100%; }
      label { font-weight: bold; display: block; margin-top: 12px; }
    </style>
  </head>
  <body>
    <header><a href="/">XY-L30A Control</a></header>
    <h2>⚙️ Settings</h2>
    <form method="POST" action="/config" id="configForm">
      <label>MQTT Server/Server IP:</label>
      <input type="text" name="mqtt_ip" value="{{MQTT_SERVER}}">

      <label>MQTT Port:</label>
      <input type="number" name="mqtt_port" value="{{MQTT_PORT}}" min="1" max="65535">

      <label>MQTT User:</label>
      <input type="text" name="mqtt_user" value="{{MQTT_USER}}">

      <label>MQTT Password:</label>
      <input type="text" name="mqtt_pass" value="{{MQTT_PASS}}">

      <label>MQTT Client ID:</label>
      <input type="text" name="client_id" value="{{MQTT_CLIENT_ID}}">

      <label>New login:</label>
      <input type="text" name="auth_user"  >

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
          body: formData,
          credentials: 'include',
        })
        .then(res => res.json())
        .then(data => {
          document.getElementById('status').textContent = "✅ Settings saved!";
          setTimeout(() => location.reload(), 3000);
        })
        .catch(err => {
          document.getElementById('status').textContent = "❌ Error: " + err;
        });
      });
    </script>
  </body>
  </html>)=====";

  html.replace("{{MQTT_SERVER}}", _mqtt_ip);
  html.replace("{{MQTT_PORT}}", _mqtt_port);
  html.replace("{{MQTT_USER}}", _mqtt_user);
  html.replace("{{MQTT_PASS}}", _mqtt_pass);
  html.replace("{{MQTT_CLIENT_ID}}", _client_id);

  server.sendHeader("Content-Type", "text/html; charset=UTF-8");
  server.send(200, "text/html", html);
}

void HttpConfigServer::handleSaveConfig() {
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
  saveCallback(mqtt_ip, mqtt_port,
                     mqtt_user, mqtt_pass, client_id,
                     newAuthUser, newAuthPass);

  server.send(200, "application/json", "{\"status\":\"saved\"}");
}

void HttpConfigServer::handleNotFound() {
    server.send(404, "text/plain", "404 Not Found");
}

void HttpConfigServer::handleStatus() {
      StaticJsonDocument<64> doc;
      doc["mqtt"] = mqttConnected ? "connected" : "disconnected";
      String jsonOut;
      serializeJson(doc, jsonOut);      
      server.send(200, "application/json", jsonOut);
}

void HttpConfigServer::setMqttConnected(bool state) {
    mqttConnected = state;
}

void HttpConfigServer::setLoraSerial(SoftwareSerial*  serial) {
  loraSerial = serial;
}

bool HttpConfigServer::isAuthorized()
{
  Serial.println("📦 EEPROM:");
  Serial.println("authUser: [" + authUser + "]");
  Serial.println("authPass: [" + authPass + "]");

  return server.authenticate(authUser.c_str(), authPass.c_str());
}

void HttpConfigServer::setAuth(String user, String pwd) {
  authUser = user;
  authPass = pwd;
}

void HttpConfigServer::setIsSerialDebug(bool isDebug) {
  isSerialDebug = isDebug;
}


void HttpConfigServer::setMQTT(String mqtt_ip, String mqtt_port, String mqtt_user, String mqtt_pass, String client_id) {
     _mqtt_ip = mqtt_ip;
     _mqtt_port = mqtt_port;
     _mqtt_user = mqtt_user;
     _mqtt_pass = mqtt_pass;
     _client_id = client_id;
}
