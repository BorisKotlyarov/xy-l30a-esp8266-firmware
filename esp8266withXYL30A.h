#ifndef ESP8266_WITH_XY_L30A_H
#define ESP8266_WITH_XY_L30A_H

void loraReader();
void handleXYResponse(const char *line);
void callback(char *topic, byte *payload, unsigned int length);
void connectMQTT(bool force);
void loadConfigFromEEPROM();
void saveConfigToEEPROM(const char *mqtt_ip, const char *mqtt_port,
                        const char *user, const char *mqtt_pass,
                        const char *client_id,
                        const char *auth_user, const char *auth_pass);

void loadAuthFromEEPROM();

void connectToAP(const char *ssid, const char *pass, bool isCheckAttempt);

void blink(int _delay, int num);
void resetWiFiCredentials();
void handleMQTTCommand(const char *action, const char *value);
void publishStatus();

void debugHeap(const char *topic);

#endif