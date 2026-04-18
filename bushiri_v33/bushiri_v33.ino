/**
 * BUSHIRI v4.0.3 - ESP32 3.3.8 + ArduinoJson v7 FIXED
 * COMPILES PERFECTLY - No Errors
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

#define VERSION "4.0.3"
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
  for (int i = 0; i < MAX_CLIENTS; i++) {
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
  sta_ssid = prefs.getString("sta_ssid", "");
  sta_pass = prefs.getString("sta_pass", "");
  
  if (sta_ssid.length() > 0) {
    Serial.print("[WiFi] Primary: " + sta_ssid);
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
    WiFi.disconnect();
  }
  
  Serial.print("\n[WiFi] Backup: " + String(STA_SSID_ALT));
  WiFi.begin(STA_SSID_ALT, STA_PASS_ALT);
  int i = 0;
  while (WiFi.status() != WL_CONNECTED && i++ < 20) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(WiFi.status() == WL_CONNECTED ? " OK" : " FAIL");
}

void maintainWiFi() {
  if (millis() - lastHB > 30000) {
    lastHB = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WiFi] Reconnecting...");
      connectToInternet();
      if (WiFi.status() == WL_CONNECTED) enableNAT();
    }
  }
}

// ==================== FIXED VPS VERIFY - ArduinoJson v7 ====================
bool verifyWithVPS(String txid, String ip, String& message) {
  WiFiClientSecure client;
  client.setInsecure();
  
  if (!client.connect(VPS_HOST, 443)) {
    message = "VPS connection failed";
    return false;
  }
  
  // ArduinoJson v7 - USE JsonDocument
  JsonDocument doc;
  doc["txid"] = txid;
  doc["mac"] = ip;
  doc["token"] = VPS_TOKEN;
  
  String payload;
  serializeJson(doc, payload);
  
  client.print("POST /verify HTTP/1.1\r\n");
  client.print("Host: ");
  client.print(VPS_HOST);
  client.print("\r\n");
  client.print("Content-Type: application/json\r\n");
  client.print("Content-Length: ");
  client.print(payload.length());
  client.print("\r\n\r\n");
  client.print(payload);
  
  String response = "";
  unsigned long timeout = millis() + 10000;
  while (client.connected() && millis() < timeout) {
    while (client.available()) {
      response += (char)client.read();
    }
  }
  client.stop();
  
  // FIXED: ArduinoJson v7 - use .as<String>()
  JsonDocument res;
  DeserializationError error = deserializeJson(res, response);
  if (error) {
    message = "JSON parse error";
    return false;
  }
  
  // FIXED: Use .as<String>() for ArduinoJson v7
  bool success = res["success"].as<bool>();
  message = res["message"].as<String>();
  
  if (success) {
    addSession(ip, 15UL * 60 * 1000UL); // 15 minutes
    Serial.println("[VPS] Verified: " + txid);
  }
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
  server.on("/pay", HTTP_GET, paymentPage);
  server.on("/verify", HTTP_POST, handleVerify);
  server.on("/success", HTTP_GET, successPage);
  server.on("/admin", HTTP_GET, adminPanel);
  
  // Captive portal - Android/iOS/Windows
  server.on("/generate_204", HTTP_GET, []() {
    String ip = server.client().remoteIP().toString();
    if (isAuthorized(ip)) {
      server.send(204, "text/plain", "");
    } else {
      server.sendHeader("Location", "http://192.168.4.1/", true);
      server.send(302, "text/html", "");
    }
  });
  
  server.on("/gen_204", HTTP_GET, []() {
    String ip = server.client().remoteIP().toString();
    if (isAuthorized(ip)) {
      server.send(204, "text/plain", "");
    } else {
      server.sendHeader("Location", "http://192.168.4.1/", true);
      server.send(302, "text/html", "");
    }
  });
  
  server.on("/hotspot-detect.html", HTTP_GET, []() {
    String ip = server.client().remoteIP().toString();
    if (isAuthorized(ip)) {
      server.send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
    } else {
      server.sendHeader("Location", "http://192.168.4.1/", true);
      server.send(302, "text/html", "");
    }
  });
  
  server.onNotFound([]( ) {
    String ip = server.client().remoteIP().toString();
    if (isAuthorized(ip)) {
      server.send(204, "text/plain", "");
    } else {
      server.sendHeader("Location", "http://192.168.4.1/", true);
      server.send(302, "text/html", "");
    }
  });
  
  server.begin();
  Serial.println("[HTTP] Server started on port 80");
}

// ==================== HTML PAGES ====================
void portalPage() {
  String clientIP = server.client().remoteIP().toString();
  if (isAuthorized(clientIP)) {
    server.sendHeader("Location", "http://google.com", true);
    server.send(302, "text/html", "Redirecting...");
    return;
  }
  
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Bushiri WiFi</title>
<style>
body { font-family: Arial; text-align: center; padding: 50px; background: #111; color: #fff; }
h1 { color: #f00; font-size: 2.5em; }
p { font-size: 1.2em; margin: 20px 0; }
button { padding: 20px 40px; font-size: 1.5em; background: #f00; color: #fff; border: none; border-radius: 10px; cursor: pointer; }
</style>
</head>
<body>
<h1>📶 Bushiri WiFi</h1>
<p>TZS 800 = 15 Hours Internet</p>
<p>💳 MIXX: )rawliteral" + String(MIXX_NUMBER) + R"rawliteral(</p>
<a href="/pay"><button>💰 Lipa Sasa</button></a>
<p style="font-size: 0.8em; margin-top: 30px; color: #ccc;">
Test TXID: TEST123 (1 minute free)
</p>
</body>
</html>
)rawliteral";
  
  server.send(200, "text/html", html);
}

void paymentPage() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Payment - Bushiri</title>
</head>
<body style="text-align:center; padding:50px; background:#111; color:#fff; font-family:Arial;">
<h2>✅ Thibitisha Malipo</h2>
<p style="font-size:1.1em;">Weka TXID kutoka SMS ya MIXX</p>
<form method="POST" action="/verify" style="max-width:350px; margin:0 auto;">
<input name="txid" placeholder="TXID (k.m. 26205921931320)" style="width:100%; padding:15px; margin:10px 0; font-size:18px; border-radius:8px; border:1px solid #444; box-sizing:border-box;"><br>
<input name="phone" placeholder="Phone (0717633805)" style="width:100%; padding:15px; margin:10px 0; font-size:18px; border-radius:8px; border:1px solid #444; box-sizing:border-box;"><br>
<button type="submit" style="width:100%; padding:20px; background:#0f0; color:#000; font-size:20px; border:none; border-radius:10px; cursor:pointer; font-weight:bold;">🚀 Verify & Connect Internet</button>
</form>
<p><a href="/" style="color:#0ff; font-size:1.1em;">← Rudi Nyuma</a></p>
</body>
</html>
)rawliteral";
  server.send(200, "text/html", html);
}

void handleVerify() {
  String txid = server.arg("txid");
  String phone = server.arg("phone");
  String ip = server.client().remoteIP().toString();
  
  Serial.println("[VERIFY] TXID=" + txid + " IP=" + ip);
  
  // Test mode
  if (txid == "TEST123") {
    addSession(ip, 60000UL); // 1 minute
    server.sendHeader("Location", "/success", true);
    server.send(302);
    return;
  }
  
  String message;
  if (verifyWithVPS(txid, ip, message)) {
    server.sendHeader("Location", "/success", true);
  } else {
    String html = "<!DOCTYPE html><html><body style='text-align:center;padding:50px;background:#111;color:#f00;'>"
      "<h1>❌ " + message + "</h1>"
      "<a href='/pay' style='color:#0ff;font-size:1.2em;'>Retry Payment</a>"
      "</body></html>";
    server.send(200, "text/html", html);
  }
  server.send(302);
}

void successPage() {
  server.send(200, "text/html", 
    "<!DOCTYPE html><html><body style='text-align:center;padding:100px;background:#0f0;color:#000;'>"
    "<h1>✅ Malipo Yamekubalika!</h1>"
    "<h2>Internet imewashwa...</h2>"
    "<script>setTimeout(function(){window.location='http://google.com';},3000);</script>"
    "</body></html>");
}

void adminPanel() {
  String html = "<h1>Bushiri Control Panel v" + String(VERSION) + "</h1>"
    "<p><b>WiFi Status:</b> " + String(WiFi.status() == WL_CONNECTED ? "🟢 UP (" + WiFi.SSID() + ")" : "🔴 DOWN") + "</p>"
    "<p><b>Clients:</b> " + String(clientCount) + "</p>"
    "<p><b>NAT:</b> " + String(natEnabled ? "🟢 ON" : "🔴 OFF") + "</p>"
    "<p><b>IP:</b> 192.168.4.1</p><hr>";
    
  html += "<h3>Active Sessions:</h3><ul>";
  int active = 0;
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (sessions[i].active && millis() < sessions[i].expiry) {
      html += "<li>" + sessions[i].ip + " (" + String((sessions[i].expiry - millis()) / 1000) + "s left)</li>";
      active++;
    }
  }
  if (!active) html += "<li>None</li>";
  html += "</ul>";
    
  server.send(200, "text/html", html);
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n🚀 BUSHIRI v" VERSION " - ESP32 3.3.8");
  
  prefs.begin("bushiri");
  
  // AP + STA mode
  WiFi.mode(WIFI_AP_STA);
  
  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.println("[AP] Bushiri WiFi @ 192.168.4.1");
  
  connectToInternet();
  if (WiFi.status() == WL_CONNECTED) {
    delay(2000);
    enableNAT();
  }
  
  dnsServer.start(53, "*", apIP);
  setupWebServer();
  Serial.println("\n✅ READY!");
  Serial.println("Admin: http://192.168.4.1/admin");
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  maintainWiFi();
  
  clientCount = WiFi.softAPgetStationNum();
  delay(10);
}