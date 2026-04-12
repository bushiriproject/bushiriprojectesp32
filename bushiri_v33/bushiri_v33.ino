/**
 * PROJECT BUSHIRI v3.3
 * MPESA/MIXX Captive Portal + NAT Router + VPS Verify
 * Bei: TZS 800 = Masaa 15
 *
 * Fixed for:
 *  - ESP32 Arduino core v3.3.x (ip_napt_enable returns void)
 *  - ArduinoJson v7 (JsonDocument badala ya DynamicJsonDocument)
 *  - Single file - hakuna redefinition
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
const char* STA_SSID_ALT = "infinitynetwork";
const char* STA_PASS_ALT = ".kibushi1";
String ownerIP           = "192.168.4.2";
// ======================================================

#define VERSION      "3.3.0"
#define MAX_CLIENTS  20
// 192.168.4.1 = 0xC0A80401  (htonl itabadilisha byte order)
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

String sta_ssid       = "";
String sta_pass       = "";
int    clientCount    = 0;
bool   natEnabled     = false;
unsigned long lastHB  = 0;

// ==================== FORWARD DECLARATIONS ====================
void portalPage();
void paymentPage();
void handleVerify();
void successPage();
void adminPanel();
void wifiConfigPage();
void saveWifiConfig();
void sendErrorPage(String msg);
void captiveRedirect();
void setupWebServer();
void setupOTA();
bool verifyWithVPS(String txid, String ip, String &message);
void enableNAT();
void connectToInternet();
void maintainWiFi();
String getClientIP();

// ==================== NAT ====================
// NOTA: ip_napt_enable() katika core v3.x inarudisha void — usijaribu kukamata return value
void enableNAT() {
  delay(500);
  ip_napt_enable(htonl(AP_IP_HEX), 1);
  natEnabled = true;
  Serial.println("[NAT] Imewashwa - 192.168.4.1");
}

// ==================== WIFI ====================
void connectToInternet() {
  sta_ssid = prefs.getString("sta_ssid", "");
  sta_pass = prefs.getString("sta_pass", "");

  if (sta_ssid.length() > 0) {
    Serial.print("[WiFi] Unganika: " + sta_ssid);
    WiFi.begin(sta_ssid.c_str(), sta_pass.c_str());
    for (int t = 0; t < 20 && WiFi.status() != WL_CONNECTED; t++) {
      delay(500); Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n[WiFi] OK: " + WiFi.localIP().toString());
      return;
    }
    Serial.println("\n[WiFi] Imeshindwa, jaribu backup...");
  }

  Serial.print("[WiFi] Backup: " + String(STA_SSID_ALT));
  WiFi.begin(STA_SSID_ALT, STA_PASS_ALT);
  for (int t = 0; t < 20 && WiFi.status() != WL_CONNECTED; t++) {
    delay(500); Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Backup OK: " + WiFi.localIP().toString());
  } else {
    Serial.println("\n[WiFi] Hakuna internet");
  }
}

void maintainWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    natEnabled = false;
    Serial.println("[WiFi] Imekatika - inajaribu...");
    connectToInternet();
    if (WiFi.status() == WL_CONNECTED) enableNAT();
  }
}

// ==================== VPS VERIFY ====================
bool verifyWithVPS(String txid, String ip, String &message) {
  if (WiFi.status() != WL_CONNECTED) {
    message = "Hakuna internet - jaribu tena";
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(30);

  if (!client.connect(VPS_HOST, VPS_PORT)) {
    message = "VPS haipatikani - jaribu tena";
    return false;
  }

  // ArduinoJson v7: tumia JsonDocument (DynamicJsonDocument imetolewa)
  JsonDocument doc;
  doc["txid"]  = txid;
  doc["mac"]   = ip;
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

  String response   = "";
  unsigned long tout = millis() + 15000;
  bool headersEnded = false;
  while (client.connected() && millis() < tout) {
    if (client.available()) {
      String line = client.readStringUntil('\n');
      if (line == "\r") headersEnded = true;
      if (headersEnded) response += line;
    }
  }
  client.stop();
  response.trim();

  JsonDocument res;
  DeserializationError err = deserializeJson(res, response);
  if (err) {
    message = "VPS ilijibu vibaya";
    return false;
  }

  bool success = res["success"] | false;
  message      = res["message"] | String("Hitilafu");

  if (success) {
    addSession(ip, 15UL * 3600000UL); // Masaa 15
  }
  return success;
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("=================================");
  Serial.println("  BUSHIRI v" VERSION " - APSTA+NAT  ");
  Serial.println("=================================");

  prefs.begin("bushiri");

  // Mode APSTA: STA = internet kutoka modem, AP = kusambaza kwa wateja
  WiFi.mode(WIFI_AP_STA);

  // Sanidi AP
  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID, (strlen(AP_PASS) >= 8) ? AP_PASS : NULL, 6, 0, 8);
  Serial.println("[AP] Imewashwa: " + String(AP_SSID));

  // Unganika na internet
  connectToInternet();

  // Washa NAT kama internet ipo
  if (WiFi.status() == WL_CONNECTED) {
    delay(1000);
    enableNAT();
  }

  // DNS - captive portal (jibu "*" na IP ya AP)
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", apIP);

  setupWebServer();
  setupOTA();

  Serial.println("[READY] Admin: http://192.168.4.1/admin");
}

// ==================== LOOP ====================
void loop() {
  dnsServer.processNextRequest();
  server.handleClient();

  if (millis() - lastHB > 30000) {
    lastHB      = millis();
    clientCount = WiFi.softAPgetStationNum();
    Serial.printf("[HB] Clients:%d Sessions:%d NAT:%s WiFi:%s\n",
      clientCount, sessionCount,
      natEnabled ? "ON" : "OFF",
      WiFi.status() == WL_CONNECTED ? WiFi.SSID().c_str() : "X");

    maintainWiFi();
    // Refresh NAT periodically
    if (WiFi.status() == WL_CONNECTED) {
      ip_napt_enable(htonl(AP_IP_HEX), 1);
      natEnabled = true;
    }
  }

  delay(10);
}

// ==================== WEB SERVER ====================
void setupWebServer() {
  server.on("/", HTTP_GET, portalPage);
  server.on("/pay", HTTP_GET, paymentPage);
  server.on("/verify", HTTP_POST, handleVerify);
  server.on("/success", HTTP_GET, successPage);
  server.on("/admin", HTTP_GET, adminPanel);
  server.on("/wifi-config", HTTP_GET, wifiConfigPage);
  server.on("/wifi-save", HTTP_POST, saveWifiConfig);

  // Captive portal detection endpoints
  auto cpHandler = [](int code, const char* body) {
    return [code, body]() {
      String ip = server.client().remoteIP().toString();
      if (isAuthorized(ip)) {
        server.send(code, "text/plain", body);
      } else {
        server.sendHeader("Location", "http://192.168.4.1/");
        server.send(302, "text/plain", "");
      }
    };
  };

  server.on("/generate_204", HTTP_GET, []() {          // Android
    String ip = server.client().remoteIP().toString();
    if (isAuthorized(ip)) server.send(204, "text/plain", "");
    else { server.sendHeader("Location","http://192.168.4.1/"); server.send(302,"text/plain",""); }
  });
  server.on("/hotspot-detect.html", HTTP_GET, []() {   // iOS
    String ip = server.client().remoteIP().toString();
    if (isAuthorized(ip)) server.send(200,"text/html","<HTML><BODY>Success</BODY></HTML>");
    else { server.sendHeader("Location","http://192.168.4.1/"); server.send(302,"text/plain",""); }
  });
  server.on("/connecttest.txt", HTTP_GET, []() {       // Windows
    String ip = server.client().remoteIP().toString();
    if (isAuthorized(ip)) server.send(200,"text/plain","Microsoft Connect Test");
    else { server.sendHeader("Location","http://192.168.4.1/"); server.send(302,"text/plain",""); }
  });
  server.on("/success.txt", HTTP_GET, []() {           // Samsung
    String ip = server.client().remoteIP().toString();
    if (isAuthorized(ip)) server.send(200,"text/plain","success");
    else { server.sendHeader("Location","http://192.168.4.1/"); server.send(302,"text/plain",""); }
  });

  server.onNotFound(captiveRedirect);
  server.begin();
  Serial.println("[Web] Server imeanza port 80");
}

String getClientIP() {
  return server.client().remoteIP().toString();
}

void captiveRedirect() {
  String ip = getClientIP();
  if (isAuthorized(ip)) { server.send(204, "text/plain", ""); return; }
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.send(302, "text/plain", "");
}

// ==================== PORTAL PAGE ====================
void portalPage() {
  String ip = getClientIP();
  if (isAuthorized(ip)) { server.sendHeader("Location","http://google.com"); server.send(302); return; }

  String html = R"(<!DOCTYPE html><html><head>
<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>BUSHIRI HOTSPOT</title>
<style>
*{margin:0;padding:0;box-sizing:border-box;font-family:'Segoe UI',sans-serif}
body{background:linear-gradient(135deg,#1a1a2e,#16213e,#0f3460);min-height:100vh;display:flex;align-items:center;justify-content:center}
.card{max-width:380px;width:93%;background:white;border-radius:20px;overflow:hidden;box-shadow:0 25px 50px rgba(0,0,0,0.4)}
.header{background:linear-gradient(135deg,#e91e63,#c2185b);color:white;padding:30px;text-align:center}
.wifi-icon{font-size:3em;margin-bottom:8px}
.brand{font-size:1.5em;font-weight:700;letter-spacing:2px}
.tagline{font-size:.85em;opacity:.9;margin-top:4px}
.body{padding:25px}
.price-box{background:#f8f9fa;border-radius:12px;padding:18px;text-align:center;margin-bottom:20px;border:2px solid #e91e63}
.price{font-size:2.2em;font-weight:800;color:#e91e63}
.price-label{color:#666;font-size:.9em;margin-top:3px}
.step{display:flex;align-items:center;padding:8px 0;border-bottom:1px solid #f0f0f0;font-size:.9em;color:#444}
.step-num{background:#e91e63;color:white;border-radius:50%;width:24px;height:24px;display:flex;align-items:center;justify-content:center;font-weight:bold;font-size:.8em;margin-right:10px;flex-shrink:0}
.btn{display:block;width:100%;padding:15px;background:linear-gradient(135deg,#e91e63,#c2185b);color:white;border:none;border-radius:12px;font-size:1.1em;font-weight:700;text-align:center;text-decoration:none;margin-top:15px}
.mixx-num{background:#e8f5e9;border-radius:8px;padding:10px;text-align:center;font-weight:700;font-size:1.1em;color:#2e7d32;margin:10px 0}
</style></head><body>
<div class='card'>
  <div class='header'>
    <div class='wifi-icon'>📶</div>
    <div class='brand'>)" + String(PORTAL_TITLE) + R"(</div>
    <div class='tagline'>Internet ya haraka na ya uhakika</div>
  </div>
  <div class='body'>
    <div class='price-box'>
      <div class='price'>TZS 800</div>
      <div class='price-label'>= Masaa 15 ya internet</div>
    </div>
    <div class='step'><div class='step-num'>1</div>Tuma TZS 800 kwa MIXX BY YAS</div>
    <div class='step'><div class='step-num'>2</div>Nambari ya kulipa:</div>
    <div class='mixx-num'>📱 )" + String(MIXX_NUMBER) + R"(</div>
    <div class='step'><div class='step-num'>3</div>Bonyeza hapa chini - weka namba ya kumbukumbu</div>
    <a href='/pay' class='btn'>✅ Nimelipa - Ingia Sasa</a>
  </div>
</div></body></html>)";

  server.send(200, "text/html", html);
}

// ==================== PAYMENT PAGE ====================
void paymentPage() {
  String html = R"(<!DOCTYPE html><html><head>
<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Thibitisha Malipo</title>
<style>
*{margin:0;padding:0;box-sizing:border-box;font-family:'Segoe UI',sans-serif}
body{background:linear-gradient(135deg,#1a1a2e,#16213e,#0f3460);min-height:100vh;display:flex;align-items:center;justify-content:center}
.card{max-width:380px;width:93%;background:white;border-radius:20px;overflow:hidden;box-shadow:0 25px 50px rgba(0,0,0,0.4)}
.header{background:linear-gradient(135deg,#2196F3,#1565C0);color:white;padding:25px;text-align:center}
.header h2{font-size:1.3em;margin-bottom:5px}.header p{font-size:.85em;opacity:.9}
.body{padding:25px}
.info-box{background:#e3f2fd;border-radius:10px;padding:15px;margin-bottom:20px;font-size:.85em;color:#1565C0;line-height:1.6}
label{display:block;font-weight:600;color:#333;margin-bottom:6px;font-size:.9em}
input{width:100%;padding:14px;border:2px solid #ddd;border-radius:10px;font-size:1em;margin-bottom:15px;box-sizing:border-box}
input:focus{outline:none;border-color:#2196F3}
.btn{width:100%;padding:15px;background:linear-gradient(135deg,#e91e63,#c2185b);color:white;border:none;border-radius:12px;font-size:1.1em;font-weight:700;cursor:pointer}
.back{display:block;text-align:center;margin-top:12px;color:#666;font-size:.85em;text-decoration:none}
#loading{display:none;text-align:center;padding:10px;color:#666}
</style></head><body>
<div class='card'>
  <div class='header'>
    <h2>✅ Thibitisha Malipo</h2>
    <p>Weka namba ya kumbukumbu ya MIXX BY YAS</p>
  </div>
  <div class='body'>
    <div class='info-box'>📩 Baada ya kutuma pesa utapata SMS kutoka MIXX BY YAS.<br>SMS hiyo ina <b>Kumbukumbu no.</b><br>Mfano: <b>26205921931320</b></div>
    <form method='POST' action='/verify' onsubmit='showLoading()'>
      <label>Namba ya Kumbukumbu (TXID):</label>
      <input type='text' name='txid' placeholder='Mfano: 26205921931320' required maxlength='20'>
      <label>Namba yako ya Simu:</label>
      <input type='tel' name='phone' placeholder='0717633805' required maxlength='13'>
      <button type='submit' class='btn'>🚀 Ingia Sasa</button>
    </form>
    <div id='loading'>⏳ Inathibitisha... Subiri...</div>
    <a href='/' class='back'>← Rudi Nyuma</a>
  </div>
</div>
<script>function showLoading(){document.querySelector('.btn').style.display='none';document.getElementById('loading').style.display='block';}</script>
</body></html>)";
  server.send(200, "text/html", html);
}

// ==================== HANDLE VERIFY ====================
void handleVerify() {
  String txid  = server.arg("txid");
  String phone = server.arg("phone");
  String ip    = getClientIP();
  txid.trim();
  Serial.println("[Verify] TXID=" + txid + " Phone=" + phone + " IP=" + ip);

  if (txid == "BUSHIRIPROJECT") {
    addSession(ip, 30UL * 60000UL);
    server.sendHeader("Location", "/success?phone=" + phone + "&trial=1");
    server.send(302); return;
  }
  if (txid.length() < 6) { sendErrorPage("TXID ni fupi mno. Angalia SMS yako tena."); return; }

  String message = "";
  if (verifyWithVPS(txid, ip, message)) {
    server.sendHeader("Location", "/success?phone=" + phone);
    server.send(302);
  } else {
    sendErrorPage(message);
  }
}

// ==================== SUCCESS PAGE ====================
void successPage() {
  String phone   = server.arg("phone");
  bool   isTrial = server.arg("trial") == "1";
  String html = R"(<!DOCTYPE html><html><head>
<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<meta http-equiv='refresh' content='5;url=http://google.com'>
<title>Umefanikiwa!</title>
<style>
*{margin:0;padding:0;box-sizing:border-box;font-family:'Segoe UI',sans-serif}
body{background:linear-gradient(135deg,#00c853,#1b5e20);min-height:100vh;display:flex;align-items:center;justify-content:center;color:white;text-align:center;padding:20px}
.card{max-width:350px;width:100%}
.icon{font-size:5em;margin-bottom:15px}
h1{font-size:1.8em;margin-bottom:10px}
.info{background:rgba(255,255,255,.2);border-radius:12px;padding:15px;margin:20px 0}
.info-item{display:flex;justify-content:space-between;padding:5px 0;font-size:.9em}
.btn{display:inline-block;margin-top:15px;padding:12px 25px;background:white;color:#00c853;border-radius:10px;font-weight:700;text-decoration:none}
</style></head><body>
<div class='card'>
  <div class='icon'>🎉</div>
  <h1>Hongera!</h1>
  <p>Internet imewashwa!</p>
  <div class='info'>
    <div class='info-item'><span>📱 Simu</span><span>)" + phone.substring(0,4) + R"(******</span></div>
    <div class='info-item'><span>⏰ Muda</span><span>)" + String(isTrial?"Dakika 30 (Trial)":"Masaa 15") + R"(</span></div>
  </div>
  <p>Inakupeleka Google baada ya sekunde 5...</p>
  <a href='http://google.com' class='btn'>🌐 Nenda Internet</a>
</div></body></html>)";
  server.send(200, "text/html", html);
}

// ==================== ERROR PAGE ====================
void sendErrorPage(String message) {
  String html = R"(<!DOCTYPE html><html><head>
<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Hitilafu</title>
<style>
*{margin:0;padding:0;box-sizing:border-box;font-family:'Segoe UI',sans-serif}
body{background:linear-gradient(135deg,#b71c1c,#c62828);min-height:100vh;display:flex;align-items:center;justify-content:center;color:white;text-align:center;padding:20px}
.card{max-width:350px;width:100%}
.icon{font-size:4em;margin-bottom:15px}
h2{font-size:1.5em;margin-bottom:15px}
.msg{background:rgba(255,255,255,.2);border-radius:12px;padding:15px;margin:15px 0;font-size:.95em;line-height:1.5}
.btn{display:inline-block;margin:8px;padding:12px 25px;background:white;color:#b71c1c;border-radius:10px;font-weight:700;text-decoration:none}
</style></head><body>
<div class='card'>
  <div class='icon'>❌</div>
  <h2>Malipo Hayakuthibitishwa</h2>
  <div class='msg'>)" + message + R"(</div>
  <a href='/pay' class='btn'>🔄 Jaribu Tena</a>
  <a href='/' class='btn'>🏠 Nyumbani</a>
</div></body></html>)";
  server.send(200, "text/html", html);
}

// ==================== ADMIN PANEL ====================
void adminPanel() {
  String inet = (WiFi.status()==WL_CONNECTED)
    ? "🟢 "+WiFi.SSID()+" ("+WiFi.localIP().toString()+")"
    : "🔴 Haijaunganika";

  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Admin</title>";
  html += "<style>body{background:#0d1117;color:#e6edf3;font-family:monospace;padding:20px;font-size:14px}";
  html += "h1{color:#58a6ff}h3{color:#3fb950;margin:15px 0 8px}";
  html += ".box{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:12px;margin:10px 0}";
  html += ".stat{display:flex;justify-content:space-between;padding:4px 0;border-bottom:1px solid #21262d}";
  html += "a{color:#58a6ff;text-decoration:none;margin-right:15px}";
  html += ".ip{font-size:11px;color:#8b949e;padding:3px 0}</style></head><body>";
  html += "<h1>🛜 BUSHIRI v" VERSION " Admin</h1><div class='box'>";
  html += "<div class='stat'><span>Internet</span><span>" + inet + "</span></div>";
  html += "<div class='stat'><span>NAT</span><span>" + String(natEnabled?"🟢 ON":"🔴 OFF") + "</span></div>";
  html += "<div class='stat'><span>Wateja</span><span>" + String(clientCount) + "</span></div>";
  html += "<div class='stat'><span>Sessions</span><span>" + String(sessionCount) + "</span></div>";
  html += "</div><h3>Sessions Aktif:</h3><div class='box'>";
  int active = 0;
  for (int i = 0; i < sessionCount; i++) {
    if (sessions[i].active && millis() < sessions[i].expiry) {
      html += "<div class='ip'>" + sessions[i].ip + " | Dakika " +
              String((sessions[i].expiry-millis())/60000) + " zimebaki</div>";
      active++;
    }
  }
  if (!active) html += "<div class='ip'>Hakuna sessions</div>";
  html += "</div><h3>Mipangilio:</h3>";
  html += "<a href='/wifi-config'>⚙️ WiFi Config</a>";
  html += "<a href='/update'>🔄 OTA</a><br><br><a href='/'>🏠 Portal</a>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

// ==================== WIFI CONFIG ====================
void wifiConfigPage() {
  String status = (WiFi.status()==WL_CONNECTED)
    ? "🟢 "+WiFi.SSID()+" | "+WiFi.localIP().toString()
    : "🔴 Haijaunganika";

  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'><title>WiFi Config</title>";
  html += "<style>body{background:#111;color:#fff;font-family:monospace;padding:20px}h2{color:#e91e63}";
  html += "input{width:100%;padding:12px;margin:6px 0 14px;background:#222;color:#fff;border:1px solid #444;border-radius:8px;font-size:15px;box-sizing:border-box}";
  html += ".btn{width:100%;padding:14px;background:#e91e63;color:white;border:none;border-radius:8px;font-size:16px;cursor:pointer;font-weight:bold}";
  html += ".status{padding:12px;background:#1a1a1a;border-left:4px solid #e91e63;margin:12px 0;border-radius:4px}";
  html += "a{color:#e91e63}</style></head><body>";
  html += "<h2>⚙️ WiFi Config</h2><div class='status'>" + status + "</div>";
  html += "<form method='POST' action='/wifi-save'>";
  html += "<label>SSID:</label><input type='text' name='ssid' value='" + sta_ssid + "' placeholder='Jina la WiFi'>";
  html += "<label>Password:</label><input type='password' name='pass' placeholder='Password'>";
  html += "<button type='submit' class='btn'>💾 Hifadhi na Restart</button>";
  html += "</form><br><a href='/admin'>← Admin</a></body></html>";
  server.send(200, "text/html", html);
}

void saveWifiConfig() {
  String new_ssid = server.arg("ssid");
  String new_pass = server.arg("pass");
  if (new_ssid.length() > 0) {
    prefs.putString("sta_ssid", new_ssid);
    prefs.putString("sta_pass", new_pass);
    server.send(200, "text/html",
      "<html><body style='background:#111;color:lime;font-family:monospace;text-align:center;padding:50px'>"
      "<h1>✅ Imehifadhiwa!</h1><p>ESP32 inarestart...</p></body></html>");
    delay(2000);
    ESP.restart();
  } else {
    server.sendHeader("Location", "/wifi-config");
    server.send(302);
  }
}

// ==================== OTA ====================
void setupOTA() {
  server.on("/update", HTTP_GET, []() {
    server.send(200, "text/html",
      "<!DOCTYPE html><html><head><meta charset='utf-8'><title>OTA</title>"
      "<style>body{background:#111;color:#fff;font-family:monospace;padding:30px;text-align:center}"
      "input,button{padding:12px;margin:10px;border-radius:8px;font-size:15px}"
      "button{background:#e91e63;color:white;border:none;cursor:pointer;width:200px}"
      "a{color:#e91e63}</style></head><body>"
      "<h2>🔄 OTA Firmware Update</h2>"
      "<form method='POST' action='/update' enctype='multipart/form-data'>"
      "<input type='file' name='update' accept='.bin'><br>"
      "<button type='submit'>Upload</button></form>"
      "<br><a href='/admin'>← Admin</a></body></html>");
  });

  server.on("/update", HTTP_POST, []() {
    server.send(200, "text/plain", Update.hasError() ? "FAIL" : "OK");
    delay(500);
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if      (upload.status == UPLOAD_FILE_START)  Update.begin(UPDATE_SIZE_UNKNOWN);
    else if (upload.status == UPLOAD_FILE_WRITE)  Update.write(upload.buf, upload.currentSize);
    else if (upload.status == UPLOAD_FILE_END)    Update.end(true);
  });
}
