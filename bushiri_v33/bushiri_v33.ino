#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <Preferences.h>
#include <ArduinoJson.h>

// ==================== CONFIG ====================
const char* AP_SSID = "Bushiri PROJECT";
const char* AP_PASS = "";
const char* VPS_HOST = "bushiri-project.onrender.com";
const int VPS_PORT = 443;
const char* VPS_TOKEN = "bushiri2026";
String authorizedMACs[10] = {"bc:90:63:a2:32:83"};
const char* PORTAL_TITLE = "BUSHIRI PROJECT";
const char* MIX_NUMBER = "0717633805";
const String MODEM_SSID = "PATAHUDUMA";
const String MODEM_PASS = "AMUDUH123";
const String TEST_CODE = "TEST123";

// ==================== SERVERS ====================
WebServer server(80);
DNSServer dnsServer;
WiFiClientSecure httpsClient;
WiFiServer natServer(8080); // NAT proxy server

// ==================== STATE ====================
IPAddress AP_IP(192, 168, 4, 1);
bool internetConnected = false;
int sessionCount = 0;

struct ClientSession {
  String mac;
  String ip;
  bool authorized;
  unsigned long expiry;
};
ClientSession sessions[50];

// ==================== HTML (same as before) ====================
const char index_html[] PROGMEM = R"=====(
<!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>BUSHIRI PROJECT - Fast Internet</title><style>*{margin:0;padding:0;box-sizing:border-box;font-family:system-ui}body{background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px}.container{max-width:400px;width:100%;background:white;border-radius:25px;box-shadow:0 25px 50px rgba(0,0,0,0.15)}.header{background:linear-gradient(135deg,#ff6b6b,#feca57);padding:35px 25px;text-align:center;color:white}.header h1{font-size:26px;margin-bottom:8px}.form-group{padding:30px}.input-group input{width:100%;padding:18px;border:2px solid #e9ecef;border-radius:15px;font-size:17px}.btn{width:100%;padding:18px;background:linear-gradient(135deg,#667eea,#764ba2);color:white;border:none;border-radius:15px;font-size:18px;cursor:pointer}.payment-info{background:#d4edda;padding:25px;border-radius:15px;margin:25px 0}.test-mode{background:#fff3cd;color:#856404;padding:20px;border-radius:15px;margin:25px 0;text-align:center;font-weight:600}</style></head><body><div class="container"><div class="header"><h1>🌐 BUSHIRI PROJECT</h1><p>High Speed Internet</p></div><div class="form-group"><div class="test-mode">🔥 TEST123 = 2min FREE!</div><input type="text" id="txid" placeholder="TXID or TEST123" style="margin-bottom:20px"><button class="btn" onclick="connect()">🚀 CONNECT</button><div class="payment-info"><h3>💳 TZS 800 → 0717633805<br>MIXX BY YAS • HAMISI BUSHIRI</h3></div></div></div><script>function connect(){let txid=document.getElementById('txid').value.toUpperCase();if(txid==='TEST123'){window.location='http://192.168.4.1/proxy';return;}fetch('/validate?txid='+txid).then(r=>r.json()).then(d=>{if(d.ok)window.location='http://192.168.4.1/proxy';else alert('Invalid TXID')});}</script></body></html>
)=====";

void setup() {
  Serial.begin(115200);
  
  // AP Setup
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255,255,255,0));
  WiFi.softAP(AP_SSID, AP_PASS);
  
  // DNS Captive
  dnsServer.start(53, "*", AP_IP);
  
  // Routes
  server.on("/", [](){server.send_P(200,"text/html",index_html);});
  server.on("/validate", handleValidate);
  server.on("/proxy", handleProxy);
  server.on("/admin", handleAdmin);
  server.begin();
  
  // NAT Server
  natServer.begin();
  
  // Connect Modem
  WiFi.begin(MODEM_SSID.c_str(), MODEM_PASS.c_str());
  Serial.println("Connecting modem...");
  
  delay(5000);
  internetConnected = WiFi.status() == WL_CONNECTED;
  Serial.println(internetConnected ? "Modem OK" : "Modem FAIL");
  Serial.println("Bushiri PROJECT ready!");
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  
  // NAT Proxy
  WiFiClient client = natServer.available();
  if (client) {
    handleNATClient(client);
  }
  
  // Reconnect modem if needed
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 30000) {
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.reconnect();
    }
    lastCheck = millis();
  }
}

void handleValidate() {
  String txid = server.arg("txid");
  String ip = server.client().remoteIP().toString();
  
  // TEST123 or Owner
  if (txid == TEST_CODE || ip == "192.168.4.100") { // Demo owner IP
    addSession(ip, 120000); // 2min
    server.send(200,"application/json","{\"ok\":true}");
    return;
  }
  
  // VPS Check (demo always true)
  addSession(ip, 86400000); // 24h
  server.send(200,"application/json","{\"ok\":true}");
}

void addSession(String ip, unsigned long duration) {
  for (int i = 0; i < sessionCount; i++) {
    if (sessions[i].ip == ip) return;
  }
  if (sessionCount < 50) {
    sessions[sessionCount].ip = ip;
    sessions[sessionCount].authorized = true;
    sessions[sessionCount].expiry = millis() + duration;
    sessionCount++;
  }
}

bool isAuthorized(String ip) {
  for (int i = 0; i < sessionCount; i++) {
    if (sessions[i].ip == ip && sessions[i].authorized && 
        millis() < sessions[i].expiry) {
      return true;
    }
  }
  return false;
}

void handleProxy() {
  String target = server.arg("url");
  if (!isAuthorized(server.client().remoteIP().toString())) {
    server.send(302, "text/plain", "/");
    return;
  }
  
  // Proxy to internet
  HTTPClient http;
  http.begin(target);
  int code = http.GET();
  String payload = http.getString();
  http.end();
  
  server.send(code, "text/html", payload);
}

void handleNATClient(WiFiClient client) {
  if (!client.connected()) return;
  
  String clientIP = client.remoteIP().toString();
  if (!isAuthorized(clientIP)) {
    client.stop();
    return;
  }
  
  // Simple HTTP proxy
  WiFiClient internet;
  if (internet.connect(WiFi.gatewayIP(), 80)) {
    // Forward request
    while (client.available()) {
      internet.write(client.read());
    }
    // Forward response  
    while (internet.available()) {
      client.write(internet.read());
    }
  }
  client.stop();
  internet.stop();
}

void handleAdmin() {
  if (server.arg("pass") != "bushiri2026") {
    server.send(401);
    return;
  }
  
  String html = "<h1>Bushiri Admin</h1>";
  html += "<p>Internet: " + String(internetConnected ? "ON" : "OFF") + "</p>";
  html += "<p>Sessions: " + String(sessionCount) + "</p>";
  html += "<p>Modem IP: " + WiFi.localIP().toString() + "</p>";
  server.send(200, "text/html", html);
}