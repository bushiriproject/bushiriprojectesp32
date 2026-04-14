/**
 * PROJECT BUSHIRI v3.6 (STABLE FIXED)
 * Captive Portal + NAT + VPS Verification
 * FIXED: ArduinoJson v7 + linker + NAT stability
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Update.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "lwip/lwip_napt.h"
#include "lwip/ip_addr.h"

// ================= CONFIG =================
const char* AP_SSID = "Bushiri WiFi";
const char* AP_PASS = "";
const char* VPS_HOST = "bushiri-project.onrender.com";
const int VPS_PORT = 443;
const char* VPS_TOKEN = "bushiri2026";

String ownerIP = "192.168.4.2";

#define AP_IP_HEX 0xC0A80401UL
#define MAX_CLIENTS 20

// ================= SESSION =================
struct Session {
  String ip;
  unsigned long expiry;
  bool active;
};

Session sessions[MAX_CLIENTS];
int sessionCount = 0;

// ================= GLOBALS =================
WebServer server(80);
DNSServer dns;
Preferences prefs;

unsigned long lastCheck = 0;
bool natOn = false;

// ================= UTIL =================
String getIP() {
  return server.client().remoteIP().toString();
}

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

void addSession(String ip, unsigned long ms) {
  for (int i = 0; i < sessionCount; i++) {
    if (sessions[i].ip == ip) {
      sessions[i].expiry = millis() + ms;
      sessions[i].active = true;
      return;
    }
  }

  if (sessionCount < MAX_CLIENTS) {
    sessions[sessionCount++] = {ip, millis() + ms, true};
  }
}

// ================= NAT =================
void enableNAT() {
  ip_napt_enable(htonl(AP_IP_HEX), 1);
  natOn = true;
  Serial.println("[NAT] ON");
}

// ================= WIFI RECOVERY =================
void checkWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] reconnect...");
    WiFi.reconnect();
    delay(2000);

    if (WiFi.status() == WL_CONNECTED) {
      enableNAT();
    }
  }
}

// ================= VPS VERIFY =================
bool verifyVPS(String txid, String ip, String &msg) {

  if (WiFi.status() != WL_CONNECTED) {
    msg = "No Internet";
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();

  if (!client.connect(VPS_HOST, VPS_PORT)) {
    msg = "VPS Error";
    return false;
  }

  // FIX ArduinoJson v7
  JsonDocument doc;
  doc["txid"] = txid;
  doc["ip"] = ip;
  doc["token"] = VPS_TOKEN;

  String payload;
  serializeJson(doc, payload);

  client.println("POST /verify HTTP/1.1");
  client.println("Host: " + String(VPS_HOST));
  client.println("Content-Type: application/json");
  client.println("Content-Length: " + String(payload.length()));
  client.println("Connection: close");
  client.println();
  client.print(payload);

  String response;
  unsigned long timeout = millis() + 10000;

  while (client.connected() && millis() < timeout) {
    response += client.readString();
  }

  JsonDocument res;
  DeserializationError err = deserializeJson(res, response);

  if (err) {
    msg = "Invalid VPS response";
    return false;
  }

  bool ok = res["success"] | false;
  msg = res["message"] | "error";

  if (ok) addSession(ip, 14UL * 3600000UL);

  return ok;
}

// ================= PORTAL =================
void portal() {
  String ip = getIP();

  if (isAuthorized(ip)) {
    server.sendHeader("Location", "http://google.com");
    server.send(302);
    return;
  }

  server.send(200, "text/html",
    "<h1>BUSHIRI WIFI</h1>"
    "<a href='/pay'>Lipa</a>");
}

// ================= PAYMENT =================
void pay() {
  server.send(200, "text/html",
    "<form method='POST' action='/verify'>"
    "TXID:<input name='txid'>"
    "<button>OK</button></form>");
}

// ================= VERIFY =================
void verify() {
  String txid = server.arg("txid");
  String msg;

  if (verifyVPS(txid, getIP(), msg)) {
    server.send(200, "text/html", "OK INTERNET");
  } else {
    server.send(200, "text/html", "FAILED: " + msg);
  }
}

// ================= ADMIN =================
void admin() {
  server.send(200, "text/html",
    "<h2>ADMIN</h2>"
    "<p>Clients: " + String(WiFi.softAPgetStationNum()) + "</p>"
    "<p>NAT: " + String(natOn ? "ON" : "OFF") + "</p>");
}

// ================= SETUP WEB =================
void setupWeb() {

  server.on("/", portal);
  server.on("/pay", pay);
  server.on("/verify", HTTP_POST, verify);
  server.on("/admin", admin);

  server.onNotFound([]() {
    server.sendHeader("Location", "/");
    server.send(302);
  });

  server.begin();
  Serial.println("[WEB] Ready");
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_AP_STA);

  IPAddress apIP(192,168,4,1);
  WiFi.softAP(AP_SSID);

  dns.start(53, "*", apIP);

  setupWeb();

  Serial.println("READY");

  if (WiFi.status() == WL_CONNECTED) {
    enableNAT();
  }
}

// ================= LOOP =================
void loop() {
  dns.processNextRequest();
  server.handleClient();

  if (millis() - lastCheck > 10000) {
    lastCheck = millis();
    checkWiFi();
  }
}