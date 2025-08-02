#ifndef CONFIG_XYL30A_ESPMQTT_CLIENT_H
#define CONFIG_XYL30A_ESPMQTT_CLIENT_H

// Настройки
const char *DEFAULT_USER = "admin";
const char *DEFAULT_PASS = "123456";

const uint8_t MQTT_QOS = 1;
const bool MQTT_RETAIN = true;

// Топики
const char STATUS_TOPIC[] PROGMEM = "device/status"; // Унифицированное имя
const char COMMAND_TOPIC[] PROGMEM = "device/command";

// Статусы
const char OFFLINE_STATUS[] PROGMEM = "offline";
const char ONLINE_STATUS[] PROGMEM = "online";

// Шаблоны JSON
const char LAST_WILL_JSON[] PROGMEM = R"({"status":"%s","device_id":"%s"})";

const char STATUS_JSON[] PROGMEM = R"({"status":"%s","ip":"%s","rssi":%d,"uptime":"%s","device_id":"%s"})";

// Сообщения
const char MSG_TOPIC[] PROGMEM = "topic: %s";
const char MSG_JSON_ERROR[] PROGMEM = "⚠️ JSON error: %s";
const char MSG_DEVICE_ID[] PROGMEM = "device_id: %s (local: %s)";
const char MSG_MQTT_CMD[] PROGMEM = "📥 MQTT cmd: %s → %s";
const char MSG_UNKNOWN_CMD[] PROGMEM = "⚠️ Unknown command: %s";

// Топики MQTT для XY-L30A/XY-L10A
const char TOPIC_XY_DATA[] PROGMEM = "esp/data";
const char TOPIC_XY_CONFIG[] PROGMEM = "esp/config";
const char TOPIC_XY_RAW[] PROGMEM = "esp/raw";

#endif