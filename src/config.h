// set hostname used for MQTT tag and WiFi
#define HOSTNAME "ESP-BME280-2"
#define MQTT_BROKER "192.168.178.15"
#define VERSION "v 0.11.0"

#define MQTT_INTERVAL 120000
#define RECONNECT_INTERVAL 5000

#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
#define SERIALINIT Serial.begin(115200);
#define LEDBLINK int espLed = 5;
#else
#define SERIALINIT
#define LEDBLINK int espLed = 1;
#endif