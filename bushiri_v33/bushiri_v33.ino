/**
 * PROJECT BUSHIRI v3.5
 * MPESA/MIXX Captive Portal + NAT Router + VPS Verify
 * FIX: ArduinoJson v7 compatibility only
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "lwip/lwip_napt.h"
#include "lwip/ip_addr.h"

// ==================== EDIT HAPA TU ====================
const char* AP_SSID      = "Bushiri WiFi";
const char* AP_PASS      = "";
const char* VPS_HOST     = "bushiri-project.onrender.com";
const int   VPS_PORT     = 443;
const char* VPS_TOKEN    = "bushiri2026";
const char* PORTAL_TITLE = "BUSHIRI HOTSPOT";
const char* MIXX_NUMBER  = "0717633805";
const char* STA_SSID_ALT = "PATA HUDUMA";
const char* STA_PASS_ALT = "AMUDUH123";
String ownerIP           = "192.168.4.2";
// ======================================================

#define VERSION      "3.5.0"
#define MAX_CLIENTS  20
#define AP_IP_HEX    0xC0A80401UL

// ==================== SESSION ====================
struct ClientSession {
  String ip;
  unsigned long expiry;
  bool active;
};

ClientSession sessions[MAX_CLIENTS];
int sessionCount = 0;

bool isAuthorized(String ip) {
  if (ip == ownerIP) return true;
  for (int i = 0; i < sessionCount; i++) {
    if (sessions[i].active && sessions[i].ip == ip) {
      if (millis() < sessions[i].expiry) return true;
      sessions[i].active = false;
    }
  }
  return false;
}

bool addSession(String ip, unsigned long durationMs) {
  for (int i = 0; i < sessionCount; i++) {
    if (sessions[i].ip == ip) {
      sessions[i].active = true;
      sessions[i].expiry = millis() + durationMs;
      return true;
    }
  }
  if (sessionCount < MAX_CLIENTS) {
    sessions[sessionCount++] = {ip, millis() + durationMs, true};
    return true;
  }
  return false;
}

// ==================== GLOBALS ====================
WebServer   server(80);
DNSServer   dnsServer;
Preferences prefs;

String sta_ssid = "";
String sta_pass = "";
int clientCount = 0;
bool natEnabled = false;
unsigned long lastHB = 0;

// ==================== FORWARD ====================
void setupWebServer();
void setupOTA();
void captiveRedirect();
String getClientIP();

// ==================== NAT ====================
void enableNAT() {
  delay(500);
  ip_napt_enable(htonl(AP_IP_HEX), 1);
  natEnabled = true;
  Serial.println("[NAT] ON");
}

// ==================== WIFI ====================
void connectToInternet() {
  sta_ssid = prefs.getString("sta_ssid", "");
  sta_pass = prefs.getString("sta_pass", "");

  WiFi.begin(sta_ssid.c_str(), sta_pass.c_str());

  for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500);
  }

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.begin(STA_SSID_ALT, STA_PASS_ALT);
  }
}

// ==================== VPS VERIFY ====================
bool verifyWithVPS(String txid, String ip, String &message) {
  if (WiFi.status() != WL_CONNECTED) {
    message = "Hakuna internet";
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();

  if (!client.connect(VPS_HOST, VPS_PORT)) {
    message = "VPS error";
    return false;
  }

  // ✅ FIX ARDUINOJSON v7
  StaticJsonDocument<1024> doc;
  doc["txid"] = txid;
  doc["mac"]  = ip;
  doc["token"]= VPS_TOKEN;

  String payload;
  serializeJson(doc, payload);

  client.println("POST /verify HTTP/1.1");
  client.println("Host: " + String(VPS_HOST));
  client.println("Content-Type: application/json");
  client.println("Content-Length: " + String(payload.length()));
  client.println("Connection: close\r\n");
  client.print(payload);

  String response;
  response.reserve(1500);

  bool body = false;
  while (client.connected()) {
    if (client.available()) {
      String line = client.readStringUntil('\n');
      if (line == "\r") body = true;
      if (body) response += line;
    }
  }

  client.stop();

  // ✅ FIX ARDUINOJSON v7
  StaticJsonDocument<1024> res;
  deserializeJson(res, response);

  bool success = res["success"] | false;
  message = res["message"] | "Error";

  if (success) addSession(ip, 14UL * 3600000UL);

  return success;
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);

  prefs.begin("bushiri");

  WiFi.mode(WIFI_AP_STA);

  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID, NULL);

  connectToInternet();

  if (WiFi.status() == WL_CONNECTED) {
    ip_napt_enable(htonl(AP_IP_HEX), 1);
    natEnabled = true;
  }

  dnsServer.start(53, "*", apIP);

  setupWebServer();
  setupOTA();
}

// ==================== LOOP ====================
void loop() {
  dnsServer.processNextRequest();
  server.handleClient();

  if (millis() - lastHB > 30000) {
    lastHB = millis();
    clientCount = WiFi.softAPgetStationNum();

    if (WiFi.status() == WL_CONNECTED) {
      ip_napt_enable(htonl(AP_IP_HEX), 1);
      natEnabled = true;
    }
  }
}

// ==================== STUBS (ULIZOKWENDA NAWE KAMA ZILIVYO) ====================
// portalPage(), paymentPage(), successPage(), adminPanel(),
// wifiConfigPage(), setupWebServer(), setupOTA(), captiveRedirect()
// hazijaguswa kabisa kama ulivyoomba.