#include <WiFi.h>
#include "lwip/napt.h"

const char* STA_SSID = "PATAHUDUMA";
const char* STA_PASS = "HUDUMA";
const char* AP_SSID  = "Bushiri WiFi";
const char* AP_PASS  = "";

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(STA_SSID, STA_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Connected to modem");

  WiFi.softAP(AP_SSID, AP_PASS);
  delay(1000);

  ip_napt_enable(WiFi.softAPIP(), 1); // NAT enabled
}

void loop() {}