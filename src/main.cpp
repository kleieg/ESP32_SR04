
#include <Arduino.h>

#include <driver/adc.h>

#include <esp_system.h>
#include <string>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "SPIFFS.h"
#include <Arduino_JSON.h>
#include <AsyncElegantOTA.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#include "WLAN_Credentials_Shelly.h"
#include "config.h"
#include "wifi_mqtt.h"

// NTP
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
long My_time = 0;
long Start_time;
long Up_time;
long U_days;
long U_hours;
long U_min;
long U_sec;

// Timers auxiliar variables
long now = millis();
char strtime[8];
int LEDblink = 0;
bool led = 1;


// variables for HC-SR04
int trigPin = 32;    // Trigger
int echoPin = 33;    // Echo
long duration;
int SR04_cm;
long SR04_lastScan;
int SR04_scanInterval = 250;  // in milliseconds 
long  SR04_time = 0;
int SR04_cm_min = 95;  // 95
int SR04_cm_max = 110; //110

    
// Create AsyncWebServer object on port 80
AsyncWebServer Asynserver(80);

// Create a WebSocket object
AsyncWebSocket ws("/ws");

// end of definitions -----------------------------------------------------

// Initialize SPIFFS
void initSPIFFS() {
  if (!SPIFFS.begin()) {
    log_i("An error has occurred while mounting LittleFS");
  }
  log_i("LittleFS mounted successfully");
}

String getOutputStates(){
  JSONVar myArray;
  
  U_days = Up_time / 86400;
  U_hours = (Up_time % 86400) / 3600;
  U_min = (Up_time % 3600) / 60;
  U_sec = (Up_time % 60);

  myArray["cards"][0]["c_text"] = Hostname;
  myArray["cards"][1]["c_text"] = WiFi.dnsIP().toString() + "   /   " + String(VERSION);
  myArray["cards"][2]["c_text"] = String(WiFi.RSSI());
  myArray["cards"][3]["c_text"] = String(MQTT_INTERVAL) + "ms";
  myArray["cards"][4]["c_text"] = String(U_days) + " days " + String(U_hours) + ":" + String(U_min) + ":" + String(U_sec);
  myArray["cards"][5]["c_text"] = "WiFi = " + String(WiFi_reconnect) + "   MQTT = " + String(Mqtt_reconnect);
  myArray["cards"][6]["c_text"] = String(SR04_cm);
  myArray["cards"][7]["c_text"] = " to reboot click ok";
  myArray["cards"][8]["c_text"] = String(SR04_scanInterval) + "ms";
  myArray["cards"][9]["c_text"] = String(SR04_cm_min) + "cm";
  myArray["cards"][10]["c_text"] = String(SR04_cm_max) + "cm";
  
  String jsonString = JSON.stringify(myArray);
  return jsonString;
}

void notifyClients(String state) {
  ws.textAll(state);
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = 0;   // according to AsyncWebServer documentation this is ok
    int card;
    int value;

    log_i("Data received: ");
    log_i("%s\n",data);

    JSONVar myObject = JSON.parse((const char *)data);

    card =  myObject["card"];
    value =  myObject["value"];
    log_i("%d", card);
    log_i("%d",value);

    switch (card) {
      case 0:   // fresh connection
        notifyClients(getOutputStates());
        break;
      case 7:
        log_i("Reset..");
        ESP.restart();
        break;
      case 8:
        SR04_scanInterval = value;
        notifyClients(getOutputStates());
        break;
      case 9:
        SR04_cm_min = value;
        notifyClients(getOutputStates());
        break;
      case 10:
        SR04_cm_max = value;
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


void MQTTsend () {

  JSONVar mqtt_data, actuators;

  String mqtt_tag = Hostname + "/STATUS";
  log_i("%s\n", mqtt_tag.c_str());
  
  mqtt_data["Time"] = My_time;
  mqtt_data["RSSI"] = WiFi.RSSI();
  mqtt_data["cm"] =SR04_cm;
  mqtt_data["Time"] = SR04_time;

  String mqtt_string = JSON.stringify(mqtt_data);

  log_i("%s\n", mqtt_string.c_str());

  mqttClient.publish(mqtt_tag.c_str(), mqtt_string.c_str());

  notifyClients(getOutputStates());
}

// receive MQTT messages
void MQTT_callback(String &topic, String &payload) {
  
  log_i("%s","Message arrived on topic: ");
  log_i("%s\n",topic);
  log_i("%s","Data : ");
  log_i("%s\n",payload);

  notifyClients(getOutputStates());
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
  log_i("duration %d",duration);
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
      Mqtt_lastSend = 0;               // send immedately
      SR04_lastScan = now + 10000;     // 10 sekunden kein Scan
    }
  }
  log_i("cm %d",SR04_cm);

  SR04_time = My_time;   

  notifyClients(getOutputStates());
}    


// setup 
void setup() {
  
  SERIALINIT                                 

  log_i("setup device\n");

  pinMode(GPIO_LED, OUTPUT);
  digitalWrite(GPIO_LED,led);

  log_i("setup WiFi\n");
  initWiFi();

  log_i("setup MQTT\n");
  initMQTT();
  mqttClient.onMessage(MQTT_callback);


  //Define inputs and outputs fpr HC-SR04
  log_i("setup HC_SR04\n");
  pinMode(trigPin, OUTPUT);
  digitalWrite(trigPin, LOW);
  pinMode(echoPin, INPUT);


  initSPIFFS();

    // init Websocket
  ws.onEvent(onEvent);
  Asynserver.addHandler(&ws);

  // Route for root / web page
  Asynserver.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html", "text/html",false);
  });

  Asynserver.serveStatic("/", SPIFFS, "/");

  timeClient.begin();
  timeClient.setTimeOffset(0);
  // update UPCtime for Starttime
  timeClient.update();
  Start_time = timeClient.getEpochTime();

  // Start ElegantOTA
  AsyncElegantOTA.begin(&Asynserver);
  
  // Start server
  Asynserver.begin();

}

void loop() {
  
  ws.cleanupClients();

  // update UPCtime
  timeClient.update();
  My_time = timeClient.getEpochTime();
  Up_time = My_time - Start_time;

  now = millis();

  // LED blinken
  if (now - LEDblink > 2000) {
    LEDblink = now;
    if(led == 0) {
      digitalWrite(GPIO_LED, 1);
      led = 1;
    }else{
      digitalWrite(GPIO_LED, 0);
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


  // check if MQTT broker is still connected
  if (!mqttClient.connected()) {
    // try reconnect every 5 seconds
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      // Attempt to reconnect
      reconnect_mqtt();
    }
  } else {
    // Client connected

    mqttClient.loop();

    // send data to MQTT broker
    if (now - Mqtt_lastSend > MQTT_INTERVAL) {
    Mqtt_lastSend = now;
    MQTTsend();
    } 
  }   
}