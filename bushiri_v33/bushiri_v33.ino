/**
 * PROJECT BUSHIRI v3.6 FIX FINAL
 * ESP32 3.3.8 + ArduinoJson v7 COMPATIBLE
 */

#include <WiFi.h>
#include <NetworkClientSecure.h>   // FIXED HERE
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
}

// ================= VPS VERIFY =================
bool verifyVPS(String txid, String ip, String &msg) {

  if (WiFi.status() != WL_CONNECTED) {
    msg = "No Internet";
    return false;
  }

  // FIX: NetworkClientSecure
  NetworkClientSecure client;

  client.setInsecure();

  if (!client.connect(VPS_HOST, VPS_PORT)) {
    msg = "VPS Error";
    return false;
  }

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
    msg = "Bad VPS response";
    return false;
  }

  bool ok = res["success"] | false;
  msg = res["message"] | "error";

  if (ok) addSession(ip, 14UL * 3600000UL);

  return ok;
}

// ================= WEB =================
void portal() {
  server.send(200, "text/html", "<h1>BUSHIRI WIFI</h1><a href='/pay'>PAY</a>");
}

void pay() {
  server.send(200, "text/html",
    "<form method='POST' action='/verify'>TXID:<input name='txid'><button>OK</button></form>");
}

void verify() {
  String msg;
  String txid = server.arg("txid");

  if (verifyVPS(txid, getIP(), msg)) {
    server.send(200, "text/html", "OK INTERNET");
  } else {
    server.send(200, "text/html", "FAILED: " + msg);
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID);

  dns.start(53, "*", IPAddress(192,168,4,1));

  server.on("/", portal);
  server.on("/pay", pay);
  server.on("/verify", HTTP_POST, verify);

  server.onNotFound([]() {
    server.sendHeader("Location", "/");
    server.send(302);
  });

  server.begin();
}

// ================= LOOP =================
void loop() {
  dns.processNextRequest();
  server.handleClient();
}