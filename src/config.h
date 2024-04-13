// set hostname used for MQTT tag and WiFi
#define HOSTNAME "ESP-SR04"
#define MQTT_BROKER "192.168.178.15"
#define VERSION "v 6.0.0"

#define MQTT_INTERVAL 120000
#define RECONNECT_INTERVAL 5000
#define LED_BLINK_INTERVAL 500
#define RELAY_RESET_INTERVAL 5000

#define GPIO_LED 1

#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
#define SERIALINIT Serial.begin(115200);
#else
#define SERIALINIT
#endif