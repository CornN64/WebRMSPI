// WP (c) 2025
// V1.8.19 or less use Tools -> ESP32 sketch Data Upload.
// V2.0 or higher use [Ctrl] + [Shift] + [P], then "Upload SPIFFS to Pico/ESP8266/ESP32".
// ---------------------------------------------------------------------------------------
// Plots RM3100 data an webserver chart with ESP32 
// Arduino 1.8.19 to make SPIFFS upload tool to work and upload the data folder files for the webserver
// ESP lib 1.0.4 from expressif
// The following to libraries needs installed to the "Arduino\libraries" folder (Required for ESPAsyncWebServer)
// https://github.com/me-no-dev/ESPAsyncWebServer 3.1.0
// https://github.com/me-no-dev/AsyncTCP 1.1.4
// "WebSockets by Markus Sattler" 2.2.0
// ArduinoJson benoit 6.21.5
// Required to make SPIFFS.h work:
// https://github.com/me-no-dev/arduino-esp32fs-plugin/releases/
// https://randomnerdtutorials.com/install-esp32-filesystem-uploader-arduino-ide/
//
// RM3100 sample rates in RM_TMRC register
//'0x92'  '0x93'  '0x94'  '0x95'  '0x96'  '0x97'  '0x98'  '0x99'  '0x9A'  '0x9B'  '0x9C'  '0x9D'  '0x9E'  '0x9F'  RM_TMRC register setting
//  600     300     150      75      37      18       9     4.5     2.3     1.2     0.6     0.3   0.015   0.075   approx. update rate in Hz

#include "freertos/FreeRTOS.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <Arduino.h>
#include <SPI.h>
#include "config.h"
#ifdef OTAenable
#include <ArduinoOTA.h>
#endif

//Registers
#define RM_POLL       0x00
#define RM_CMM        0x01
#define RM_CCXMSB     0x04
#define RM_CCXLSB     0x05
#define RM_CCYMSB     0x06
#define RM_CCYLSB     0x07
#define RM_CCZMSB     0x08
#define RM_CCZLSB     0x09
#define RM_NOS        0x0A
#define RM_TMRC       0x0B
#define RM_MX2        0xA4 // x-axis
#define RM_MX1        0xA5
#define RM_MX0        0xA6
#define RM_MY2        0xA7 // y-axis
#define RM_MY1        0xA8
#define RM_MY0        0xA9
#define RM_MZ2        0xAA // z-axis
#define RM_MZ1        0xAB
#define RM_MZ0        0xAC
#define RM_STAT       0x34 //status of DRDY
#define RM_REVID      0x36 //ID
#define RMgain 1.f/((float)AVG * (0.3671f * (float)CYCLECOUNT + 1.5f)) //calculate the gain from cycle count

// global variables
volatile bool NEWDATA = false;
int plotdata = 2;
int X_vals[ARRAY_LENGTH];
int Y_vals[ARRAY_LENGTH];
int Z_vals[ARRAY_LENGTH];
//int B_vals[ARRAY_LENGTH];
  
// Initialization of webserver and websocket
AsyncWebServer server(80);                            // the server uses port 80 (standard port for websites
WebSocketsServer webSocket = WebSocketsServer(81);    // the websocket uses port 81 (standard port for websockets

float StDev(int32_t *data, int32_t n) {
    float mean = 0.f;
    for (int i = 0; i < n; i++) mean += data[i];
    mean /= n;

    float variance = 0.f;
    for (int i = 0; i < n; i++) variance += (data[i] - mean) * (data[i] - mean);
    //variance /= n;  // for population stddev
    variance /= (n - 1); // for sample stddev (unbiased)

    return sqrtf(variance);
}

//Median-Mean filter
float IRAM_ATTR QQSort(int32_t *arr, int32_t n) {
#if SR%2
    const int32_t avgs[] = {SR/2-2, SR/2-1, SR/2+0, SR/2+1, SR/2+2};
#else
    const int32_t avgs[] = {SR/2-2, SR/2-1, SR/2+0, SR/2+1};
#endif
    const int avgscount = ((sizeof avgs) / (sizeof *avgs));
    int32_t stack[64];  // manual stack for recursion-free quicksort
    int32_t top = -1;
    stack[++top] = 0;
    stack[++top] = n - 1;

    while (top >= 0) {
        int32_t high = stack[top--];
        int32_t low = stack[top--];

        int32_t i = low, j = high;
        int32_t pivot = arr[(low + high) / 2];

        while (i <= j) {
            while (arr[i] < pivot) i++;
            while (arr[j] > pivot) j--;
            if (i <= j) {
                int32_t temp = arr[i];
                arr[i] = arr[j];
                arr[j] = temp;
                i++;
                j--;
            }
        }

        if (low < j) {
            stack[++top] = low;
            stack[++top] = j;
        }
        if (i < high) {
            stack[++top] = i;
            stack[++top] = high;
        }
    }
    float accu = 0.f;
    for(int i=0; i < avgscount; i++) {
      accu += (float) arr[avgs[i]];
    }
    return (RMgain * accu / (float)avgscount);    
}

uint8_t IRAM_ATTR readReg(uint8_t addr){
  uint8_t data = 0;
  digitalWrite(CS_GPIO, LOW);
  SPI.transfer(addr | 0x80);
  data = SPI.transfer(0);
  digitalWrite(CS_GPIO, HIGH);
  return data;
}

void IRAM_ATTR writeReg(uint8_t addr, uint8_t data){
  digitalWrite(CS_GPIO, LOW); 
  SPI.transfer(addr & 0x7F);
  SPI.transfer(data);
  digitalWrite(CS_GPIO, HIGH);
}

void webSocketEvent(byte num, WStype_t type, uint8_t * payload, size_t length) {      // the parameters of this callback function are always the same -> num: id of the client who send the event, type: type of message, payload: actual data sent and length: length of payload
  switch (type) {                                     // switch on the type of information sent
    case WStype_DISCONNECTED:                         // if a client is disconnected, then type == WStype_DISCONNECTED
      Serial.println("Client " + String(num) + " disconnected");
      break;
    case WStype_CONNECTED:                            // if a client is connected, then type == WStype_CONNECTED
      Serial.println("Client " + String(num) + " connected");
      // send variables to newly connected web client -> as optimization step one could send it just to the new client "num", but for simplicity I left that out here
      sendJson("plotdata", String(plotdata));
      sendJsonArray("graph_X", X_vals);
      sendJsonArray("graph_Y", Y_vals);
      sendJsonArray("graph_Z", Z_vals);
      break;
    case WStype_TEXT:                                 // if a client has sent data, then type == WStype_TEXT
      // try to decipher the JSON string received
      StaticJsonDocument<200> doc;                    // create JSON container 
      DeserializationError error = deserializeJson(doc, payload);
      if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        return;
      }
      else {
        // JSON string was received correctly, so information can be retrieved:
        const char* l_type = doc["type"];
        const int l_value = doc["value"];
        Serial.println(String(l_type) + String(l_value));

        // if plotdata value is received -> update and write to all web clients
        if(String(l_type) == "plotdata") {
          plotdata = int(l_value);
          sendJson("plotdata", String(l_value));
        }
      }
      break;
  }
}

// Simple function to send information to the web clients
void sendJson(String l_type, String l_value) {
    String jsonString = "";                           // create a JSON string for sending data to the client
    StaticJsonDocument<200> doc;                      // create JSON container
    JsonObject object = doc.to<JsonObject>();         // create a JSON Object
    object["type"] = l_type;                          // write data into the JSON object
    object["value"] = l_value;
    serializeJson(doc, jsonString);                   // convert JSON object to string
    webSocket.broadcastTXT(jsonString);               // send JSON string to all clients
}

// Simple function to send information to the web clients
void sendJsonArray(String l_type, int l_array_values[]) {
    String jsonString = "";                           // create a JSON string for sending data to the client
    const size_t CAPACITY = JSON_ARRAY_SIZE(ARRAY_LENGTH) + 100;
    StaticJsonDocument<CAPACITY> doc;                 // create JSON container
    
    JsonObject object = doc.to<JsonObject>();         // create a JSON Object
    object["type"] = l_type;                          // write data into the JSON object
    JsonArray value = object.createNestedArray("value");
    for(int i=0; i<ARRAY_LENGTH; i++) {
      value.add(l_array_values[i]);
    }
    serializeJson(doc, jsonString);                   // convert JSON object to string
    webSocket.broadcastTXT(jsonString);               // send JSON string to all clients
}

//Web server task
void Task( void * parameter ) {
  if (!SPIFFS.begin()) {
    Serial.println("SPIFFS could not initialize");
  }

  if (USEAP){  //Accesspoint or DHCP
    Serial.print("Setting up Access Point ... ");
    Serial.println(WiFi.softAPConfig(local_IP, gateway, subnet) ? "Ready" : "Failed!");
  
    Serial.print("Starting Access Point ... ");
    Serial.println(WiFi.softAP(ssid, password) ? "Ready" : "Failed!");
  
    Serial.print("IP address = ");
    Serial.println(WiFi.softAPIP());
  }
  else{
    WiFi.begin(ssid, password);
    Serial.println("Looking for WiFi with SSID: " + String(ssid));
    while (WiFi.status() != WL_CONNECTED) {
      delay(1000);
      Serial.print(".");
    }
    Serial.print("Connected with IP: ");
    Serial.println(WiFi.localIP());
  }
  
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {    // define here wat the webserver needs to do
    request->send(SPIFFS, "/webpage.html", "text/html");           
  });

  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "File not found");
  });

  server.serveStatic("/", SPIFFS, "/");
  webSocket.begin();                                  // start websocket
  webSocket.onEvent(webSocketEvent);                  // define a callback function -> what does the ESP32 need to do when an event from the websocket is received? -> run function "webSocketEvent()"
  server.begin();                                     // start server -> best practise is to start the server after the websocket
  printf("Webserver on Core: %d\n", xPortGetCoreID());

#ifdef OTAenable
  ArduinoOTA.begin();  // Starts OTA
#endif
  
  for (;;) {
    webSocket.loop();                                 // call webSockets
#ifdef OTAenable
    ArduinoOTA.handle();  // Handles a code update request
#endif    
    if (NEWDATA) {
      NEWDATA = false;
      sendJsonArray("graph_X", X_vals);
      sendJsonArray("graph_Y", Y_vals);
      sendJsonArray("graph_Z", Z_vals);
    }
  vTaskDelay(1 / portTICK_PERIOD_MS); //allow a task switch on this core
  }
}

void setup() {
  Serial.begin(115200); // init serial port for debugging
  pinMode(DRDY_GPIO, INPUT);  
  pinMode(CS_GPIO, OUTPUT);
  digitalWrite(CS_GPIO, HIGH);
  //SPI.begin(); // Initiate the SPI library
  SPI.begin(CLK_GPIO, MISO_GPIO, MOSI_GPIO, CS_GPIO); // Initiate the SPI library
  SPI.beginTransaction(SPISettings(SPISPD, MSBFIRST, SPI_MODE0)); 

  // Check REVID register first, should return 0x22.
  uint8_t ID = readReg(RM_REVID);
  if (ID != 0x22) Serial.print("NOT CORRECT REVID:0x");
  else Serial.print("RM3100 detected REVID:0x");
  Serial.println(ID, HEX);

  xTaskCreatePinnedToCore(Task, "core0", 2*16384, NULL, tskIDLE_PRIORITY, NULL, 0); //Start task
  //xTaskCreate(Task, "anycore", 2*16384, NULL, tskIDLE_PRIORITY, NULL); //Start task

  // Set up cycle counts
  digitalWrite(CS_GPIO, LOW); 
  SPI.transfer(RM_CCXMSB);
  SPI.transfer(0xFF & (CYCLECOUNT >> 8));
  SPI.transfer(0xFF & (CYCLECOUNT));
  SPI.transfer(0xFF & (CYCLECOUNT >> 8));
  SPI.transfer(0xFF & (CYCLECOUNT));
  SPI.transfer(0xFF & (CYCLECOUNT >> 8));
  SPI.transfer(0xFF & (CYCLECOUNT));
  digitalWrite(CS_GPIO, HIGH);
 
  writeReg(RM_TMRC, 0x96); //update rate 0x96 ~36Hz

  if (singleMode){
    writeReg(RM_CMM, 0x00); //set up single measurement mode
  }
  else{
    writeReg(RM_CMM, 0x71); //Enable continuous measurement (0x79?)
  }
  printf("RM3100 on Core: %d\n", xPortGetCoreID());
}

void IRAM_ATTR loop() {
  static int32_t xv[SR], yv[SR], zv[SR];
  static float x=0.f, y=0.f, z=0.f; // average in uT
  static int32_t sidx = SR-1, avgcnt = AVG;
  static bool first = true;
  
  // poll the RM3100 for a single measurement
  if (singleMode){
    writeReg(RM_POLL, 0x70); //set up single measurement mode
  }

  //wait until data is ready
  if(useDRDYPin){ 
    while(digitalRead(DRDY_GPIO) == LOW) delay(1); //check RDRY pin
  }
  else{
    while((readReg(RM_STAT) & 0x80) == 0) delay(1); //read internal status register
  }

  // Grab sensor data and format results, this will cause DRDY to go low
  digitalWrite(CS_GPIO, LOW);
  SPI.transfer(RM_MX2);	// 3 x 24bit
  xv[sidx] = ((int32_t)((SPI.transfer(RM_MX1) << 24) | (SPI.transfer(RM_MX0) << 16) | (SPI.transfer(RM_MY2) << 8)) >> 8);
  yv[sidx] = ((int32_t)((SPI.transfer(RM_MY1) << 24) | (SPI.transfer(RM_MY0) << 16) | (SPI.transfer(RM_MZ2) << 8)) >> 8);
  zv[sidx] = ((int32_t)((SPI.transfer(RM_MZ1) << 24) | (SPI.transfer(RM_MZ0) << 16) | (SPI.transfer(0     ) << 8)) >> 8);
  digitalWrite(CS_GPIO, HIGH);

  if (sidx-- <= 0) {
    sidx = SR-1;
    x = x + QQSort(xv, SR);
    y = y + QQSort(yv, SR);
    z = z + QQSort(zv, SR);
    if (--avgcnt <= 0) {
      avgcnt = AVG;
      float Btot = sqrtf(x*x+y*y+z*z);
      Serial.print( "B:"); Serial.print(Btot,5);  //scale ints to uT
      Serial.print("\tX:"); Serial.print(x,5);
      Serial.print("\tY:"); Serial.print(y,5);
      Serial.print("\tZ:"); Serial.println(z,5);
      if (first) {
        for(int i = 0; i<ARRAY_LENGTH-1; i++){
          //B_vals[i] = roundf(1e5f*Btot);
          X_vals[i] = roundf(1e5f*x);
          Y_vals[i] = roundf(1e5f*y);
          Z_vals[i] = roundf(1e5f*z);
        }
        first = false;
      }
      else {
        for(int i = 0; i<ARRAY_LENGTH-1; i++){
          //B_vals[i] = B_vals[i+1];
          X_vals[i] = X_vals[i+1];
          Y_vals[i] = Y_vals[i+1];
          Z_vals[i] = Z_vals[i+1];
        }
      }
      //B_vals[ARRAY_LENGTH-1] = roundf(1e5f*Btot); //scale ints to 10pT
      X_vals[ARRAY_LENGTH-1] = roundf(1e5f*x);
      Y_vals[ARRAY_LENGTH-1] = roundf(1e5f*y);
      Z_vals[ARRAY_LENGTH-1] = roundf(1e5f*z);
      x = 0.f; y = 0.f; z = 0.f;
      NEWDATA = true; //let webserver send over the data
    }
  }
 }
