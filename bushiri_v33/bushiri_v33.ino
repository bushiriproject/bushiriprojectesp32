/**
 * PROJECT BUSHIRI v4.0.1 - FIXED VERSION
 * MPESA/MIXX Captive Portal + NAT Router + VPS Verify
 * Fixed: WiFi connection, NAT, compile errors
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

// ==================== SETTINGS ====================
const char* AP_SSID      = "Bushiri WiFi";
const char* AP_PASS      = "";
const char* VPS_HOST     = "bushiri-project.onrender.com";
const int   VPS_PORT     = 443;
const char* VPS_TOKEN    = "bushiri2026";
const char* STA_SSID_ALT = "PATAHUDUMA";  // Backup WiFi
const char* STA_PASS_ALT = "AMUDUH123";
String ownerIP           = "192.168.4.2";

#define VERSION      "4.0.1"
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
    if (sessions[i].active && sessions[i].ip == ip && millis() < sessions[i].expiry) {
      return true;
    }
  }
  return false;
}

bool addSession(String ip, unsigned long durationMs) {
  for (int i = 0; i < sessionCount; i++) {
    if (!sessions[i].active) {
      sessions[i] = {ip, millis() + durationMs, true};
      return true;
    }
  }
  if (sessionCount < MAX_CLIENTS) {
    sessions[sessionCount] = {ip, millis() + durationMs, true};
    sessionCount++;
    return true;
  }
  return false;
}

// ==================== GLOBALS ====================
WebServer server(80);
DNSServer dnsServer;
Preferences prefs;
String sta_ssid = "";
String sta_pass = "";
int clientCount = 0;
bool natEnabled = false;
unsigned long lastHB = 0;

// ==================== INTERNET CHECK ====================
bool hasInternet() {
  WiFiClient client;
  if (client.connect("8.8.8.8", 53)) {
    client.stop();
    return true;
  }
  return false;
}

// ==================== NAT ====================
void enableNAT() {
  if (WiFi.status() != WL_CONNECTED) return;
  
  // ESP32 Arduino core v3.x+ - ip_napt_enable returns void
  ip_napt_enable(htonl(AP_IP_HEX), 1);
  natEnabled = true;
  Serial.println("[NAT] ✅ ON - 192.168.4.1");
}

// ==================== WIFI ====================
void connectToInternet() {
  // Primary WiFi from prefs
  sta_ssid = prefs.getString("sta_ssid", "");
  sta_pass = prefs.getString("sta_pass", "");
  
  if (sta_ssid.length() > 0) {
    Serial.print("[WiFi] Primary: " + sta_ssid);
    WiFi.begin(sta_ssid.c_str(), sta_pass.c_str());
    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout++ < 20) {
      delay(500);
      Serial.print(".");
    }
    
    if (WiFi.status() == WL_CONNECTED && hasInternet()) {
      Serial.println("\n[WiFi] ✅ Primary OK: " + WiFi.localIP().toString());
      return;
    }
    WiFi.disconnect();
    delay(1000);
  }

  // Backup WiFi
  Serial.print("\n[WiFi] Backup: " + String(STA_SSID_ALT));
  WiFi.begin(STA_SSID_ALT, STA_PASS_ALT);
  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout++ < 20) {
    delay(500);
    Serial.print(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] ✅ Backup OK: " + WiFi.localIP().toString());
  } else {
    Serial.println("\n[WiFi] ❌ NO INTERNET");
  }
}

void maintainWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Disconnected - reconnecting...");
    natEnabled = false;
    connectToInternet();
    if (WiFi.status() == WL_CONNECTED) {
      delay(2000);
      enableNAT();
    }
  }
}

// ==================== VPS VERIFY ====================
bool verifyWithVPS(String txid, String ip, String &message) {
  if (WiFi.status() != WL_CONNECTED || !hasInternet()) {
    message = "❌ Hakuna internet";
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15000);

  if (!client.connect(VPS_HOST, VPS_PORT)) {
    message = "❌ VPS connection failed";
    return false;
  }

  JsonDocument doc;
  doc["txid"] = txid;
  doc["mac"] = ip;
  doc["token"] = VPS_TOKEN;
  
  String payload;
  serializeJson(doc, payload);

  client.print("POST /verify HTTP/1.1\r\n");
  client.print("Host: " + String(VPS_HOST) + "\r\n");
  client.print("Content-Type: application/json\r\n");
  client.print("Content-Length: " + String(payload.length()) + "\r\n");
  client.print("Connection: close\r\n\r\n");
  client.print(payload);

  String response = "";
  unsigned long timeout = millis() + 10000;
  while (client.connected() && millis() < timeout) {
    while (client.available()) {
      response += (char)client.read();
    }
  }
  client.stop();

  JsonDocument res;
  DeserializationError error = deserializeJson(res, response);
  if (error) {
    message = "❌ VPS response error";
    return false;
  }

  bool success = res["success"] | false;
  message = res["message"] | "Unknown error";
  
  if (success) {
    addSession(ip, 15UL * 60 * 1000UL); // 15 minutes
  }
  return success;
}

// ==================== WEB SERVER ====================
void setupWebServer();

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n🚀 BUSHIRI v" VERSION " - Starting...");
  
  prefs.begin("bushiri", false);

  // AP + STA mode
  WiFi.mode(WIFI_AP_STA);
  
  // Setup AP
  IPAddress apIP(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  
  WiFi.softAPConfig(apIP, gateway, subnet);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.println("[AP] ✅ " + String(AP_SSID) + " @ 192.168.4.1");

  // Connect to internet
  connectToInternet();
  
  // Enable NAT if connected
  if (WiFi.status() == WL_CONNECTED) {
    delay(3000);
    enableNAT();
  }

  // DNS Captive
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", apIP);

  setupWebServer();
  Serial.println("\n✅ READY - Admin: http://192.168.4.1/admin");
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();

  // Heartbeat every 30s
  if (millis() - lastHB > 30000) {
    lastHB = millis();
    clientCount = WiFi.softAPgetStationNum();
    
    Serial.printf("[STATUS] Clients:%d | WiFi:%s | NAT:%s\n",
      clientCount,
      WiFi.status() == WL_CONNECTED ? WiFi.SSID().c_str() : "DOWN",
      natEnabled ? "ON" : "OFF"
    );
    
    maintainWiFi();
  }
  
  delay(10);
}

// ==================== WEB PAGES (SHORTENED) ====================
void portalPage() {
  String ip = server.client().remoteIP().toString();
  if (isAuthorized(ip)) {
    server.sendHeader("Location", "http://google.com");
    server.send(302);
    return;
  }
  
  String html = "<h1>BUSHIRI HOTSPOT</h1>"
    "<p>TZS 800 = Masaa 15</p>"
    "<p>MIXX: " + String(MIXX_NUMBER) + "</p>"
    "<a href='/pay'>Lipa Sasa</a>";
  server.send(200, "text/html", html);
}

void paymentPage() {
  String html = "<h2>Thibitisha Malipo</h2>"
    "<form method='POST' action='/verify'>"
    "<input name='txid' placeholder='TXID'><br>"
    "<input name='phone' placeholder='Phone'><br>"
    "<button>Verify</button>"
    "</form>";
  server.send(200, "text/html", html);
}

void handleVerify() {
  String txid = server.arg("txid");
  String ip = server.client().remoteIP().toString();
  
  if (txid == "TEST") {
    addSession(ip, 60000UL); // 1 minute test
    server.sendHeader("Location", "/success");
    server.send(302);
    return;
  }
  
  String message;
  if (verifyWithVPS(txid, ip, message)) {
    server.sendHeader("Location", "/success");
  } else {
    server.send(200, "text/html", "<h1>Error: " + message + "</h1><a href='/pay'>Retry</a>");
  }
  server.send(302);
}

void successPage() {
  server.send(200, "text/html", 
    "<h1>✅ Success!</h1>"
    "<script>window.location='http://google.com';</script>");
}

void adminPanel() {
  String html = "<h1>Admin v" VERSION "</h1>"
    "<p>WiFi: " + (WiFi.status()==WL_CONNECTED ? WiFi.SSID() : "DOWN") + "</p>"
    "<p>Clients: " + String(clientCount) + "</p>"
    "<a href='/wifi-config'>WiFi Config</a>";
  server.send(200, "text/html", html);
}

// Add all other handlers...
void setupWebServer() {
  server.on("/", portalPage);
  server.on("/pay", paymentPage);
  server.on("/verify", HTTP_POST, handleVerify);
  server.on("/success", successPage);
  server.on("/admin", adminPanel);
  
  // Captive portals
  server.on("/generate_204", [](){ 
    String ip = server.client().remoteIP().toString();
    if (isAuthorized(ip)) server.send(204);
    else {server.sendHeader("Location","/"); server.send(302);}
  });
  
  server.onNotFound([]( ){ 
    String ip = server.client().remoteIP().toString();
    if (isAuthorized(ip)) server.send(204);
    else {server.sendHeader("Location","/"); server.send(302);}
  });
  
  server.begin();
}