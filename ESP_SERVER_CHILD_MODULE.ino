// ### INCLUDE'S ### //
#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include <PubSubClient.h>  //broker mqtt

// ### DEFINES ### //

#define EEPROM_SIZE 256
#define FLAG_ADDR 132
#define CONFIG_FLAG 0xA5
#define LED_PIN 2  //1:off 0:on
#define AP_SSID "ESP01_01"
#define AP_PASS "ESP40637184"
#define MQTT_USER "backend"
#define MQTT_PASS "2207"
#define MQTT_TOPIC "ESP_COM"
#define PIN_COUNT 16

enum PinModeType {
  PIN_UNUSED = 0,
  PIN_INPUT,
  PIN_OUTPUT,
  PIN_ANALOG
};

struct PinState {
  int pin;
  PinModeType mode;
  int value;
};

// ### Setup values ### //
char ssid[32];      //For EEPROM
char pass[64];      //For EEPROM
char mqttIp[16];    //For EEPROM
char deviceId[18];  //For local usage

bool ledState = true;       //off
unsigned long ledLast = 0;  //sin iniciar
unsigned long ledInterval = 0;
unsigned long lastWifiTry = 0;
unsigned long lastWifiScan = 0;
unsigned long lastMqttTry = 0;
bool staEnabled = false;

PinState pins[PIN_COUNT] = {
  { A0, PIN_ANALOG, 0 },
  { 16, PIN_UNUSED, 0 },
  { 14, PIN_UNUSED, 0 },
  { 13, PIN_UNUSED, 0 },
  { 12, PIN_UNUSED, 0 },
  { 11, PIN_UNUSED, 0 },
  { 10, PIN_UNUSED, 0 },
  { 9, PIN_UNUSED, 0 },
  { 8, PIN_UNUSED, 0 },
  { 7, PIN_UNUSED, 0 },
  { 6, PIN_UNUSED, 0 },
  { 5, PIN_UNUSED, 0 },
  { 4, PIN_UNUSED, 0 },
  { 3, PIN_UNUSED, 0 },
  { 2, PIN_UNUSED, 0 },
  { 1, PIN_UNUSED, 0 },
};

WiFiClient espClient;
PubSubClient mqtt(espClient);
bool mqttReady = false;

ESP8266WebServer server(80);  //WebServer for AP configuration

// ### Global functions ### //

// ### MQTT
void initMqtt() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!validMqttIp()) return;

  mqtt.setServer(mqttIp, 1883);
  mqtt.setCallback(mqttCallback);

  Serial.print("MQTT -> ");
  Serial.println(mqttIp);

  if (mqtt.connect(deviceId, MQTT_USER, MQTT_PASS)) {
    mqttReady = true;
    Serial.println("\nMQTT connected.");
    mqtt.subscribe(MQTT_TOPIC);
  } else {
    Serial.println("MQTT connect failed.");
    Serial.print("MQTT error: ");
    Serial.println(mqtt.state());
  }
}

bool validMqttIp() {
  if (strlen(mqttIp) < 7 || strlen(mqttIp) > 15) return false;
  for (int i = 0; mqttIp[i]; i++) {
    char c = mqttIp[i];
    if ((c < '0' || c > '9') && c != '.') return false;
  }
  return true;
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  static char msg[256];

  if (length >= sizeof(msg)) length = sizeof(msg) - 1;
  memcpy(msg, payload, length);
  msg[length] = 0;
  onMqttMessage(msg);
}

void onMqttMessage(const char* json) {
  char id[18];
  char action[16];
  char pinStr[8];
  char valueStr[16];

  if (!jsonGet(json, "id", id, sizeof(id))) {
    Serial.println("Cannot get id");
    return;
  }
  if (strcmp(id, "report") == 0) {
    return;
  }
  if (strcmp(id, "device_report") == 0) {
    Serial.print("Reporting this device as: ");
    Serial.println(deviceId);
    char out[64];
    sprintf(out, "{\"id\":\"report\",\"device\":\"%s\"}", deviceId);
    mqttSend(out);
    return;
  }
  if (strcmp(id, deviceId) != 0)
    return;  //otro esp

  if (!jsonGet(json, "pin", pinStr, sizeof(pinStr))) {
    Serial.println("Cannot get pin");
    return;
  }
  if (!jsonGet(json, "action", action, sizeof(action))) {
    Serial.println("Cannot get action");
    return;
  }
  if (!jsonGet(json, "value", valueStr, sizeof(valueStr))) {
    Serial.println("Cannot get value");
    return;
  }

  int pin = (strcmp(pinStr, "A0") == 0) ? A0 : atoi(pinStr);

  PinState* p = getPin(pin);
  if (!p) return;

  /* ######## READ ########*/

  if (strcmp(action, "read") == 0) {
    int v;

    if (p->mode == PIN_ANALOG) {
      v = map(analogRead(A0), 0, 1023, 0, 1000);
      p->value = v;
    } else v = p->value;

    char out[96];
    sprintf(out, "{\"id\":\"%s\", \"action\":\"update\", \"pin\":\"%s\",\"value\":\"%d\"}", deviceId, pinStr, v);
    mqttSend(out);
  }

  /* ######## WRITE ########*/

  if (strcmp(action, "set") == 0) {
    if (!jsonGet(json, "value", valueStr, sizeof(valueStr))) return;

    if (strcmp(valueStr, "HIGH") == 0 || strcmp(valueStr, "1") == 0) {
      pinMode(pin, OUTPUT);
      digitalWrite(pin, HIGH);
      p->mode = PIN_OUTPUT;
      p->value = 1;
    } else {
      pinMode(pin, OUTPUT);
      digitalWrite(pin, LOW);
      p->mode = PIN_OUTPUT;
      p->value = 0;
    }

    char out[96];
    sprintf(out,
            "{\"id\":\"%s\",\"action\":\"update\",\"pin\":\"%s\",\"value\":\"%d\"}",
            deviceId, pinStr, p->value);
    mqttSend(out);
    return;
  }
}

void mqttSend(const char* msg) {
  if (!mqttReady) return;
  mqtt.publish(MQTT_TOPIC, msg);
}

bool jsonGet(const char* json, const char* key, char* out, int max) {
  char pattern[32];
  sprintf(pattern, "\"%s\":\"", key);

  char* p = strstr(json, pattern);
  if (!p) return false;

  p += strlen(pattern);
  int i = 0;
  while (*p && *p != '"' && i < max - 1) {
    out[i++] = *p++;
  }

  out[i] = 0;
  return true;
}

// ### Device
void loadDeviceId() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  sprintf(deviceId, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void handleLed(unsigned long int interval = ledInterval) {
  if (interval <= 0 && ledInterval <= 0) return;
  if (interval <= 0) interval = ledInterval;
  unsigned long now = millis();
  if ((now - ledLast) >= interval) {
    ledLast = now;
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState ? LOW : HIGH);
  }
}

void setLedBlink(int ms) {
  ledInterval = ms;
}

PinState* getPin(int p) {
  for (int i = 0; i < PIN_COUNT; i++) {
    if (pins[i].pin == p) return &pins[i];
  }
  return nullptr;
}

void setPinModeTracked(int pin, PinModeType mode) {
  PinState* ps = getPin(pin);
  ps->mode = mode;

  if (mode == PIN_OUTPUT) pinMode(pin, OUTPUT);
  if (mode == PIN_INPUT) pinMode(pin, INPUT);
}

void writePin(int pin, int value) {
  PinState* ps = getPin(pin);
  if (!ps || ps->mode != PIN_OUTPUT) return;
  digitalWrite(pin, value);
  ps->value = value;
}

int readPin(int pin) {
  PinState* ps = getPin(pin);
  if (!ps) return 0;
  if (pin == A0) {
    ps->value = map(analogRead(A0), 0, 1023, 0, 1000);  //send as milivolts
  } else if (ps->mode == PIN_INPUT) {
    ps->value = digitalRead(pin);
  }
  return ps->value;
}

// ### WI-FI
bool isNetworkAvailable() {
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == String(ssid)) return true;
  }
  return false;
}

bool validSSID() {
  int len = strlen(ssid);
  if (len <= 3 || len > 32) return false;

  for (int i = 0; i < len; i++)
    if (ssid[i] < 32 || ssid[i] > 126) return false;

  return true;
}

bool isConfigured() {
  return EEPROM.read(FLAG_ADDR) == CONFIG_FLAG;
}

void setupWiFi() {
  WiFi.mode(WIFI_AP_STA);
  Serial.println("Iniciando modo AP");
  Serial.print("SSID:");
  Serial.println(AP_SSID);
  Serial.print("PASS:");
  Serial.println(AP_PASS);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));

  WiFi.softAP(AP_SSID, AP_PASS, 1, true);
}

// ### EEPROM

void readEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, ssid);
  EEPROM.get(32, pass);
  EEPROM.get(96, mqttIp);
  Serial.println("=== LOADED FROM EEPROM ===");
  Serial.print("SSID: ");
  Serial.println(ssid);
  Serial.print("PASS: ");
  Serial.println(pass);
  Serial.print("MQTT IP: ");
  Serial.println(mqttIp);
  Serial.print("FLAG: ");
  Serial.println(EEPROM.read(FLAG_ADDR), HEX);
}

void dumpEEPROM() {
  char ssid[33];
  char pass[65];
  char mqtt[17];

  EEPROM.begin(256);

  EEPROM.get(0, ssid);
  EEPROM.get(32, pass);
  EEPROM.get(96, mqtt);

  ssid[32] = 0;
  pass[64] = 0;
  mqtt[16] = 0;

  Serial.println("=== EEPROM DUMP ===");
  Serial.print("SSID: ");
  Serial.println(ssid);
  Serial.print("PASS: ");
  Serial.println(pass);
  Serial.print("MQTT IP: ");
  Serial.println(mqtt);
  Serial.print("FLAG: ");
  Serial.println(EEPROM.read(132), HEX);
}
// WEB SERVER:
const char PAGE[] PROGMEM = R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
      <meta charset="UTF-8">
      <title>Configurar Wi-Fi</title>
      <style>
        body { font-family: sans-serif; background: #20232a; color: #61dafb; text-align:center; margin-top:40px; }
        input { margin:5px; padding:8px; border-radius:4px; border:1px solid #ccc; }
        button { padding:10px 20px; background:#61dafb; border:none; border-radius:4px; cursor:pointer; }
      </style>
    </head>
    <body>
      <h2>Configuración Wi-Fi</h2>
      <form action="/save" method="POST">
        <input type="text" name="ssid" placeholder="SSID" required><br>
        <input type="password" name="pass" placeholder="Contraseña" required><br>
        <button type="submit">Guardar</button>
      </form>
      <form action="/saveIP" method="POST">
        <input type="text" name="brokerIP" placeholder="IP Broker MQTT" required>
        <button type="submit">Guardar IP</button>
      </form>
      <p>ID: {{ID}}</p>
    </body>
    </html>
  )rawliteral";


void handleRoot() {
  String page = FPSTR(PAGE);
  page.replace("{{ID}}", deviceId);
  server.send(200, "text/html", page);
}

/*void connectSTA() {
  WiFi.begin(ssid, pass);
  unsigned long t0 = millis();
  bool connected = false;
  digitalWrite(LED_PIN, 0);
  Serial.print("Connecting to SSID: ");
  Serial.println(ssid);
  Serial.print("With password: ");
  Serial.println(pass);
  Serial.print("...");

  while (millis() - t0 < 15000 && !connected) {
    if (WiFi.status() != WL_CONNECTED) {
      delay(200);
      Serial.print(".");
    } else {
      connected = true;
      Serial.println("\nConectado");
      Serial.print("Device MAC: ");
      Serial.println(deviceId);
      setLedBlink(0);
      digitalWrite(LED_PIN, 1);  //led off
    }
  }
  if (!connected) {
    Serial.println("\nSTA failed, staying in AP mode");
    setLedBlink(0);
    digitalWrite(LED_PIN, 0);
  }
  initMqtt();
}*/

void startSTA() {
  WiFi.begin(ssid, pass);
  staEnabled = true;
  lastWifiTry = millis();
  Serial.println("STA attempt started");
}

void saveConfig() {
  memset(ssid, 0, sizeof(ssid));
  memset(pass, 0, sizeof(pass));

  server.arg("ssid").toCharArray(ssid, sizeof(ssid));
  server.arg("pass").toCharArray(pass, sizeof(pass));

  EEPROM.put(0, ssid);
  EEPROM.put(32, pass);
  EEPROM.write(FLAG_ADDR, CONFIG_FLAG);
  EEPROM.commit();

  server.send(200, "text/plain", "OK");
  digitalWrite(2, 0);
  delay(500);
  ESP.restart();
}

void saveMqttIp() {
  memset(mqttIp, 0, sizeof(mqttIp));
  server.arg("brokerIP").toCharArray(mqttIp, sizeof(mqttIp));

  EEPROM.put(96, mqttIp);
  EEPROM.commit();

  server.send(200, "text/plain", "OK");
}
// ### Running code ### //

void setup() {
  Serial.begin(115200);
  Serial.println("\nESP STARTED");
  EEPROM.begin(EEPROM_SIZE);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(2, HIGH);

  setupWiFi();
  loadDeviceId();
  readEEPROM();
  dumpEEPROM();

  if (isConfigured() && validSSID()) {
    setLedBlink(333);
    startSTA();
  } else {
    Serial.println("\nSSID invalido. STA bloqueado.");
    setLedBlink(0);
    digitalWrite(LED_PIN, LOW);
  }

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, saveConfig);
  server.on("/saveIP", HTTP_POST, saveMqttIp);
  server.begin();
}

void loop() {
  server.handleClient();
  if (isConfigured() && validSSID()) {
    if (WiFi.status() != WL_CONNECTED) {
      handleLed(1500);
      if (millis() - lastWifiScan > 7500) {
        lastWifiScan = millis();
        if (isNetworkAvailable()) {
          Serial.println("Target ssid found. Retrying STA...");
          startSTA();
        } else {
          Serial.println("Target SSID not found");
        }
      }
    } else {
      if (!mqtt.connected()) {
        initMqtt();
        handleLed(3000);
        if (millis() - lastMqttTry > 5000) {
          lastMqttTry = millis();
          if (mqtt.connect(deviceId, MQTT_USER, MQTT_PASS)) {
            mqtt.subscribe(MQTT_TOPIC);
            mqttReady = true;
            Serial.println("MQTT connected");
          }
        }
      } else {
        if (digitalRead(2) == LOW) digitalWrite(2, HIGH);
        mqtt.loop();
      }
    }
  }
}
