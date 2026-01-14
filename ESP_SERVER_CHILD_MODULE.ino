// ### INCLUDE'S ### //
#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>

// ### DEFINES ### //

#define EEPROM_SIZE 256
#define FLAG_ADDR 132
#define CONFIG_FLAG 0xA5
#define LED_PIN 2  //1:off 0:on

// ### Setup values ### //
char ssid[32];  //For EEPROM
char pass[64];  //For EEPROM
char uuid[36];  //For EEPROM

ESP8266WebServer server(80);  //WebServer for AP configuration

// ### Global functions ### //

void readEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, ssid);
  EEPROM.get(32, pass);
  EEPROM.get(96, uuid);
}

bool isConfigured() {
  return EEPROM.read(FLAG_ADDR) == CONFIG_FLAG;
}

void setupWifi() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));

  WiFi.softAP("ESP_SETUP_1", "ESP40637184", 1, true);
}

void connectSTA() {
  WiFi.begin(ssid, pass);
  unsigned long t0 = millis();
  bool connected = false;
  digitalWrite(LED_PIN, 0);

  while (millis() - t0 < 15000 && !connected) {
    Serial.print("Connecting to SSID: ");
    Serial.println(ssid);
    Serial.print("With password: ");
    Serial.println(pass);
    Serial.print("...");
    if (WiFi.status() != WL_CONNECTED) {
      Serial.print(".");
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      delay(333);
    } else {
      connected = true;
      digitalWrite(LED_PIN, 1);  //led off
    }
  }
  if (!connected) {
    Serial.println("Couldn connect. Resetting...");
    ESP.reset();
  }
}
// ### Running code ### //

void setup() {
  Serial.begin(115200);
  Serial.println("ESP STARTED");
}

void loop() {
  // put your main code here, to run repeatedly:
}
