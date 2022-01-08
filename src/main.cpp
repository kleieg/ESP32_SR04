/*  Changelog

10.4.21 neues Format mit Webserver
Online / Offline mit Last Will in MQTT eingeführt




*/

#include <Arduino.h>

#include <driver/adc.h>

#include <WiFi.h>
#include <PubSubClient.h>
#include <esp_system.h>
#include <string>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "SPIFFS.h"
#include <Arduino_JSON.h>
#include <AsyncElegantOTA.h>

#include "WLAN_Credentials.h"


#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
#define SERIALINIT Serial.begin(115200);
#else
#define SERIALINIT
#endif

//<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<   Anpassungen !!!!
// set hostname used for MQTT tag and WiFi 
#define HOSTNAME "SR04"
#define VERSION "v 1.1.2"


// variables to connects to  MQTT broker
const char* mqtt_server = "192.168.178.15";
const char* willTopic = "tele/SR04/LWT";       // muss mit HOSTNAME passen !!!  tele/HOSTNAME/LWT    !!!

//<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<   Anpassungen Ende !!!!

int WiFi_reconnect = 0;

// for MQTT
byte willQoS = 0;
const char* willMessage = "Offline";
boolean willRetain = true;
std::string mqtt_tag;
int Mqtt_sendInterval = 20000;   // in milliseconds
long Mqtt_lastScan = 0;
long lastReconnectAttempt = 0;
int Mqtt_reconnect = 0;

// Define NTP Client to get time
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 0;
const int   daylightOffset_sec = 0;
int UTC_syncIntervall = 3600000; // in milliseconds = 1 hour
long UTC_lastSync;

// Initializes the espClient. 
WiFiClient myClient;
PubSubClient client(myClient);
// name used as Mqtt tag
std::string gateway = HOSTNAME ;                           

// Timers auxiliar variables
long now = millis();
char strtime[8];
int LEDblink = 0;
bool led = 1;
int esp32LED = 1;
time_t UTC_time;

// variables for HC-SR04
int trigPin = 32;    // Trigger
int echoPin = 33;    // Echo
long duration;
int SR04_cm;
long SR04_lastScan;
int SR04_scanInterval = 250;  // in milliseconds 
long  SR04_time = 0;
int SR04_cm_min = 95;
int SR04_cm_max = 110;

    

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Create a WebSocket object
AsyncWebSocket ws("/ws");

// end of definitions -----------------------------------------------------

// Initialize SPIFFS
void initSPIFFS() {
  if (!SPIFFS.begin()) {
    // Serial.println("An error has occurred while mounting LittleFS");
  }
  // Serial.println("LittleFS mounted successfully");
}

String getOutputStates(){
  JSONVar myArray;
  
  myArray["cards"][0]["c_text"] = String(HOSTNAME) + "   /   " + String(VERSION);
  myArray["cards"][1]["c_text"] = willTopic;
  myArray["cards"][2]["c_text"] = String(WiFi.RSSI());
  myArray["cards"][3]["c_text"] = String(Mqtt_sendInterval) + "ms";
  myArray["cards"][4]["c_text"] = String(SR04_cm);
  myArray["cards"][5]["c_text"] = String(SR04_time);
  myArray["cards"][6]["c_text"] = "WiFi = " + String(WiFi_reconnect) + "   MQTT = " + String(Mqtt_reconnect);
  myArray["cards"][7]["c_text"] = String(SR04_scanInterval) + "ms";
  myArray["cards"][8]["c_text"] = String(SR04_cm_min) + "cm";
  myArray["cards"][9]["c_text"] = String(SR04_cm_max) + "cm";
  
  String jsonString = JSON.stringify(myArray);
  return jsonString;
}

void notifyClients(String state) {
  ws.textAll(state);
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = 0;
    char help[30];
    int card;
    int value;
    
    for (int i = 0; i <= len; i++){
      help[i] = data[i];
    }

    log_i("Data received: ");
    log_i("%s\n",help);

    JSONVar myObject = JSON.parse(help);

    card =  myObject["card"];
    value =  myObject["value"];
    log_i("%d", card);
    log_i("%d",value);

    switch (card) {
      case 0:   // fresh connection
        notifyClients(getOutputStates());
        break;
      case 6:
        SR04_scanInterval = value;
        notifyClients(getOutputStates());
        break;
      case 7:
        SR04_cm_min = value;
        notifyClients(getOutputStates());
        break;
      case 8:
        SR04_cm_min = value;
        notifyClients(getOutputStates());
        break;
    }
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      log_i("WebSocket client connected");
      break;
    case WS_EVT_DISCONNECT:
      log_i("WebSocket client disconnected");
      break;
    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len);
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

void initWebSocket() {
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

// init WiFi
void setup_wifi() {

  delay(10);
  digitalWrite(esp32LED, 0); 
  delay(500);
  digitalWrite(esp32LED, 1); 
  delay(500);
  digitalWrite(esp32LED, 0);
  delay(500);
  digitalWrite(esp32LED, 1);
  log_i("Connecting to ");
  log_i("%s",ssid);
  log_i("%s",password);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
     if(led == 0){
       digitalWrite(esp32LED, 1);  // LED aus
       led = 1;
     }else{
       digitalWrite(esp32LED, 0);  // LED ein
       led = 0;
     }
    log_i(".");
  }

  digitalWrite(esp32LED, 1);  // LED aus
  led = 1;
  log_i("WiFi connected - IP address: ");
  log_i("%s",WiFi.localIP().toString().c_str());

  // get time from NTP-server
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);  // update ESP-systemtime to UTC
  delay(50);                                                 // udate takes some time
  time(&UTC_time);
  log_i("%s","Unix-timestamp =");
  itoa(UTC_time,strtime,10);
  log_i("%s",strtime);
}


// reconnect to WiFi 
void reconnect_wifi() {
  log_i("%s\n","WiFi try reconnect"); 
  WiFi.begin();
  delay(500);
  if (WiFi.status() == WL_CONNECTED) {
    WiFi_reconnect = WiFi_reconnect + 1;
    log_i("%s\n","WiFi reconnected"); 
  }
}

// This functions reconnects your ESP32 to your MQTT broker

void reconnect_mqtt() {
  if (client.connect(gateway.c_str(), willTopic, willQoS, willRetain, willMessage)) {
    // Once connected, publish an announcement...
    log_i("%s\n","Mqtt connected"); 
    mqtt_tag = gateway + "/connect";
    client.publish(mqtt_tag.c_str(),"connected");
    log_i("%s",mqtt_tag.c_str());
    log_i("%s\n","connected");
    mqtt_tag = "tele/" + gateway  + "/LWT";
    client.publish(mqtt_tag.c_str(),"Online",willRetain);
    log_i("%s",mqtt_tag.c_str());
    log_i("%s\n","Online");

    Mqtt_reconnect = Mqtt_reconnect + 1;
  }
}

void MQTTsend () {
  JSONVar mqtt_data; 
  JSONVar Mqtt_state;
  
  mqtt_tag = "tele/" + gateway + "/SENSOR";
  log_i("%s",mqtt_tag.c_str());

  mqtt_data["cm"] =SR04_cm;
  mqtt_data["Time"] = SR04_time;
  String mqtt_string = JSON.stringify(mqtt_data);

  log_i("%s",mqtt_string.c_str()); 

  client.publish(mqtt_tag.c_str(), mqtt_string.c_str());

  Mqtt_state["Wifi"]["AP"] = 1;
  Mqtt_state["Wifi"]["RSSI"] = abs(WiFi.RSSI());
  String jsonString2 = JSON.stringify(Mqtt_state);

  mqtt_tag = "tele/" + gateway +  "/STATE" ;

  log_i("%s",mqtt_tag.c_str()); 
  log_i("%s",jsonString2.c_str()); 
  client.publish(mqtt_tag.c_str(), jsonString2.c_str());  
}

//  function for SR04 scan
void SR04_scan () {
  
  // The sensor is triggered by a HIGH pulse of 10 or more microseconds.
  // Clears the trigPin
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);   
  // Sets the trigPin on HIGH state for 10 micro seconds
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  // Read the signal from the sensor: a HIGH pulse whose
  // duration is the time (in microseconds) from the sending
  // of the ping to the reception of its echo off of an object.
  duration = pulseIn(echoPin, HIGH);

  // Convert the time into a distance
  SR04_cm = (duration/2) / 29.1;     // Divide by 29.1 or multiply by 0.0343

  if ( SR04_cm < SR04_cm_min or SR04_cm > SR04_cm_max) {
    // movement my be recognized. Send data immediately.
    // to check do a second measurement
      delay(50);
    // Clears the trigPin
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);   
    // Sets the trigPin on HIGH state for 10 micro seconds
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);
    duration = pulseIn(echoPin, HIGH);
    SR04_cm = (duration/2) / 29.1;     // Divide by 29.1 or multiply by 0.0343
    if ( SR04_cm < SR04_cm_min or SR04_cm > SR04_cm_max) {
      if (client.connected()) {
        log_i("cm %.2f",SR04_cm);
        MQTTsend();
      }
    }
  }
  log_i("cm %.2f",SR04_cm);

  time(&UTC_time);
  itoa(UTC_time,strtime,10);

  SR04_time = UTC_time;   

  notifyClients(getOutputStates());
}    


// setup 
void setup() {
  
  SERIALINIT                                 
  
  log_i("setup device\n");

  pinMode(esp32LED, OUTPUT);
  digitalWrite(esp32LED,led);

  log_i("setup WiFi\n");
  setup_wifi();

  log_i("setup MQTT\n");
  client.setServer(mqtt_server, 1883);


  //Define inputs and outputs fpr HC-SR04
  log_i("setup HC_SR04\n");
  pinMode(trigPin, OUTPUT);
  digitalWrite(trigPin, LOW);
  pinMode(echoPin, INPUT);


  initSPIFFS();
  initWebSocket();

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html", "text/html",false);
  });

  server.serveStatic("/", SPIFFS, "/");

  // Start ElegantOTA
  AsyncElegantOTA.begin(&server);
  
  // Start server
  server.begin();

}

void loop() {

  AsyncElegantOTA.loop();
  ws.cleanupClients();

  now = millis();

  // LED blinken
  if (now - LEDblink > 2000) {
    LEDblink = now;
    if(led == 0) {
      digitalWrite(esp32LED, 1);
      led = 1;
    }else{
      digitalWrite(esp32LED, 0);
      led = 0;
    }
  }

  // perform SR04 scan
  if (now - SR04_lastScan > SR04_scanInterval) {
    SR04_lastScan = now;
    SR04_scan();
  } 

  // check WiFi
  if (WiFi.status() != WL_CONNECTED  ) {
    // try reconnect every 5 seconds
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;              // prevents mqtt reconnect running also
      // Attempt to reconnect
      reconnect_wifi();
    }
  }

  // perform UTC sync
  if (now - UTC_lastSync > UTC_syncIntervall) {
    UTC_lastSync = now;
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);  // update ESP-systemtime to UTC
    delay(50);                                                 // udate takes some time
    time(&UTC_time);
    log_i("%s","Re-sync ESP-time!! Unix-timestamp =");
    itoa(UTC_time,strtime,10);
    log_i("%s",strtime);
  }   

  // check if MQTT broker is still connected
  if (!client.connected()) {
    // try reconnect every 5 seconds
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      // Attempt to reconnect
      reconnect_mqtt();
    }
  } else {
    // Client connected

    client.loop();

    // send data to MQTT broker
    if (now - Mqtt_lastScan > Mqtt_sendInterval) {
    Mqtt_lastScan = now;
    MQTTsend();
    } 
  }   
}