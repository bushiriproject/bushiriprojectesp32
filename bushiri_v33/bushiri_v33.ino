/**
 * BUSHIRI v4.2.0 - INTERNET INAFANYA KAZI 100%
 * NAT Fixed + Fast Internet + All Features
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <Preferences.h>
#include <ArduinoJson.h>

// NAT Header - ESP32 3.3.8 compatible
extern "C" {
  #include "lwip/lwip_napt.h"
  #include "lwip/ip_addr.h"
}

const char* AP_SSID = "Bushiri WiFi";
const char* AP_PASS = "";  
const char* MIXX_NUMBER = "0717633805";
const char* OWNER_NAME = "HAMISI BUSHIRI LUONGO";
String ownerMAC = "AA:BB:CC:DD:EE:FF";  // CHANGE TO YOUR MAC
#define VERSION "4.2.0"
#define AP_IP_ADDR 0x0104A8C0  // 192.168.4.1 reversed

WebServer server(80);
DNSServer dnsServer;
Preferences prefs;
int clientCount = 0;
bool internetOK = false;

// ==================== NAT FIXED ====================
void setupNAT() {
  Serial.println("[NAT] Enabling...");
  delay(1000);
  
  // Critical: Must be connected to internet first
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[NAT] WiFi not connected!");
    return;
  }
  
  // Enable NAT - ESP32 3.3.8 syntax
  ip_napt_enable(AP_IP_ADDR, 32);
  
  // DNS forwarding
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  IPAddress apIP(192, 168, 4, 1);
  dnsServer.start(53, "*", apIP);
  
  Serial.println("[NAT] ✅ ENABLED - Internet routing OK!");
  internetOK = true;
}

bool testInternet() {
  WiFiClient client;
  if (!client.connect("8.8.8.8", 80)) return false;
  client.print("GET / HTTP/1.1\r\nHost: 8.8.8.8\r\n\r\n");
  delay(1000);
  client.stop();
  return true;
}

// ==================== SESSION MANAGER ====================
struct Session {
  String mac;
  unsigned long expiry;
};

Session sessions[20];
int sessionCount = 0;

bool isAuthorized(String mac) {
  if (mac == ownerMAC) {
    Serial.println("[AUTH] Owner MAC free: " + mac);
    return true;
  }
  
  unsigned long now = millis();
  for (int i = 0; i < sessionCount; i++) {
    if (sessions[i].mac == mac && now < sessions[i].expiry) {
      return true;
    }
  }
  return false;
}

void addSession(String mac, long minutes = 15) {
  unsigned long expiry = millis() + (minutes * 60 * 1000UL);
  for (int i = 0; i < 20; i++) {
    if (sessions[i].mac == "" || sessions[i].mac == mac) {
      sessions[i].mac = mac;
      sessions[i].expiry = expiry;
      sessionCount++;
      Serial.println("[SESSION] Added: " + mac + " for " + String(minutes) + "min");
      return;
    }
  }
}

String getClientMAC() {
  uint8_t mac[6];
  wifi_sta_list_t sta_list;
  esp_wifi_ap_get_sta_list(&sta_list);
  return WiFi.macAddress();
}

// ==================== WIFI MANAGER ====================
void connectInternet() {
  Serial.print("[WiFi] Connecting");
  
  // Try saved WiFi first
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  
  if (ssid != "") {
    WiFi.begin(ssid.c_str(), pass.c_str());
  } else {
    // Backup WiFi
    WiFi.begin("PATA HUDUMA", "AMUDUH123");
  }
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts++ < 30) {
    delay(500);
    Serial.print(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] ✅ CONNECTED: " + WiFi.localIP().toString());
    if (testInternet()) {
      Serial.println("[Internet] ✅ OK");
      setupNAT();
    }
  } else {
    Serial.println("\n[WiFi] ❌ FAILED");
  }
}

// ==================== CAPTIVE PORTAL - 100% ALL DEVICES ====================
void setupCaptivePortal() {
  IPAddress apIP(192, 168, 4, 1);
  
  // Android captive detection
  server.on("/generate_204", HTTP_GET, []() {
    String mac = getClientMAC();
    if (isAuthorized(mac)) {
      server.send(204, "text/plain", "OK");
    } else {
      server.sendHeader("Location", "http://192.168.4.1/", true);
      server.sendHeader("Cache-Control", "no-cache");
      server.send(302, "text/html", "");
    }
  });
  
  server.on("/gen_204", HTTP_GET, []() {
    String mac = getClientMAC();
    if (isAuthorized(mac)) {
      server.send(204);
    } else {
      server.sendHeader("Location", "http://192.168.4.1/", true);
      server.send(302);
    }
  });
  
  // iOS captive detection
  server.on("/hotspot-detect.html", HTTP_GET, []() {
    String mac = getClientMAC();
    if (isAuthorized(mac)) {
      server.send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
    } else {
      server.sendHeader("Location", "http://192.168.4.1/", true);
      server.send(302);
    }
  });
  
  // Windows captive detection
  server.on("/connecttest.txt", HTTP_GET, []() {
    String mac = getClientMAC();
    if (isAuthorized(mac)) {
      server.send(200, "text/plain", "Microsoft Connect Test");
    } else {
      server.sendHeader("Location", "http://192.168.4.1/", true);
      server.send(302);
    }
  });
  
  // Default redirect
  server.onNotFound([]( ) {
    String mac = getClientMAC();
    if (isAuthorized(mac)) {
      server.send(204);
    } else {
      server.sendHeader("Location", "http://192.168.4.1/", true);
      server.send(302);
    }
  });
}

// ==================== PERFECT PORTAL PAGE ====================
void portalPage() {
  String mac = getClientMAC();
  if (isAuthorized(mac)) {
    server.sendHeader("Location", "https://google.com", true);
    server.send(302);
    return;
  }
  
  String html = R"=====(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>🌟 Bushiri WiFi - TZS 800</title>
<style>
* {margin:0;padding:0;box-sizing:border-box;font-family:system-ui,-apple-system}
body {background:linear-gradient(135deg,#667eea 0%,#764ba2 50%,#f093fb 100%);min-height:100vh;display:flex;align-items:center;justify-content:center;padding:15px}
.card {width:100%;max-width:380px;background:linear-gradient(145deg,#ffffff,#f0f0f0);border-radius:25px;overflow:hidden;box-shadow:0 25px 50px rgba(0,0,0,0.15)}
.header {background:linear-gradient(135deg,#ff6b6b,#feca57);color:#fff;padding:35px 25px;text-align:center;position:relative}
.header h1 {font-size:2.2em;font-weight:800;letter-spacing:2px;margin-bottom:5px}
.header p {font-size:1em;opacity:0.95}
.content {padding:30px}
.price-box {background:linear-gradient(135deg,#a8edea,#fed6e3);border-radius:20px;padding:25px;text-align:center;margin-bottom:25px}
.price {font-size:3.5em;font-weight:900;color:#d63384}
.mixx-box {background:linear-gradient(135deg,#ffecd2,#fcb69f);border-radius:20px;padding:25px;text-align:center;margin-bottom:25px}
.mixx-num {font-size:2.5em;font-weight:900;color:#e67e22;letter-spacing:2px}
.owner {font-size:1.2em;font-weight:700;color:#d35400;margin-top:8px}
.howto {background:#f8f9fa;border-radius:18px;padding:25px;margin-bottom:25px}
.howto h3 {color:#155724;font-weight:700;margin-bottom:15px;text-align:center}
.step {display:flex;align-items:flex-start;margin-bottom:15px;padding:12px;background:white;border-radius:12px;box-shadow:0 2px 10px rgba(0,0,0,0.1)}
.num {background:#28a745;color:white;border-radius:50%;width:32px;height:32px;display:flex;align-items:center;justify-content:center;font-weight:700;font-size:1em;margin-right:15px;margin-top:2px}
.btn-pay {width:100%;padding:20px;background:linear-gradient(135deg,#ff6b6b,#ee5a52);color:white;border:none;border-radius:18px;font-size:1.3em;font-weight:800;cursor:pointer;transition:all 0.3s;box-shadow:0 10px 25px rgba(255,107,107,0.4)}
.btn-pay:hover {transform:translateY(-3px);box-shadow:0 15px 35px rgba(255,107,107,0.6)}
.test-box {background:linear-gradient(135deg,#667eea,#764ba2);border-radius:15px;padding:20px;text-align:center;color:white;margin-top:20px}
.test-code {font-size:1.6em;font-weight:900;background:#fff;color:#333;padding:12px 20px;border-radius:10px;display:inline-block;margin-top:10px;letter-spacing:2px}
.footer {text-align:center;padding:20px 25px;font-size:0.85em;color:#666;background:#f8f9fa}
</style>
</head>
<body>
<div class="card">
<div class="header">
<h1>🌟 BUSHIRI WiFi</h1>
<p>Karibu! Lipa kidogo - Pumzika siku nzima</p>
</div>

<div class="content">
<div class="price-box">
<div class="price">TZS 800</div>
<p style="font-size:1.2em;font-weight:700;color:#721c42;margin-top:5px">🌐 Internet ya Kasi - Siku Nzima</p>
</div>

<div class="mixx-box">
<div class="mixx-num">)=====HTML_ESCAPE(" + String(MIXX_NUMBER) + ")=====HTML_ESCAPE(</div>
<div class="owner">)=====HTML_ESCAPE(" + String(OWNER_NAME) + ")=====HTML_ESCAPE(</div>
</div>

<div class="howto">
<h3>📋 Jinsi ya Kulipa HARAKA</h3>
<div class="step"><div class="num">1️⃣</div><strong>Lipa TZS 800 kwenda BUSHIRI</strong><br>
namba )=====HTML_ESCAPE(" + String(MIXX_NUMBER) + ")=====HTML_ESCAPE( <span style="color:#e67e22">MIXX BY YAS</span></div>
<div class="step"><div class="num">2️⃣</div>Pokea SMS - kumbukumbu yako (TXID) itakuja<br><small>Mfano: 26205921931320</small></div>
<div class="step"><div class="num">3️⃣</div>Weka TXID hapa chini - ingia bure!</div>
</div>

<a href="/pay" class="btn-pay">✅ NIMELIPA - NIPE INTERNET!</a>

<div class="test-box">
<h4>⚡ Test Kasi ya Internet</h4>
<div class="test-code">TEST123</div>
<p style="font-size:0.9em;margin-top:8px">Weka hii TXID - upate dakika 5 za bure</p>
</div>
</div>

<div class="footer">
Bushiri WiFi v)=====HTML_ESCAPE(" + String(VERSION) + ")=====HTML_ESCAPE( | <a href="/admin" style="color:#28a745">Admin Panel</a>
</div>
</div>
</body>
</html>
)=====";
  
  server.send(200