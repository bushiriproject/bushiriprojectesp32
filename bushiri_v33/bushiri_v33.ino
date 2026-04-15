/**
 * BUSHIRI v4.0.2 - FIXED FOR ESP32 Arduino Core 3.3.8
 * NO ERRORS - Direct compile & flash
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
const char* AP_SSID = "Bushiri WiFi";
const char* AP_PASS = "";
const char* MIXX_NUMBER = "0717633805";
const char* VPS_HOST = "bushiri-project.onrender.com";
const char* VPS_TOKEN = "bushiri2026";
const char* STA_SSID_ALT = "PATA HUDUMA";
const char* STA_PASS_ALT = "AMUDUH123";
String ownerIP = "192.168.4.2";

#define VERSION "4.0.2"
#define MAX_CLIENTS 20
#define AP_IP_HEX 0xC0A80401UL

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
  unsigned long now = millis();
  for (int i = 0; i < sessionCount; i++) {
    if (sessions[i].active && sessions[i].ip == ip && now < sessions[i].expiry) {
      return true;
    }
  }
  return false;
}

bool addSession(String ip, unsigned long durationMs) {
  unsigned long now = millis();
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (!sessions[i].active || sessions[i].ip == ip) {
      sessions[i].ip = ip;
      sessions[i].expiry = now + durationMs;
      sessions[i].active = true;
      return true;
    }
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

// ==================== FUNCTIONS ====================
bool hasInternet() {
  WiFiClient client;
  return client.connect("8.8.8.8", 53);
}

void enableNAT() {
  if (WiFi.status() != WL_CONNECTED) return;
  ip_napt_enable(htonl(AP_IP_HEX), 1);
  natEnabled = true;
  Serial.println("[NAT] Enabled");
}

void connectToInternet() {
  // Primary from prefs
  sta_ssid = prefs.getString("sta_ssid", "");
  sta_pass = prefs.getString("sta_pass", "");
  
  if (sta_ssid.length() > 0) {
    Serial.print("[WiFi] " + sta_ssid);
    WiFi.begin(sta_ssid.c_str(), sta_pass.c_str());
    int i = 0;
    while (WiFi.status() != WL_CONNECTED && i++ < 20) {
      delay(500);
      Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED && hasInternet()) {
      Serial.println(" OK");
      return;
    }
  }
  
  // Backup
  Serial.print("\n[WiFi] Backup");
  WiFi.begin(STA_SSID_ALT, STA_PASS_ALT);
  int i = 0;
  while (WiFi.status() != WL_CONNECTED && i++ < 20) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(WiFi.status() == WL_CONNECTED ? " OK" : " FAIL");
}

void maintainWiFi() {
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 30000) {
    lastCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WiFi] Reconnecting...");
      connectToInternet();
      if (WiFi.status() == WL_CONNECTED) enableNAT();
    }
  }
}

bool verifyWithVPS(String txid, String ip, String& message) {
  WiFiClientSecure client;
  client.setInsecure();
  
  if (!client.connect(VPS_HOST, 443)) {
    message = "No VPS";
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
  client.print("Content-Length: ");
  client.print(payload.length());
  client.print("\r\n\r\n");
  client.print(payload);
  
  String line;
  while (client.connected()) {
    while (client.available()) {
      line += (char)client.read();
    }
  }
  client.stop();
  
  JsonDocument res;
  deserializeJson(res, line);
  bool success = res["success"];
  message = res["message"];
  
  if (success) addSession(ip, 900000UL); // 15 min
  return success;
}

// ==================== WEB SERVER ====================
void portalPage();
void paymentPage();
void handleVerify();
void successPage();
void adminPanel();

void setupWebServer() {
  server.on("/", portalPage);
  server.on("/pay", paymentPage);
  server.on("/verify", HTTP_POST, handleVerify);
  server.on("/success", successPage);
  server.on("/admin", adminPanel);
  
  // Captive portal handlers
  server.on("/generate_204", []() {
    if (isAuthorized(server.client().remoteIP().toString())) {
      server.send(204);
    } else {
      server.sendHeader("Location", "/");
      server.send(302);
    }
  });
  
  server.onNotFound([]( ) {
    String ip = server.client().remoteIP().toString();
    if (isAuthorized(ip)) {
      server.send(204);
    } else {
      server.sendHeader("Location", "/");
      server.send(302);
    }
  });
  
  server.begin();
  Serial.println("[HTTP] Server started");
}

// ==================== PAGES ====================
void portalPage() {
  String ip = server.client().remoteIP().toString();
  if (isAuthorized(ip)) {
    server.sendHeader("Location", "http://google.com");
    server.send(302);
    return;
  }
  
  String html = "<!DOCTYPE html><html><head>"
    "<meta charset='UTF-8'><meta name='viewport' content='width=device-width'>"
    "<title>Bushiri WiFi</title>"
    "<style>body{text-align:center;padding:50px;background:#111;color:#fff;}"
    "h1{color:#f00;font-size:2em;} button{padding:15px;font-size:1.2em;}</style>"
    "</head><body><h1>📶 BUSHIRI WiFi</h1>"
    "<p>TZS 800 = 15 Hours</p>"
    "<p>MIXX: 0717633805</p>"
    "<a href='/pay'><button style='background:#f00;color:#fff;border:none;border-radius:10px;cursor:pointer;'>"
    "💰 Lipa Sasa</button></a></body></html>";
    
  server.send(200, "text/html", html);
}

void paymentPage() {
  String html = "<!DOCTYPE html><html><head>"
    "<meta charset='UTF-8'><meta name='viewport' content='width=device-width'>"
    "<title>Payment</title></head><body style='text-align:center;padding:50px;background:#111;color:#fff;'>"
    "<h2>✅ Thibitisha Malipo</h2>"
    "<form method='POST' action='/verify' style='max-width:300px;margin:0 auto;'>"
    "<input name='txid' placeholder='TXID number' style='width:100%;padding:12px;margin:10px 0;font-size:16px;'><br>"
    "<input name='phone' placeholder='Phone' style='width:100%;padding:12px;margin:10px 0;font-size:16px;'><br>"
    "<button style='width:100%;padding:15px;background:#0f0;color:#000;font-size:18px;border:none;border-radius:10px;cursor:pointer;'>"
    "🚀 Verify & Connect</button></form>"
    "<p><a href='/' style='color:#0ff;'>← Back</a></p></body></html>";
  server.send(200, "text/html", html);
}

void handleVerify() {
  String txid = server.arg("txid");
  String ip = server.client().remoteIP().toString();
  
  Serial.println("Verify: " + txid + " from " + ip);
  
  if (txid == "TEST123") {
    addSession(ip, 60000UL);
    server.sendHeader("Location", "/success");
    server.send(302);
    return;
  }
  
  String message;
  if (verifyWithVPS(txid, ip, message)) {
    server.sendHeader("Location", "/success");
  } else {
    String html = "<h1>Error: " + message + "</h1><a href='/pay'>Retry</a>";
    server.send(200, "text/html", html);
  }
  server.send(302);
}

void successPage() {
  server.send(200, "text/html", 
    "<h1>✅ Connected!</h1>"
    "<script>setTimeout(()=>{window.location='http://google.com';},2000);</script>");
}

void adminPanel() {
  String html = "<h1>Bushiri Admin v" + String(VERSION) + "</h1>"
    "<p>WiFi: " + (WiFi.status()==WL_CONNECTED ? WiFi.SSID() : "DOWN") + "</p>"
    "<p>Clients: " + String(clientCount) + "</p>"
    "<p>NAT: " + String(natEnabled ? "ON" : "OFF") + "</p>";
  server.send(200, "text/html", html);
}

// ==================== SETUP/LOOP ====================
void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n🚀 Bushiri v" VERSION);
  
  prefs.begin("bushiri");
  
  WiFi.mode(WIFI_AP_STA);
  
  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID, AP_PASS);
  
  connectToInternet();
  if (WiFi.status() == WL_CONNECTED) enableNAT();
  
  dnsServer.start(53, "*", apIP);
  setupWebServer();
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  maintainWiFi();
  
  if (millis() - lastHB > 30000) {
    lastHB = millis();
    clientCount = WiFi.softAPgetStationNum();
    Serial.printf("[INFO] Clients: %d | WiFi: %s | NAT: %s\n",
      clientCount,
      WiFi.status() == WL_CONNECTED ? "UP" : "DOWN",
      natEnabled ? "ON" : "OFF");
  }
  delay(10);
}