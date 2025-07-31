#ifndef ESP8266_WITH_XY_L30A_H
#define ESP8266_WITH_XY_L30A_H

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

void loadAuthFromEEPROM();
void saveWiFiConfig(const String &ssid, const String &pass);
void saveStringToEEPROM(int addr, const String &value);
String readStringFromEEPROM(int addr);
void connectToAP(const char *ssid, const char *pass, bool isCheckAttempt);
void debugEEPROM(int start, int len);
void blink(int _delay, int num);
void resetWiFiCredentials();
void handleMQTTCommand(String action, String value);
void publishStatus();
void waitForStableVcc();
float readVcc();

#endif