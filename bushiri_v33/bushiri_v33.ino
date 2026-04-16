/**
 * PROJECT BUSHIRI v3.6
 * MIXX BY YAS Captive Portal + NAT Router + VPS Verify
 * Bei: TZS 800 = Masaa 15 | Free Trial = Dakika 2
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
const char* OWNER_NAME   = "HAMISI BUSHIRI LUONGO";
const char* STA_SSID_ALT = "PATAHUDUMA";
const char* STA_PASS_ALT = "AMUDUH123";
const char* ADMIN_PASS   = "bushiri2026";
String ownerIP           = "192.168.4.2";
// ======================================================

#define VERSION     "3.6.0"
#define MAX_CLIENTS 20
#define AP_IP_HEX   0xC0A80401UL

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

String sta_ssid    = "";
String sta_pass    = "";
int    clientCount = 0;
bool   natEnabled  = false;
unsigned long lastHB = 0;

// ==================== FORWARD DECLARATIONS ====================
void portalPage();
void paymentPage();
void handleVerify();
void successPage();
void adminPanel();
void wifiConfigPage();
void saveWifiConfig();
void apConfigPage();
void saveApConfig();
void sendErrorPage(String msg);
void captiveRedirect();
void setupWebServer();
void setupOTA();
bool verifyWithVPS(String txid, String ip, String &message);
void enableNAT();
void connectToInternet();
void maintainWiFi();
String getClientIP();
bool checkAdminAuth();
bool hasInternet();

// ==================== INTERNET CHECK ====================
bool hasInternet() {
  WiFiClient client;
  bool ok = client.connect("8.8.8.8", 53);
  client.stop();
  return ok;
}

// ==================== NAT ====================
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
    Serial.println("\n[WiFi] Imeshindwa - jaribu backup...");
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

// ==================== ADMIN AUTH ====================
bool checkAdminAuth() {
  String pass = server.arg("p");
  if (pass != String(ADMIN_PASS)) {
    // Onyesha login form
    String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>Admin Login</title>";
    html += "<style>*{margin:0;padding:0;box-sizing:border-box}";
    html += "body{background:linear-gradient(135deg,#0d1117,#161b22);min-height:100vh;display:flex;align-items:center;justify-content:center;font-family:monospace}";
    html += ".box{background:#161b22;border:1px solid #30363d;border-radius:16px;padding:30px;max-width:320px;width:90%;text-align:center}";
    html += "h2{color:#58a6ff;margin-bottom:20px;font-size:1.3em}";
    html += "input{width:100%;padding:12px;margin:8px 0;background:#0d1117;color:#e6edf3;border:1px solid #30363d;border-radius:8px;font-size:1em}";
    html += ".btn{width:100%;padding:13px;background:#238636;color:white;border:none;border-radius:8px;font-size:1em;font-weight:bold;cursor:pointer;margin-top:10px}";
    html += ".warn{color:#f85149;font-size:.8em;margin-top:10px}</style></head><body>";
    html += "<div class='box'><h2>🔐 BUSHIRI Admin</h2>";
    html += "<form method='GET' action='/admin'>";
    html += "<input type='password' name='p' placeholder='Weka password ya admin' required>";
    html += "<button type='submit' class='btn'>🚀 Ingia</button>";
    html += "</form>";
    if (server.hasArg("p")) html += "<div class='warn'>❌ Password si sahihi</div>";
    html += "</div></body></html>";
    server.send(200, "text/html", html);
    return false;
  }
  return true;
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

  String response = "";
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
  if (deserializeJson(res, response)) {
    message = "VPS ilijibu vibaya";
    return false;
  }

  bool success = res["success"] | false;
  message = res["message"] | String("Hitilafu");

  if (success) {
    addSession(ip, 14UL * 3600000UL);
  }
  return success;
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("================================");
  Serial.println("  BUSHIRI v" VERSION " - APSTA+NAT ");
  Serial.println("================================");

  prefs.begin("bushiri");

  WiFi.mode(WIFI_AP_STA);

  // Sanidi AP
  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

  // Soma SSID na pass ya AP kutoka prefs
  String savedApSSID = prefs.getString("ap_ssid", String(AP_SSID));
  String savedApPass = prefs.getString("ap_pass", String(AP_PASS));
  WiFi.softAP(savedApSSID.c_str(),
              (savedApPass.length() >= 8) ? savedApPass.c_str() : NULL,
              6, 0, 8);
  Serial.println("[AP] Imewashwa: " + savedApSSID);

  // Unganika na internet
  connectToInternet();

  // Washa NAT
  if (WiFi.status() == WL_CONNECTED) {
    delay(1000);
    enableNAT();
  }

  // DNS - captive portal
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
  server.on("/ap-config", HTTP_GET, apConfigPage);
  server.on("/ap-save", HTTP_POST, saveApConfig);

  // Captive portal detection - kila aina ya simu
  server.on("/generate_204", HTTP_GET, []() {
    String ip = server.client().remoteIP().toString();
    if (isAuthorized(ip)) server.send(204, "text/plain", "");
    else { server.sendHeader("Location","http://192.168.4.1/"); server.send(302,"text/plain",""); }
  });
  server.on("/gen_204", HTTP_GET, []() {
    String ip = server.client().remoteIP().toString();
    if (isAuthorized(ip)) server.send(204, "text/plain", "");
    else { server.sendHeader("Location","http://192.168.4.1/"); server.send(302,"text/plain",""); }
  });
  server.on("/hotspot-detect.html", HTTP_GET, []() {
    String ip = server.client().remoteIP().toString();
    if (isAuthorized(ip)) server.send(200,"text/html","<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
    else { server.sendHeader("Location","http://192.168.4.1/"); server.send(302,"text/plain",""); }
  });
  server.on("/library/test/success.html", HTTP_GET, []() {
    String ip = server.client().remoteIP().toString();
    if (isAuthorized(ip)) server.send(200,"text/html","<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
    else { server.sendHeader("Location","http://192.168.4.1/"); server.send(302,"text/plain",""); }
  });
  server.on("/connecttest.txt", HTTP_GET, []() {
    String ip = server.client().remoteIP().toString();
    if (isAuthorized(ip)) server.send(200,"text/plain","Microsoft Connect Test");
    else { server.sendHeader("Location","http://192.168.4.1/"); server.send(302,"text/plain",""); }
  });
  server.on("/ncsi.txt", HTTP_GET, []() {
    String ip = server.client().remoteIP().toString();
    if (isAuthorized(ip)) server.send(200,"text/plain","Microsoft NCSI");
    else { server.sendHeader("Location","http://192.168.4.1/"); server.send(302,"text/plain",""); }
  });
  server.on("/success.txt", HTTP_GET, []() {
    String ip = server.client().remoteIP().toString();
    if (isAuthorized(ip)) server.send(200,"text/plain","success");
    else { server.sendHeader("Location","http://192.168.4.1/"); server.send(302,"text/plain",""); }
  });
  server.on("/kindle-wifi/wifistub.html", HTTP_GET, []() {
    String ip = server.client().remoteIP().toString();
    if (isAuthorized(ip)) server.send(200,"text/html","");
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
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.send(302, "text/plain", "Redirecting...");
}

// ==================== PORTAL PAGE ====================
void portalPage() {
  String ip = getClientIP();
  if (isAuthorized(ip)) {
    server.sendHeader("Location", "http://google.com");
    server.send(302); return;
  }

  String html = R"(<!DOCTYPE html><html><head>
<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>BUSHIRI HOTSPOT</title>
<style>
*{margin:0;padding:0;box-sizing:border-box;font-family:'Segoe UI',sans-serif}
body{background:linear-gradient(135deg,#0a0a1a,#1a1035,#0d2040);min-height:100vh;display:flex;align-items:center;justify-content:center;padding:15px}
.card{max-width:390px;width:100%;background:white;border-radius:24px;overflow:hidden;box-shadow:0 30px 60px rgba(0,0,0,0.5)}
.header{background:linear-gradient(135deg,#e91e63,#9c27b0,#3f51b5);color:white;padding:28px 25px;text-align:center;position:relative}
.wifi-icon{font-size:3.5em;margin-bottom:6px;animation:pulse 2s infinite}
@keyframes pulse{0%,100%{transform:scale(1)}50%{transform:scale(1.1)}}
.brand{font-size:1.6em;font-weight:800;letter-spacing:3px;text-shadow:0 2px 4px rgba(0,0,0,0.3)}
.tagline{font-size:.82em;opacity:.85;margin-top:5px}
.body{padding:20px}
.owner-box{background:linear-gradient(135deg,#fff8e1,#ffecb3);border-radius:12px;padding:12px;text-align:center;margin-bottom:14px;border:2px solid #ffc107}
.owner-label{font-size:.72em;color:#e65100;font-weight:700;text-transform:uppercase;letter-spacing:1px}
.owner-name{font-size:1em;font-weight:800;color:#bf360c;margin-top:3px}
.price-box{background:linear-gradient(135deg,#fce4ec,#f8bbd9);border-radius:12px;padding:14px;text-align:center;margin-bottom:14px;border:2px solid #e91e63}
.price{font-size:2.4em;font-weight:900;color:#c2185b}
.price-label{color:#880e4f;font-size:.9em;font-weight:700;margin-top:2px}
.price-sub{color:#ad1457;font-size:.76em;margin-top:3px;opacity:.85}
.steps-title{font-size:.78em;font-weight:700;color:#888;text-transform:uppercase;letter-spacing:1px;margin-bottom:8px;text-align:center}
.step{display:flex;align-items:flex-start;padding:8px 0;border-bottom:1px solid #f0f0f0;font-size:.86em;color:#444;line-height:1.4}
.step:last-of-type{border-bottom:none}
.step-num{background:linear-gradient(135deg,#e91e63,#9c27b0);color:white;border-radius:50%;width:26px;height:26px;min-width:26px;display:flex;align-items:center;justify-content:center;font-weight:bold;font-size:.78em;margin-right:10px;margin-top:1px}
.mixx-box{background:linear-gradient(135deg,#e8f5e9,#c8e6c9);border-radius:10px;padding:12px;text-align:center;margin:8px 0;border:2px solid #4caf50}
.mixx-label{font-size:.72em;color:#1b5e20;font-weight:700;text-transform:uppercase;letter-spacing:.5px}
.mixx-num{font-size:1.5em;font-weight:900;color:#2e7d32;letter-spacing:2px;margin-top:3px}
.mixx-name{font-size:.78em;color:#388e3c;font-weight:600;margin-top:2px}
.btn{display:block;width:100%;padding:16px;background:linear-gradient(135deg,#e91e63,#9c27b0);color:white;border:none;border-radius:14px;font-size:1.1em;font-weight:800;text-align:center;text-decoration:none;margin-top:14px;box-shadow:0 4px 15px rgba(233,30,99,0.4);transition:transform .1s}
.btn:active{transform:scale(.98)}
.trial-btn{display:block;width:100%;padding:11px;background:linear-gradient(135deg,#00bcd4,#0097a7);color:white;border:none;border-radius:12px;font-size:.9em;font-weight:700;text-align:center;text-decoration:none;margin-top:8px;box-shadow:0 3px 10px rgba(0,188,212,0.3)}
.note{font-size:.72em;color:#aaa;text-align:center;margin-top:10px;line-height:1.5}
.badge{display:inline-block;background:#e91e63;color:white;font-size:.65em;padding:2px 8px;border-radius:20px;margin-left:5px;vertical-align:middle}
</style></head><body>
<div class='card'>
  <div class='header'>
    <div class='wifi-icon'>📶</div>
    <div class='brand'>)" + String(PORTAL_TITLE) + R"(</div>
    <div class='tagline'>🌍 Internet ya haraka · Lipa kidogo · Pumzika siku nzima</div>
  </div>
  <div class='body'>
    <div class='owner-box'>
      <div class='owner-label'>⚡ Mtoa Huduma</div>
      <div class='owner-name'>👤 )" + String(OWNER_NAME) + R"(</div>
    </div>
    <div class='price-box'>
      <div class='price'>TZS 800</div>
      <div class='price-label'>🌐 Masaa 15 ya Internet <span class='badge'>POPULAR</span></div>
      <div class='price-sub'>✨ Unganika asubuhi — toka usiku bila wasiwasi</div>
    </div>
    <div class='steps-title'>📋 Hatua za Kulipa na Kuingia</div>
    <div class='step'><div class='step-num'>1</div><span>Fungua <b>MIXX BY YAS</b> kwenye simu yako — nenda sehemu ya <b>Tuma Pesa</b></span></div>
    <div class='step'><div class='step-num'>2</div><span>Tuma <b>TZS 800</b> kwenda namba hii:</span></div>
    <div class='mixx-box'>
      <div class='mixx-label'>📲 MIXX BY YAS — Namba ya Kulipa</div>
      <div class='mixx-num'>)" + String(MIXX_NUMBER) + R"(</div>
      <div class='mixx-name'>👤 )" + String(OWNER_NAME) + R"(</div>
    </div>
    <div class='step'><div class='step-num'>3</div><span>Angalia SMS utakayopata — namba ya <b>Kumbukumbu</b> itaonekana hapo</span></div>
    <div class='step'><div class='step-num'>4</div><span>Bonyeza kitufe cha <b>Nimelipa</b> hapa chini — weka namba ya kumbukumbu — uingie!</span></div>
    <a href='/pay' class='btn'>✅ Nimelipa — Ingia Sasa</a>
    <a href='/pay?trial=1' class='trial-btn'>🎁 Jaribu Bure Dakika 2 (BUSHIRIPROJECT)</a>
    <div class='note'>⚠️ Malipo ya MIXX BY YAS tu · Piga *150*01# au tumia App · Hakuna tozo ya kutuma</div>
  </div>
</div></body></html>)";

  server.send(200, "text/html", html);
}

// ==================== PAYMENT PAGE ====================
void paymentPage() {
  bool isTrial = server.arg("trial") == "1";

  String html = R"(<!DOCTYPE html><html><head>
<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Thibitisha Malipo</title>
<style>
*{margin:0;padding:0;box-sizing:border-box;font-family:'Segoe UI',sans-serif}
body{background:linear-gradient(135deg,#1a1a2e,#16213e,#0f3460);min-height:100vh;display:flex;align-items:center;justify-content:center;padding:15px}
.card{max-width:380px;width:100%;background:white;border-radius:20px;overflow:hidden;box-shadow:0 25px 50px rgba(0,0,0,0.4)}
.header{background:linear-gradient(135deg,#2196F3,#1565C0,#0d47a1);color:white;padding:25px;text-align:center}
.header h2{font-size:1.3em;margin-bottom:5px}
.header p{font-size:.85em;opacity:.9}
.body{padding:22px}
.info-box{background:#e3f2fd;border-radius:10px;padding:14px;margin-bottom:18px;font-size:.84em;color:#1565C0;line-height:1.7}
.trial-box{background:linear-gradient(135deg,#e0f7fa,#b2ebf2);border-radius:10px;padding:14px;margin-bottom:18px;font-size:.84em;color:#006064;line-height:1.7;border:2px solid #00bcd4}
label{display:block;font-weight:600;color:#333;margin-bottom:5px;font-size:.88em}
input{width:100%;padding:13px;border:2px solid #ddd;border-radius:10px;font-size:1em;margin-bottom:13px;box-sizing:border-box;transition:border-color .2s}
input:focus{outline:none;border-color:#2196F3}
.btn{width:100%;padding:15px;background:linear-gradient(135deg,#e91e63,#c2185b);color:white;border:none;border-radius:12px;font-size:1.1em;font-weight:700;cursor:pointer}
.back{display:block;text-align:center;margin-top:12px;color:#666;font-size:.84em;text-decoration:none}
#loading{display:none;text-align:center;padding:10px;color:#666;font-size:.9em}
</style></head><body>
<div class='card'>
  <div class='header'>
    <h2>)" + String(isTrial ? "🎁 Jaribu Bure" : "✅ Thibitisha Malipo") + R"(</h2>
    <p>)" + String(isTrial ? "Weka BUSHIRIPROJECT kupata dakika 2 za bure" : "Weka namba ya kumbukumbu ya MIXX BY YAS") + R"(</p>
  </div>
  <div class='body'>)";

  if (isTrial) {
    html += R"(<div class='trial-box'>🎁 <b>Ofa ya Majaribio!</b><br>Weka <b>BUSHIRIPROJECT</b> kwenye sehemu ya TXID hapa chini kupata dakika 2 za internet <b>BILA MALIPO</b>.<br><br>⚠️ Kila simu inaweza kujaribu mara moja tu.</div>)";
  } else {
    html += R"(<div class='info-box'>📩 Baada ya kutuma pesa utapata SMS kutoka <b>MIXX BY YAS</b>.<br>SMS hiyo ina <b>Kumbukumbu no.</b> — hiyo ndiyo TXID.<br><br>Mfano: <b>26205921931320</b></div>)";
  }

  html += R"(
    <form method='POST' action='/verify' onsubmit='showLoading()'>
      <label>Namba ya Kumbukumbu (TXID):</label>
      <input type='text' name='txid' placeholder=')" + String(isTrial ? "BUSHIRIPROJECT" : "Mfano: 26205921931320") + R"(' required maxlength='20' autocomplete='off'>
      <label>Namba yako ya Simu:</label>
      <input type='tel' name='phone' placeholder='0717XXXXXX' required maxlength='13'>
      <button type='submit' class='btn'>🚀 )" + String(isTrial ? "Pata Trial Sasa" : "Ingia Sasa") + R"(</button>
    </form>
    <div id='loading'>⏳ Inathibitisha... Subiri sekunde chache...</div>
    <a href='/' class='back'>← Rudi Nyumbani</a>
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

  // Free trial - dakika 2 tu
  if (txid == "BUSHIRIPROJECT") {
    addSession(ip, 2UL * 60000UL);
    server.sendHeader("Location", "/success?phone=" + phone + "&trial=1");
    server.send(302); return;
  }

  if (txid.length() < 6) {
    sendErrorPage("❌ TXID ni fupi mno.\n\nAngalia SMS yako na ujaribu tena.");
    return;
  }

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
body{background:linear-gradient(135deg,#00c853,#1b5e20,#33691e);min-height:100vh;display:flex;align-items:center;justify-content:center;color:white;text-align:center;padding:20px}
.card{max-width:350px;width:100%}
.icon{font-size:5em;margin-bottom:15px;animation:bounce .6s infinite alternate}
@keyframes bounce{from{transform:translateY(0)}to{transform:translateY(-10px)}}
h1{font-size:1.8em;margin-bottom:8px}
p{opacity:.9;margin:6px 0;font-size:.95em}
.info{background:rgba(255,255,255,.2);border-radius:14px;padding:16px;margin:18px 0}
.info-item{display:flex;justify-content:space-between;padding:6px 0;font-size:.9em;border-bottom:1px solid rgba(255,255,255,.1)}
.info-item:last-child{border-bottom:none}
.btn{display:inline-block;margin-top:16px;padding:14px 30px;background:white;color:#00c853;border-radius:12px;font-weight:800;text-decoration:none;font-size:1em}
</style></head><body>
<div class='card'>
  <div class='icon'>🎉</div>
  <h1>Hongera!</h1>
  <p>)" + String(isTrial ? "Trial yako ya dakika 2 imeanza!" : "Internet imewashwa kwa mafanikio!") + R"(</p>
  <div class='info'>
    <div class='info-item'><span>📱 Simu</span><span>)" + phone.substring(0,4) + R"(******</span></div>
    <div class='info-item'><span>⏰ Muda</span><span>)" + String(isTrial ? "Dakika 2 (Trial)" : "Masaa 15") + R"(</span></div>
    <div class='info-item'><span>🌐 Mtandao</span><span>Bushiri WiFi</span></div>
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
body{background:linear-gradient(135deg,#b71c1c,#c62828,#d32f2f);min-height:100vh;display:flex;align-items:center;justify-content:center;color:white;text-align:center;padding:20px}
.card{max-width:350px;width:100%}
.icon{font-size:4em;margin-bottom:12px}
h2{font-size:1.5em;margin-bottom:12px}
.msg{background:rgba(255,255,255,.2);border-radius:12px;padding:15px;margin:14px 0;font-size:.92em;line-height:1.6;white-space:pre-line}
.btn{display:inline-block;margin:6px;padding:12px 22px;background:white;color:#b71c1c;border-radius:10px;font-weight:700;text-decoration:none}
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
  if (!checkAdminAuth()) return;

  String inet = (WiFi.status()==WL_CONNECTED)
    ? "🟢 "+WiFi.SSID()+" ("+WiFi.localIP().toString()+")"
    : "🔴 Haijaunganika";

  String savedApSSID = prefs.getString("ap_ssid", String(AP_SSID));
  String p = server.arg("p");

  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Admin - BUSHIRI</title>";
  html += "<style>";
  html += "*{margin:0;padding:0;box-sizing:border-box}";
  html += "body{background:linear-gradient(135deg,#0d1117,#161b22);min-height:100vh;font-family:monospace;padding:20px;color:#e6edf3}";
  html += "h1{color:#58a6ff;margin-bottom:5px;font-size:1.3em}";
  html += ".ver{color:#8b949e;font-size:.75em;margin-bottom:15px}";
  html += "h3{color:#3fb950;margin:18px 0 8px;font-size:.95em;text-transform:uppercase;letter-spacing:1px}";
  html += ".box{background:#161b22;border:1px solid #30363d;border-radius:10px;padding:14px;margin:8px 0}";
  html += ".stat{display:flex;justify-content:space-between;padding:5px 0;border-bottom:1px solid #21262d;font-size:.85em}";
  html += ".stat:last-child{border-bottom:none}";
  html += ".label{color:#8b949e}.val{color:#e6edf3;font-weight:600}";
  html += ".green{color:#3fb950}.red{color:#f85149}.blue{color:#58a6ff}";
  html += ".ip{font-size:.78em;color:#8b949e;padding:3px 0;border-bottom:1px solid #21262d}";
  html += ".ip:last-child{border-bottom:none}";
  html += ".btn-row{display:flex;flex-wrap:wrap;gap:8px;margin-top:10px}";
  html += ".btn{padding:10px 16px;border-radius:8px;font-size:.82em;font-weight:700;text-decoration:none;text-align:center}";
  html += ".btn-blue{background:#1f6feb;color:white}";
  html += ".btn-green{background:#238636;color:white}";
  html += ".btn-orange{background:#d1811f;color:white}";
  html += ".btn-red{background:#b91c1c;color:white}";
  html += ".divider{border:none;border-top:1px solid #30363d;margin:15px 0}";
  html += "</style></head><body>";

  html += "<h1>🛜 BUSHIRI Admin Panel</h1>";
  html += "<div class='ver'>v" VERSION " · 192.168.4.1</div>";

  // Status
  html += "<h3>📊 Hali ya Mfumo</h3><div class='box'>";
  html += "<div class='stat'><span class='label'>Internet</span><span class='val'>" + inet + "</span></div>";
  html += "<div class='stat'><span class='label'>NAT Router</span><span class='val " + String(natEnabled?"green":"red") + "'>" + String(natEnabled?"🟢 ON":"🔴 OFF") + "</span></div>";
  html += "<div class='stat'><span class='label'>WiFi AP</span><span class='val blue'>" + savedApSSID + "</span></div>";
  html += "<div class='stat'><span class='label'>Wateja Sasa</span><span class='val'>" + String(clientCount) + "</span></div>";
  html += "<div class='stat'><span class='label'>Sessions Aktif</span><span class='val green'>" + String(sessionCount) + "</span></div>";
  html += "<div class='stat'><span class='label'>Bei</span><span class='val'>TZS 800 / Masaa 15</span></div>";
  html += "</div>";

  // Sessions
  html += "<h3>👥 Sessions Zilizo Aktif</h3><div class='box'>";
  int active = 0;
  for (int i = 0; i < sessionCount; i++) {
    if (sessions[i].active && millis() < sessions[i].expiry) {
      unsigned long mins = (sessions[i].expiry - millis()) / 60000;
      html += "<div class='ip'>📱 " + sessions[i].ip + " &nbsp;|&nbsp; ⏱ Dakika " + String(mins) + " zimebaki</div>";
      active++;
    }
  }
  if (!active) html += "<div class='ip' style='color:#8b949e'>Hakuna sessions za sasa</div>";
  html += "</div>";

  // Viungo
  html += "<h3>⚙️ Mipangilio</h3>";
  html += "<div class='btn-row'>";
  html += "<a href='/wifi-config?p=" + p + "' class='btn btn-blue'>📡 WiFi ya Modem</a>";
  html += "<a href='/ap-config?p=" + p + "' class='btn btn-green'>📶 Badilisha SSID/Pass ya AP</a>";
  html += "<a href='/update' class='btn btn-orange'>🔄 OTA Update</a>";
  html += "<a href='/' class='btn btn-red'>🏠 Portal</a>";
  html += "</div>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

// ==================== WIFI CONFIG ====================
void wifiConfigPage() {
  if (!checkAdminAuth()) return;
  String p = server.arg("p");

  String status = (WiFi.status()==WL_CONNECTED)
    ? "🟢 "+WiFi.SSID()+" | "+WiFi.localIP().toString()
    : "🔴 Haijaunganika";

  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'><title>WiFi Config</title>";
  html += "<style>*{margin:0;padding:0;box-sizing:border-box}body{background:#0d1117;color:#e6edf3;font-family:monospace;padding:20px}";
  html += "h2{color:#58a6ff;margin-bottom:15px}";
  html += "input{width:100%;padding:12px;margin:6px 0 14px;background:#161b22;color:#e6edf3;border:1px solid #30363d;border-radius:8px;font-size:15px;box-sizing:border-box}";
  html += ".btn{width:100%;padding:14px;background:#238636;color:white;border:none;border-radius:8px;font-size:1em;cursor:pointer;font-weight:bold}";
  html += ".status{padding:12px;background:#161b22;border-left:4px solid #58a6ff;margin:12px 0;border-radius:4px;font-size:.85em}";
  html += "a{color:#58a6ff;font-size:.85em}label{color:#8b949e;font-size:.85em}</style></head><body>";
  html += "<h2>📡 WiFi ya Modem (Internet)</h2>";
  html += "<div class='status'>" + status + "</div>";
  html += "<form method='POST' action='/wifi-save'>";
  html += "<input type='hidden' name='p' value='" + p + "'>";
  html += "<label>SSID (Jina la Modem/Hotspot):</label>";
  html += "<input type='text' name='ssid' value='" + sta_ssid + "' placeholder='Jina la WiFi'>";
  html += "<label>Password:</label>";
  html += "<input type='password' name='pass' placeholder='Password (acha wazi kama open)'>";
  html += "<button type='submit' class='btn'>💾 Hifadhi na Restart</button>";
  html += "</form><br><a href='/admin?p=" + p + "'>← Rudi Admin</a></body></html>";
  server.send(200, "text/html", html);
}

void saveWifiConfig() {
  String new_ssid = server.arg("ssid");
  String new_pass = server.arg("pass");
  if (new_ssid.length() > 0) {
    prefs.putString("sta_ssid", new_ssid);
    prefs.putString("sta_pass", new_pass);
    server.send(200, "text/html",
      "<html><body style='background:#0d1117;color:#3fb950;font-family:monospace;text-align:center;padding:50px'>"
      "<h1>✅ Imehifadhiwa!</h1><p>ESP32 inarestart...</p></body></html>");
    delay(2000);
    ESP.restart();
  } else {
    server.sendHeader("Location", "/wifi-config");
    server.send(302);
  }
}

// ==================== AP CONFIG (Badilisha SSID/Password ya AP) ====================
void apConfigPage() {
  if (!checkAdminAuth()) return;
  String p = server.arg("p");
  String currentSSID = prefs.getString("ap_ssid", String(AP_SSID));

  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'><title>AP Config</title>";
  html += "<style>*{margin:0;padding:0;box-sizing:border-box}body{background:#0d1117;color:#e6edf3;font-family:monospace;padding:20px}";
  html += "h2{color:#3fb950;margin-bottom:15px}";
  html += "input{width:100%;padding:12px;margin:6px 0 14px;background:#161b22;color:#e6edf3;border:1px solid #30363d;border-radius:8px;font-size:15px;box-sizing:border-box}";
  html += ".btn{width:100%;padding:14px;background:#e91e63;color:white;border:none;border-radius:8px;font-size:1em;cursor:pointer;font-weight:bold}";
  html += ".warn{background:#161b22;border-left:4px solid #f85149;padding:12px;margin:12px 0;border-radius:4px;font-size:.82em;color:#f85149}";
  html += "a{color:#58a6ff;font-size:.85em}label{color:#8b949e;font-size:.85em}</style></head><body>";
  html += "<h2>📶 Badilisha Jina la WiFi (AP)</h2>";
  html += "<div class='warn'>⚠️ Baada ya kubadilisha - ESP32 itaanza upya. Utahitaji kuunganika tena na SSID mpya.</div>";
  html += "<form method='POST' action='/ap-save'>";
  html += "<input type='hidden' name='p' value='" + p + "'>";
  html += "<label>Jina Jipya la WiFi (SSID):</label>";
  html += "<input type='text' name='ssid' value='" + currentSSID + "' placeholder='Jina la WiFi' maxlength='32' required>";
  html += "<label>Password Mpya (acha wazi = open, au angalau herufi 8):</label>";
  html += "<input type='password' name='pass' placeholder='Password (optional)' maxlength='63'>";
  html += "<button type='submit' class='btn'>💾 Hifadhi na Restart</button>";
  html += "</form><br><a href='/admin?p=" + p + "'>← Rudi Admin</a></body></html>";
  server.send(200, "text/html", html);
}

void saveApConfig() {
  String new_ssid = server.arg("ssid");
  String new_pass = server.arg("pass");
  if (new_ssid.length() > 0) {
    prefs.putString("ap_ssid", new_ssid);
    prefs.putString("ap_pass", new_pass);
    server.send(200, "text/html",
      "<html><body style='background:#0d1117;color:#3fb950;font-family:monospace;text-align:center;padding:50px'>"
      "<h1>✅ Imehifadhiwa!</h1>"
      "<p>SSID mpya: <b>" + new_ssid + "</b></p>"
      "<p>ESP32 inarestart - unganika na SSID mpya!</p></body></html>");
    delay(2000);
    ESP.restart();
  } else {
    server.sendHeader("Location", "/ap-config");
    server.send(302);
  }
}

// ==================== OTA ====================
void setupOTA() {
  server.on("/update", HTTP_GET, []() {
    server.send(200, "text/html",
      "<!DOCTYPE html><html><head><meta charset='utf-8'><title>OTA</title>"
      "<style>body{background:#0d1117;color:#e6edf3;font-family:monospace;padding:30px;text-align:center}"
      "input,button{padding:12px;margin:10px;border-radius:8px;font-size:15px}"
      "button{background:#e91e63;color:white;border:none;cursor:pointer;width:220px}"
      "a{color:#58a6ff}</style></head><body>"
      "<h2>🔄 OTA Firmware Update</h2>"
      "<form method='POST' action='/update' enctype='multipart/form-data'>"
      "<input type='file' name='update' accept='.bin'><br>"
      "<button type='submit'>⬆️ Upload Firmware</button></form>"
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
