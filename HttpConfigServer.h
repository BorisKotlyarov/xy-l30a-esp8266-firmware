#ifndef HTTP_CONFIG_SERVER_H
#define HTTP_CONFIG_SERVER_H

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <SoftwareSerial.h>

class HttpConfigServer {
private:
    ESP8266WebServer server;
    String authUser;
    String authPass;
    
    SoftwareSerial* loraSerial = nullptr;
    bool isSerialDebug = false;
    bool mqttConnected = false;

    std::function<void(const String&, const String&, const String&, const String&, const String&, const String&, const String&)> saveCallback;
    std::function<void()> resetCredentialsCallback;

    String _mqtt_ip = "";
    String _mqtt_port = "";
    String _mqtt_user = "";
    String _mqtt_pass = "";
    String _client_id = "";


    void handleRoot();
    void handleSendCommand();
    void handleConfigPage();
    void handleSaveConfig();
    void handleStatus();
    void handleNotFound();
    bool isAuthorized();
    

public:
    HttpConfigServer(int port = 80,
        std::function<void(const String&, const String&, const String&, const String&, const String&, const String&, const String&)> credCb = nullptr,
        std::function<void()> rstCredCb = nullptr);


    void begin();
    void loop();

    // MQTT - state setter
    void setMqttConnected(bool state);
    
    void setAuth(String user, String pwd);    
    
    void setLoraSerial(SoftwareSerial* serial);

    void setIsSerialDebug(bool isDebug);    

    void setMQTT(String mqtt_ip, String mqtt_port, String mqtt_user, String mqtt_pass, String client_id);
};


#endif // HTTP_CONFIG_SERVER_H