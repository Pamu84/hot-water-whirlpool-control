#include <WiFi.h>  //built-in
#include <AsyncTCP.h> //AsyncTCP-esphome by Hristo Gochkov
#include <ESPAsyncWebServer.h> //ESPAsyncWebServer@^3.8.0
#include <SPIFFS.h> //built-in
#include <ArduinoJson.h> //ArduinoJson by Benoit Blanchon
#include <OneWire.h> //paulstoffregen/OneWire@^2.3.8
#include <DallasTemperature.h> //DallasTemperature by Miles Burton
#include <Preferences.h> //built-in
#include <time.h> //built-in
#include <ElegantOTA.h>
#include <HTTPClient.h>


//Wifi credentials.
const char* ssid = "";
const char* password = "";

//staattinen IP.
const IPAddress local_IP(192, 168, 1, 237);
const IPAddress subnet(255, 255, 255, 0);
const IPAddress gateway(192, 168, 1, 1);

//Muiden laitteiden staattisia IP:osoitteita.
const char* shellyIP = "192.168.1.238";

//Specifications
#define PUMP_PIN 13//28//GPIO 13 //Rele1, 100W pumppu 
#define HEATER_PIN 12//27//GPIO 12 //Rele2, 2kW lämmitin 
#define AIR_PIN 14//26//GPIO 14 //Rele3, ei vielä olemassa, joku lehtipuhallin mahdollisesti
#define HEATER_AUTO_PIN 32//21//GPIO 32 //Pörssisähkökäytölle. Shellyllä on sulkeutuva kosketin, joka ohjaa tämän tulon päälle. Yhdistetty groundiin.
#define LIGHTS_PIN 27//25//GPIO 27 //Rele4,
#define ONE_WIRE_BUS 33//20//GPIO 35 // käyttää onewire kirjastoa
#define PRESSURE_PIN 34//19//GPIO 34 //toimii ADC:llä (?)
#define UV_FILTER_PIN 26//24//GPIO 26 //Rele5, UV-C filter
#define RESERVE 25//23//GPIO 25 //Rele6, varalla

//THese drives the relays
bool pumpState = false;
bool heaterState = false;
bool uvFilterState = false;
bool airState = false;
bool lightsState = false;

//These are used to track if the state was set by automation or manually by user
bool heaterAutoState = false;
bool heaterSetByAuto = false;
bool pumpSetByAuto = false;
bool uvFilterSetByAuto = false;

//Manual selection states (from Websocket or buttons)
bool pumpManual = false;
bool heaterManual = false;
bool airManual = false;
bool uvFilterManual = false;
bool lightsManual = false;

//Temperature, energy, pressure, fetch variables
float poolTemp = 0.0;
float outsideTemp = 0.0;
float setTemp = 40.0;
float tempTolerance = 1.0;
unsigned long lastTempRead = 0;
const long tempReadInterval = 10000;
const long saveInterval = 60000;
unsigned long lastSave = 0;
unsigned long ota_progress_millis = 0;
double energy_total_kwh = 0.0;
double energy_current_w = 0.0;
unsigned long lastShellyFetch = 0;
const long shellyFetchInterval = 60000;
unsigned long lastUpdate = 0;
float currentPressure = 0;

// Device addresses for the two DS18B20 sensors
uint8_t outsideSensorAddress[8] = {0x28, 0xFF, 0x47, 0x4E, 0x78, 0x04, 0x00, 0xF4};
uint8_t poolSensorAddress[8] = {0x28, 0x79, 0x79, 0x46, 0xD4, 0x33, 0x28, 0x79};

/*--- --- Asetuksia --- ---*/
//1-wire setup
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

/*--- ------ ---*/

//AsyncWebserver ja Websocketin asetus
AsyncWebServer server (80); //create Asyncwebserver object on port 80
AsyncWebSocket ws("/ws"); //ESPAsyncWebServer library includes a WebSocket plugin. Create an AsyncWebSocket object called ws to handle the connections on the /ws path.

//Preferences for persistence. 
Preferences prefs;
void savePrefs() {
  prefs.putFloat("setTemp", setTemp);
  prefs.putFloat("tempTolerance", tempTolerance);
  prefs.putBool("pumpState", pumpState);
  prefs.putBool("heaterState", heaterState);
  prefs.putBool("uvFilterState", uvFilterState);
  prefs.putBool("airState", airState);
  prefs.putBool("heaterAutoState", heaterAutoState);
  prefs.putBool("heaterSetByAuto", heaterSetByAuto);
  prefs.putBool("pumpSetByAuto", pumpSetByAuto);
  prefs.putBool("lightsState", lightsState);
}

void sendStatus() {
  JsonDocument doc;
  doc["pool_temp"] = poolTemp;
  doc["outside_temp"] = outsideTemp;
  doc["pump"] = pumpState;
  doc["heater"] = heaterState;
  doc["uvFilter"] = uvFilterState;
  doc["air"] = airState;
  doc["heater_auto"] = heaterAutoState;
  doc["lights"] = lightsState;
  
  // NEW: Manual selection states (what user clicked)
  doc["pumpManual"]   = pumpManual;
  doc["heaterManual"] = heaterManual;
  doc["uvFilterManual"] = uvFilterManual;
  doc["airManual"]      = airManual;
  doc["lightsManual"]   = lightsManual;

  doc["setTemp"] = setTemp;
  doc["tempTolerance"] = tempTolerance;
  doc["energy_total"] = energy_total_kwh;
  doc["energy_current"] = energy_current_w;
  doc["pressure"] = currentPressure;
  doc["rssi"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : -100; //dBm, -100 if disconnected
  String json;
  serializeJson(doc, json);
  
  if (ws.availableForWriteAll()) {
    ws.textAll(json);
  } else {
    Serial.println("WARNING: WebSocket queue full, skipping status send");
  }
}

//Function prototypes
void applyControlLogic() {
  bool contactClosed = (digitalRead(HEATER_AUTO_PIN) == LOW);

  if (heaterAutoState) {
    if (contactClosed && poolTemp < setTemp - tempTolerance) {
      if (!pumpManual) {
        pumpState = true;
        pumpSetByAuto = true;
      }
      if (!heaterManual) {
        heaterState = true;
        heaterSetByAuto = true;
      }
      if (!uvFilterManual) {
        uvFilterState = true;
        uvFilterSetByAuto = true;
      }
    } else if (contactClosed && poolTemp > setTemp + tempTolerance) {
      if (!heaterManual) {
        heaterState = false;
        heaterSetByAuto = false;
      }
      if (!uvFilterManual) {
        uvFilterState = false;
        uvFilterSetByAuto = false;
      }
    } else if (!contactClosed && heaterSetByAuto && !heaterManual) {
      heaterState = false;
      heaterSetByAuto = false;
      if (!uvFilterManual) {
        uvFilterState = false;
        uvFilterSetByAuto = false;
      }
    }
  } else {
    if (heaterState && !heaterManual) {
      if (poolTemp < setTemp - tempTolerance) {
        heaterState = true;
        heaterSetByAuto = true;
        if (!uvFilterManual) {
          uvFilterState = true;
          uvFilterSetByAuto = true;
        }
      } else if (poolTemp > setTemp + tempTolerance) {
        heaterState = false;
        heaterSetByAuto = false;
        if (!uvFilterManual) {
          uvFilterState = false;
          uvFilterSetByAuto = false;
        }
      }
    }
  }

  if ((heaterState || uvFilterState) && !pumpManual) {
    pumpState = true;
    pumpSetByAuto = true;
  } else if (heaterAutoState && contactClosed && poolTemp < setTemp - tempTolerance) {
    if (!pumpManual) {
      pumpState = true;
      pumpSetByAuto = true;
    }
  } else if (heaterAutoState && !pumpManual && pumpSetByAuto) {
    pumpState = false;
    pumpSetByAuto = false;
  }

  digitalWrite(PUMP_PIN, pumpState ? HIGH : LOW);
  digitalWrite(HEATER_PIN, heaterState ? HIGH : LOW);
  digitalWrite(UV_FILTER_PIN, uvFilterState ? HIGH : LOW);
  digitalWrite(AIR_PIN, airState ? HIGH : LOW);
  digitalWrite(LIGHTS_PIN, lightsState ? HIGH : LOW);
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                      AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("WS client #%u connected from %s\n", client->id(),
                  client->remoteIP().toString().c_str());
    sendStatus();  // send full state when a client connects
  }

  else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("WS client #%u disconnected\n", client->id());
  }

  else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      // Convert bytes -> string
      String msg = "";
      for (size_t i = 0; i < len; i++) {
        msg += (char)data[i];
      }
      Serial.println("WS received: " + msg);

      // Parse JSON
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, msg);
      if (error) {
        Serial.println("deserializeJson() failed");
        return;
      }

      const char *param = doc["param"];
      if (!param) return;

      // Handle value (can be bool or number)
      if (doc["value"].is<bool>()) {
        bool val = doc["value"];

        if (strcmp(param, "pump") == 0) {
          pumpState = val;
          pumpManual = val;
        } else if (strcmp(param, "heater") == 0) {
          heaterState = val;
          heaterManual = val;
        } else if (strcmp(param, "uvFilter") == 0) {
          uvFilterState = val;
          uvFilterManual = val;
        } else if (strcmp(param, "air") == 0) {
          airState = val;
          airManual = val;
        } else if (strcmp(param, "lights") == 0) {
          lightsState = val;
          lightsManual = val;
        } else if (strcmp(param, "heater_auto") == 0) {
          heaterAutoState = val;
            if (val) { // Turning on auto mode
            pumpManual = false;
            heaterManual = false;
            pumpSetByAuto = false;
            heaterSetByAuto = false;
        }
      }

      } else if (doc["value"].is<float>() || doc["value"].is<int>()) {
        float val = doc["value"];
        if (strcmp(param, "setTemp") == 0) {
          setTemp = val;
        } else if (strcmp(param, "tempTolerance") == 0) {
          tempTolerance = val;
        }
      }

      // After applying change, broadcast new state to all clients
      applyControlLogic(); // Apply control logic after any change
      sendStatus();
    }
  }
}


//OTA functions
void onOTAStart() {
  return;
}
void onOTAProgress(size_t current, size_t final) {
  if (millis() - ota_progress_millis > 1000) {
    ota_progress_millis = millis();
  }
}
void onOTAEnd(bool success) {
  return;
}

// --- --- --- setup() --- --- ---
void setup() {
  Serial.begin(115200);
  while (!Serial) delay(5); // Wait for Serial to initialize
  
  //Wifi config
  if (!WiFi.config(local_IP, gateway, subnet)) {
    return;
  }
  WiFi.begin(ssid, password);
  //WiFi.setTxPower(WIFI_POWER_8_5dBm); // //ONLY WITH ESP32-C3 super mini. Powers: 8_5dBm, 11dBm, 13dBm, 15dBm, 17dBm, 18_5dBm, 19dBm, 19_5dBm
  unsigned long wifiTimeout = millis() + 15000; // 15s timeout
  while (WiFi.status() != WL_CONNECTED && millis() < wifiTimeout) {
    delay(1000);
  }

  //Websocket event handler
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  
  //OTA calls
  ElegantOTA.begin(&server);
  ElegantOTA.onStart(onOTAStart);
  ElegantOTA.onProgress(onOTAProgress);
  ElegantOTA.onEnd(onOTAEnd);

  //Initialize 1-wire sensors
  sensors.begin();

  //Initialize pins
  pinMode(PUMP_PIN, OUTPUT);
  pinMode(HEATER_PIN, OUTPUT);
  pinMode(UV_FILTER_PIN, OUTPUT);
  pinMode(AIR_PIN, OUTPUT);
  pinMode(HEATER_AUTO_PIN, INPUT_PULLUP);
  pinMode(LIGHTS_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, LOW);
  digitalWrite(HEATER_PIN, LOW);
  digitalWrite(UV_FILTER_PIN, LOW);
  digitalWrite(AIR_PIN, LOW);
  digitalWrite(LIGHTS_PIN, LOW);

  //Initialize SPIFFS
  //SPIFFS.begin(true) initializes SPIFFS and if it fails (!)
  if (!SPIFFS.begin(true)) {
    return;
  }
  
  //load persisted data.
  prefs.begin("whirlpool", false);
  pumpState = prefs.getBool("pumpState", false);
  heaterState = prefs.getBool("heaterState", false);
  uvFilterState = prefs.getBool("uvFilterState", false);
  airState = prefs.getBool("airState", false);
  heaterAutoState = prefs.getBool("heaterAutoState", false);
  heaterSetByAuto = prefs.getBool("heaterSetByAuto", false);
  pumpSetByAuto = prefs.getBool("pumpSetByAuto", false);
  lightsState = prefs.getBool("lightsState", false);
  setTemp = prefs.getFloat("setTemp", 35.0);
  tempTolerance = prefs.getFloat("tempTolerance", 0.5);
  
  //Serve static files
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/index.html", "text/html");
  });
  server.serveStatic("/", SPIFFS, "/");
  
  // Endpoint for Shelly energy data
  server.on("/energy", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["energy_current_w"] = energy_current_w; // From fetch
    doc["energy_total_kwh"] = energy_total_kwh;
    JsonArray byMinute = doc["by_minute_wh"].to<JsonArray>();
    doc["minute_ts"] = 1757692828; // From fetch
    char buffer[512];
    serializeJson(doc, buffer);
    request->send(200, "application/json", buffer);
  });

  //Start server (listening clients)
  server.begin();

  //End preferences. Good practice to free resources.
  prefs.end();
}

//--- --- --- loop() --- --- ---
void loop() {
  
  // Clean up WebSocket clients
  //it may happen that the connection is lost for various reasons, or closed intentionally.
  //AsyncWebSocket class provides an efficient cleaning method: cleanupClients()
  //Periodically calling the cleanClients() function from the main loop() limits the number of clients by closing the oldest client when the maximum number of clients has been exceeded.
  ws.cleanupClients();

  //ElegantOTA
  ElegantOTA.loop();

  //Reconnect WiFi if disconnected
  if (WiFi.status() != WL_CONNECTED && millis() - lastUpdate >= 20000) {
    WiFi.config(local_IP, gateway, subnet);
    WiFi.begin(ssid, password);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
      delay(500);
    }
    if (WiFi.status() == WL_CONNECTED) {
    }
  }

  // Read temperatures periodically, pool and outside temperature
  if (millis() - lastTempRead >= tempReadInterval) {
    sensors.requestTemperatures();
    poolTemp = sensors.getTempC(poolSensorAddress);
    if (poolTemp == DEVICE_DISCONNECTED_C) {
        poolTemp = -127.0; // Error value
        Serial.println("No pool temperature sensor.");
    }
    
    outsideTemp = sensors.getTempC(outsideSensorAddress);    
    if (outsideTemp == DEVICE_DISCONNECTED_C) {
        outsideTemp = -127.0; // Error value
        Serial.println("No outside temperature sensor.");
    }

    lastTempRead = millis();
    sendStatus();
  }

  //One wire search. Needed only for debugging, to find sensor addresses.
  /*
  static unsigned long lastsensoraddressread = 0;
  if(millis() - lastsensoraddressread >= 5000) {
    byte address[8];
    int sensorNum = 1;
    while (oneWire.search(address)) {
      Serial.print("Sensor ");
      Serial.print(sensorNum++);
      Serial.print(" Address: {0x");
      for (uint8_t i = 0; i < 8; i++) {
        if (address[i] < 16) Serial.print("0"); // Add leading zero for single-digit hex
        Serial.print(address[i], HEX);
        if (i < 7) Serial.print(", 0x");
      }
      Serial.println("}");
    }
    oneWire.reset_search();
    Serial.println("Scan complete.");
    lastsensoraddressread = millis();
  }
  */

  // Read pressure sensor
  static unsigned long lastPressureRead = 0;
  if (millis() - lastPressureRead >= 10000) { // Read every 10 second
    int rawValue = analogRead(PRESSURE_PIN);
    float voltage = (rawValue / 4095.0) * 3.3; // 12-bit ADC, 3.3V reference
    float pressure_PSI = (voltage / 3.3) * 5.0; // Linear: 0-3.3V -> 0-5 PSI
    currentPressure = pressure_PSI * 6.89476; // Convert to kPa
    sendStatus(); // Broadcast updated pressure
    lastPressureRead = millis();
  }

  //Fetch from Shelly Plug Plus PM. Fetch aenergy (total energy) from Shelly
  if (millis() - lastShellyFetch >= 60000) { // shellyFetchInternal --> 60000 (every 1 minute for by_minute (tulee shellystä))
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        String url = String("http://") + shellyIP + "/rpc/Switch.GetStatus?id=0";
        http.setTimeout(5000);
        int retries = 3;
        bool success = false;
        while (retries > 0 && !success) {
            http.begin(url);
            int httpCode = http.GET();
            if (httpCode == HTTP_CODE_OK) {
                String payload = http.getString();
                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, payload);
                if (!error && doc["aenergy"].is<JsonObject>() && doc["aenergy"]["total"].is<double>()) {
                    double currentTotalWh = doc["aenergy"]["total"].as<double>(); // Wh
                    energy_total_kwh = currentTotalWh / 1000.0;
                    success = true;
                }
                if (!error && doc["apower"].is<JsonObject>() && doc["apower"].is<double>()) {
                    double currentW = doc["apower"].as<double>(); // Wh
                    energy_current_w = currentW;
                    success = true;
                }
            }
            http.end();
            retries--;
            if (!success && retries > 0) {
                delay(1000);
            }
        }
    }
    lastShellyFetch = millis();
  }

  // Control logic, apply automation rules written in the applyControlLogic() function.
  applyControlLogic();

  //Lähetetään status vain, jos tila on muuttunut.
  static bool lastHeaterState = !heaterState;
  static bool lastPumpState = !pumpState;
  static bool lastheaterAutoState = !heaterAutoState;
  static bool lastUvFilterState = !uvFilterState;
  if (heaterState != lastHeaterState || pumpState != lastPumpState || heaterAutoState != lastheaterAutoState || uvFilterState != lastUvFilterState) {
  lastHeaterState = heaterState;
  lastPumpState = pumpState;
  lastheaterAutoState = heaterAutoState;
  lastUvFilterState = uvFilterState;
  sendStatus();  // Broadcast new state
  }

  // Save states periodically
  if (millis() - lastSave >= saveInterval) {
    savePrefs();
    lastSave = millis();
  }
}
