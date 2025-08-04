#pragma once
#include <Arduino.h>
#include <EEPROM.h>

class EEPROMConfigManager
{
public:
    static const int EEPROM_SIZE = 512;
    static const int OFFSET_WIFI_SSID = 0;
    static const int OFFSET_WIFI_PASS = 64;
    static const int OFFSET_MQTT_SERVER = 128;
    static const int OFFSET_MQTT_PORT = 192;
    static const int OFFSET_MQTT_USER = 200;
    static const int OFFSET_MQTT_PASS = 264;
    static const int OFFSET_MQTT_CLIENT_ID = 328;
    static const int OFFSET_AUTH_USER = 448;
    static const int OFFSET_AUTH_PASS = 480;

    static const int MAX_VALUE_LEN = 63; // Change it If you want values lenght more then 63

    static const int MAX_LEN_WIFI_SSID = 63;
    static const int MAX_LEN_WIFI_PASSWORD = 63;

    static const int MAX_LEN_MQTT_SERVER = 63;
    static const int MAX_LEN_MQTT_USER = 63;
    static const int MAX_LEN_MQTT_PASSW = 63;
    static const int MAX_LEN_MQTT_CLIENT_ID = 63;

    static const int MAX_LEN_AUTH_USER = 32;
    static const int MAX_LEN_AUTH_PASSW = 32;

    void
    begin();

    void saveStringToEEPROM(int addr, const char *value);
    void readStringFromEEPROM(int addr, char *buffer, size_t maxLen);

    void saveWiFiConfig(const char *ssid, const char *pass);
    void loadWiFiConfig(char *ssid, char *pass);

    void saveMQTTConfig(const char *server, uint16_t port,
                        const char *user, const char *pass,
                        const char *clientId);

    void loadMQTTConfig(char *server, uint16_t *port,
                        char *user, char *pass,
                        char *clientId);

    void saveAuth(const char *user, const char *pass);
    void loadAuth(char *user, char *pass);

    void resetWiFiCredentials();
};