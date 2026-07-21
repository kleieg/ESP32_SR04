// set hostname used for MQTT tag and WiFi
#define HOSTNAME "ESP-SR04"
#define MQTT_BROKER "sym_mqtt"
#define VERSION "v 7.1.0"

#define MQTT_INTERVAL 60000
#define RECONNECT_INTERVAL 5000
#define Connect_test 1000


#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
#define SERIALINIT Serial.begin(115200);
#else
#define SERIALINIT
#endif