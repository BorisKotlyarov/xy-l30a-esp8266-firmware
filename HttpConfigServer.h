#ifndef HTTP_CONFIG_SERVER_H
#define HTTP_CONFIG_SERVER_H
#pragma once
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <SoftwareSerial.h>

const char ERROR_EMPTY_COMMAND[] PROGMEM = "{\"error\":\"Empty command\"}";
const char ERROR_UART_IS_SHUTDOWN[] PROGMEM = "{\"error\":\"UART is shut down (debug mode)\"}";

const char HTML_HEADER[] PROGMEM = R"=====(<!DOCTYPE html><!DOCTYPE html>
  <html>
  <head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>XY-L10A/XY-L30A Control</title>
    <style>
    <style>
    * { box-sizing: border-box; }
    body { font-family: Arial; margin: 20px; }
    input, button { padding: 10px 0px; margin: 5px 0px; }
    .btn { background-color: #5a6463;color: white; border: none;text-transform: uppercase; font-weight: 700; padding-left: 1rem; padding-right: 1rem;}
    .btn.danger { background-color: #e90000; }
    .danger-text { color: #e90000; }
    label { font-weight: bold; display: block; margin-top: 12px; }
    .w-100 {width: 100%;}
    fieldset {  margin-bottom: 1rem; background-color: #e7e7e7; border-color: #ebebeb; }
    legend {font-weight: 900; }
    </style>
    </style>
  </head>
  <body>
)=====";

const char ROOT_HTML[] PROGMEM = R"=====(
  <body>
    <header>
    <a href="/config">Settings</a>
    <div id="mqttStatus" style="font-weight:bold; margin-top:10px;"></div>

    </header>
    <hr>
    <h2>XY-Lx0A — Control</h1>
    <hr>
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
    <hr>
    <form name="publish">
      <input type="text" name="message">
      <input type="submit" value="Send">
    </form>
    <hr>
    <button class="btn danger" onclick="confirmResetWifi(event)">Reset WIFI auth</button>
    <hr>
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
        if(data.error) {
          throw new Error(data.error);
        }
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
        error.classList.add("danger-text");
        error.textContent = "" + err;
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

const char HTML_SETTINGS_START[] PROGMEM = R"=====(
    <header><a href="/">XY-Lx0A Control</a></header>
    <hr>
    <h2>⚙️ Settings</h2><hr>
    <form method="POST" action="/config" id="configForm">
    <fieldset>
      <legend>MQTT settings</legend>
        <label for="mqtt_ip">MQTT Server/Server IP:</label>
)=====";

const char HTML_SETTINGS_MQTT_PORT_LABEL[] PROGMEM = R"=====(<label for="mqtt_port">MQTT Port:</label>)=====";
const char HTML_SETTINGS_MQTT_USER_LABEL[] PROGMEM = R"=====(<label for="mqtt_user">MQTT User:</label>)=====";
const char HTML_SETTINGS_MQTT_PASS_LABEL[] PROGMEM = R"=====(<label for="mqtt_pass">MQTT Password:</label>)=====";
const char HTML_SETTINGS_MQTT_CLIENT_ID_LABEL[] PROGMEM = R"=====(<label for="client_id">MQTT Client ID:</label>)=====";

const char HTML_SETTINGS_INPUT_END[] PROGMEM = R"=====(">)=====";

const char HTML_SETTINGS_INPUT_MQTT_SERVER[] PROGMEM = R"=====(<input class="w-100" id="mqtt_ip" type="text" name="mqtt_ip" value=")=====";
const char HTML_SETTINGS_INPUT_MQTT_PORT[] PROGMEM = R"=====(<input class="w-100"  id="mqtt_port"  type="number" name="mqtt_port" min="1" max="65535" value=")=====";
const char HTML_SETTINGS_MQTT_USER[] PROGMEM = R"=====(<input class="w-100" id="mqtt_user" type="text" name="mqtt_user" value=")=====";
const char HTML_SETTINGS_MQTT_PASS[] PROGMEM = R"=====(<input class="w-100" id="mqtt_pass" type="text" name="mqtt_pass" value=")=====";
const char HTML_SETTINGS_MQTT_CLIENT_ID[] PROGMEM = R"=====(<input class="w-100" id="client_id" type="text" name="client_id" value=")=====";

const char HTML_SETTINGS_HTML_END[] PROGMEM = R"=====(
      </fieldset>
      <fieldset>
        <legend>Web interface settings</legend>
        <label for="auth_user">New login:</label>
        <input  class="w-100"  id="auth_user" type="text" name="auth_user"  >

        <label for="auth_pass">New password:</label>
        <input class="w-100"  id="auth_pass" type="text" name="auth_pass" >
      </fieldset>
      <button class="w-100 btn" class="btn"  type="submit">💾 Save</button>
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

const char STATUS_CONNECTED[] PROGMEM = "connected";
const char STATUS_DISCONNECTED[] PROGMEM = "disconnected";
const char MQTT_CONN_STATUS_JSON[] PROGMEM = R"({"mqtt":"%s"})";

class HttpConfigServer
{
private:
  ESP8266WebServer server;
  char authUser[32] = {0};
  char authPass[32] = {0};

  SoftwareSerial *loraSerial = nullptr;
  bool isSerialDebug = false;
  bool mqttConnected = false;

  std::function<void(const char *, const char *, const char *, const char *,
                     const char *, const char *, const char *)>
      saveCallback;
  std::function<void()> resetCredentialsCallback;

  char _mqtt_ip[64] = {0};
  uint16_t _mqtt_port = 1883;

  char _mqtt_user[64] = {0};
  char _mqtt_pass[64] = {0};
  char _client_id[64] = {0};

  void handleRoot();
  void handleSendCommand();
  void handleConfigPage();
  void handleSaveConfig();
  void handleStatus();
  void handleNotFound();
  bool isAuthorized();
  void sendChunk(const char *data);

public:
  HttpConfigServer(int port = 80,
                   std::function<void(const char *, const char *, const char *, const char *,
                                      const char *, const char *, const char *)>
                       credCb = nullptr,
                   std::function<void()> rstCredCb = nullptr);

  void begin();
  void loop();

  // MQTT - state setter
  void setMqttConnected(bool state);

  void setAuth(const char *user, const char *pwd);

  void setLoraSerial(SoftwareSerial *serial);

  void setIsSerialDebug(bool isDebug);

  void setMQTT(const char *mqtt_ip, uint16_t mqtt_port,
               const char *mqtt_user, const char *mqtt_pass,
               const char *client_id);
};

#endif // HTTP_CONFIG_SERVER_H