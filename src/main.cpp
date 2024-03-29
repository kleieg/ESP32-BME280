#include <Arduino.h>


#include <Wire.h>
#include <Adafruit_BME280.h>
#include <SPI.h>
//#include <Adafruit_Sensor.h>
#include <string>

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "SPIFFS.h"
#include <Arduino_JSON.h>
#include <AsyncElegantOTA.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#include "WLAN_Credentials.h"
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


Adafruit_BME280 bme; // I2C               

/*
used GPIOs 
BMNE280: SDA = 21  SCL = 22
*/
// variables for BME280
bool status;
int BME280_scanIntervall = 20000;  // in milliseconds
long BME280lastScan = 0;
float BME_Temp;
float BME_Hum;
float BME_Pres;
long  BME_Time;


// variables for LED 
bool led = 1;
long LEDblink = 0;
LEDBLINK


// Create AsyncWebServer object on port 80
AsyncWebServer Asynserver(80);

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
  myArray["cards"][6]["c_text"] = " ";
  myArray["cards"][7]["c_text"] = " to reboot click ok";
  myArray["cards"][8]["c_text"] = String(BME_Hum) +"%";
  myArray["cards"][9]["c_text"] = String(BME_Pres) ;
  myArray["cards"][10]["c_text"] = String(BME_Temp) + "Grad";
  
  String jsonString = JSON.stringify(myArray);
  log_i("%s",jsonString.c_str()); 
  return jsonString;
}

void notifyClients(String state) {
  ws.textAll(state);
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = 0;   // according to AsyncWebServer documentation this is ok

    log_i("Data received: ");
    log_i("%s\n",data);

    JSONVar myObject = JSON.parse((const char *)data);
//    int value;
    int card;
    
    card =  myObject["card"];
//    value =  myObject["value"];
    log_i("%d", card);
//    log_i("%d",value);

    switch (card) {
      case 0:   // fresh connection
        notifyClients(getOutputStates());
        break;
      case 7:
        log_i("Reset..");
        ESP.restart();
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

// init BME280
void setup_BME280() {
  // default settings
  // (you can also pass in a Wire library object like &Wire2)
  status = bme.begin();  
  if (!bme.begin(0x76)) {
    Serial.println("Could not find a valid BME280 sensor, check wiring!");
    while (1);
  }
  log_i("%s\n","BME280 sensor connected");
  bme.setSampling(Adafruit_BME280::MODE_FORCED, // takeForcedMeasurement must be called before each reading
  Adafruit_BME280::SAMPLING_X1, // Temp. oversampling
  Adafruit_BME280::SAMPLING_X1, // Pressure oversampling
  Adafruit_BME280::SAMPLING_X1, // Humidity oversampling
  Adafruit_BME280::FILTER_OFF,
  Adafruit_BME280::STANDBY_MS_1000);
  log_i("%s\n","BME280 sensor initialized");

  bme.takeForcedMeasurement();
  BME_Temp = bme.readTemperature();
  BME_Hum = bme.readHumidity();
  BME_Pres = bme.readPressure() / 100.0F;  // /100.0F  >> forces floating point division
  log_i("%s\n","BME280 Startwert für exponentielle Glättung erstellt");
}


// read BME280 values
void BME280_scan() {

  bme.takeForcedMeasurement();
  BME_Temp = BME_Temp + (bme.readTemperature() - BME_Temp) * 0.3;
  BME_Hum = BME_Hum + (bme.readHumidity() - BME_Hum) * 0.3;
  BME_Pres = BME_Pres + (bme.readPressure() / 100.0F  - BME_Pres) * 0.3;  // /100.0F  >> forces floating point division


  notifyClients(getOutputStates());
}

// send data using Mqtt 
void MQTTsend () {

  int i;
  JSONVar mqtt_data,  actuators;

  String mqtt_tag = Hostname + "/STATUS";
  log_i("%s\n", mqtt_tag.c_str());

  mqtt_data["Time"] = My_time;
  mqtt_data["RSSI"] = WiFi.RSSI();
  mqtt_data["Temp"] = round (BME_Temp*10) / 10;
  mqtt_data["Hum"] = round (BME_Hum*10) / 10;
  mqtt_data["Pres"] = round (BME_Pres*10) / 10;

  String mqtt_string = JSON.stringify(mqtt_data);

  log_i("%s\n", mqtt_string.c_str());

  Mqttclient.publish(mqtt_tag.c_str(), mqtt_string.c_str());

  notifyClients(getOutputStates());

}

void setup() {

  SERIALINIT
  
  log_i("setup device\n");

  // initialise LED
  pinMode(espLed, OUTPUT);


  log_i("setup WiFi\n");
  initWiFi();

  log_i("setup MQTT\n");
  Mqttclient.setServer(MQTT_BROKER, 1883);

  log_i("setup BME280\n");
  setup_BME280() ;
  delay(10);
  BME280_scan();

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

  // LED blink
  if (now - LEDblink > 200) {
    LEDblink = now;
    if(led == 0) {
      digitalWrite(espLed, 1);  // LED aus
      led = 1;
    }else{
      digitalWrite(espLed, 0);  // LED ein
      led = 0;
    }
  }

  // perform BME280 scan  
   if (now - BME280lastScan > BME280_scanIntervall) {
    BME280lastScan = now;
    BME280_scan();
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
  if (!Mqttclient.connected()) {
    // try reconnect every 5 seconds
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      // Attempt to reconnect
      reconnect_mqtt();
    }
  } else {
    // Client connected

    Mqttclient.loop();

    // send data to MQTT broker
    if (now - Mqtt_lastSend > MQTT_INTERVAL) {
    Mqtt_lastSend = now;
    MQTTsend();
    } 
  }   
}
