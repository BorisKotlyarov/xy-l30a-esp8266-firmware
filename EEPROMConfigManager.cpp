#include "EEPROMConfigManager.h"

void EEPROMConfigManager::begin()
{
    EEPROM.begin(EEPROM_SIZE);
}

void EEPROMConfigManager::saveStringToEEPROM(int addr, const char *value)
{
    int i = 0;
    while (value[i] && i < MAX_VALUE_LEN)
    {
        EEPROM.write(addr + i, value[i]);
        i++;
    }
    EEPROM.write(addr + i, '\0');
    EEPROM.commit();
}

void EEPROMConfigManager::readStringFromEEPROM(int addr, char *buffer, size_t maxLen)
{
    int i = 0;
    char c;
    while ((c = EEPROM.read(addr + i)) != '\0' && i < maxLen - 1)
    {
        buffer[i++] = c;
    }
    buffer[i] = '\0';
}

void EEPROMConfigManager::saveWiFiConfig(const char *ssid, const char *pass)
{
    saveStringToEEPROM(OFFSET_WIFI_SSID, ssid);
    saveStringToEEPROM(OFFSET_WIFI_PASS, pass);
}

void EEPROMConfigManager::loadWiFiConfig(char *ssid, char *pass)
{
    readStringFromEEPROM(OFFSET_WIFI_SSID, ssid, MAX_LEN_WIFI_SSID);
    readStringFromEEPROM(OFFSET_WIFI_PASS, pass, MAX_LEN_WIFI_PASSWORD);
}

void EEPROMConfigManager::saveMQTTConfig(const char *server, uint16_t port,
                                         const char *user, const char *pass,
                                         const char *clientId)
{
    saveStringToEEPROM(OFFSET_MQTT_SERVER, server);

    char portStr[6];
    snprintf(portStr, sizeof(portStr), "%u", port);
    saveStringToEEPROM(OFFSET_MQTT_PORT, portStr);

    saveStringToEEPROM(OFFSET_MQTT_USER, user);
    saveStringToEEPROM(OFFSET_MQTT_PASS, pass);
    saveStringToEEPROM(OFFSET_MQTT_CLIENT_ID, clientId);
}

void EEPROMConfigManager::loadMQTTConfig(char *server, uint16_t *port,
                                         char *user, char *pass,
                                         char *clientId)
{
    readStringFromEEPROM(OFFSET_MQTT_SERVER, server, MAX_LEN_MQTT_SERVER);

    char portStr[6] = {0};
    readStringFromEEPROM(OFFSET_MQTT_PORT, portStr, sizeof(portStr));
    if (port)
        *port = atoi(portStr);

    readStringFromEEPROM(OFFSET_MQTT_USER, user, MAX_LEN_MQTT_USER);
    readStringFromEEPROM(OFFSET_MQTT_PASS, pass, MAX_LEN_MQTT_PASSW);
    readStringFromEEPROM(OFFSET_MQTT_CLIENT_ID, clientId, MAX_LEN_MQTT_CLIENT_ID);
}

void EEPROMConfigManager::saveAuth(const char *user, const char *pass)
{
    if (user && strlen(user) > 0 && isAscii(user[0]))
    {
        saveStringToEEPROM(OFFSET_AUTH_USER, user);
    }

    if (pass && strlen(pass) > 0 && isAscii(pass[0]))
    {
        saveStringToEEPROM(OFFSET_AUTH_PASS, pass);
    }
}

void EEPROMConfigManager::loadAuth(char *user, char *pass)
{
    readStringFromEEPROM(OFFSET_AUTH_USER, user, MAX_LEN_AUTH_USER);
    readStringFromEEPROM(OFFSET_AUTH_PASS, pass, MAX_LEN_AUTH_PASSW);
}

void EEPROMConfigManager::resetWiFiCredentials()
{

    for (int i = OFFSET_WIFI_SSID; i < OFFSET_WIFI_PASS + 64; i++)
    {
        EEPROM.write(i, 0); // Обнуляем байты
    }
    EEPROM.commit();
}