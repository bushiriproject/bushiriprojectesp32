#include <WiFi.h>
#include <WiFiAP.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// Bushiri PROJECT - ESP32 Captive Portal with M-PESA & NAT
// Platform: ESP32 3.3.8 - Fixed compilation errors

// Configuration
const char* AP_SSID = "Bushiri-WiFi";
const char* AP_PASS = "12345678";
const char* VPS_URL = "https://your-vps.com/report";
const int AP_CHANNEL = 6;
const int MAX_CLIENTS = 8;
const int NAT_PORT = 8080;
const unsigned long SESSION_TIMEOUT = 3600000UL; // 1 hour
const unsigned long CHECK_INTERVAL = 30000UL;    // 30 seconds
const char* MODEM_APN = "internet";

// Globals
WebServer server(80);
WebServer natServer(NAT_PORT);
Preferences prefs;
bool modemConnected = false;
uint32_t activeSessions = 0;
unsigned long lastCheck = 0;

// Session structure
struct ClientSession {
  uint8_t mac[6];
  char phone[16];
  char mpesaTxId[32];
  unsigned long startTime;
  bool authorized;
};

ClientSession sessions[MAX_CLIENTS];
int sessionCount = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Bushiri PROJECT Starting...");
  
  prefs.begin("bushiri", false);
  
  // Configure AP
  WiFi.mode(WIFI_AP_STA);
  IPAddress apIP(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  
  WiFi.softAPConfig(apIP, gateway, subnet);
  WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL, 0, MAX_CLIENTS);
  
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
  
  // Web server routes
  server.on("/", handlePortal);
  server.on("/admin", handleAdmin);
  server.on("/mpesa-validate", HTTP_POST, handleMpesaValidate);
  server.on("/generate_204", handlePortal);
  server.on("/fwlink", handlePortal);
  server.onNotFound(handlePortal);
  server.begin();
  
  // NAT server
  natServer.onNotFound(handleNATForward);
  natServer.begin();
  
  connectModem();
  Serial.println("Bushiri PROJECT Ready!");
}

void loop() {
  server.handleClient();
  natServer.handleClient();
  
  unsigned long now = millis();
  if (now - lastCheck > CHECK_INTERVAL) {
    checkExpiredSessions();
    reportToVPS();
    lastCheck = now;
  }
  
  if (!modemConnected) {
    connectModem();
  }
  
  delay(10);
}

void handlePortal() {
  // Check authorization (simplified)
  bool authorized = false;
  for (int i = 0; i < sessionCount; i++) {
    if (sessions[i].authorized) {
      authorized = true;
      break;
    }
  }
  
  if (authorized) {
    String clientIP = server.client().remoteIP().toString();
    server.sendHeader("Location", String("http://") + clientIP + ":8080/", true);
    server.send(302, "text/html", "");
    return;
  }
  
  // Captive portal HTML (fixed raw string literals)
  String html = "<!DOCTYPE html><html><head><title>Bushiri WiFi</title>";
  html += "<meta name=viewport content=width=device-width,initial-scale=1>";
  html += "<style>body{font-family:Arial;max-width:400px;margin:50px auto;padding:20px;text-align:center;}";
  html += ".pay-button{background:#25D366;color:white;padding:15px;border:none;border-radius:10px;font-size:18px;width:100%;margin:10px 0;}";
  html += ".status{padding:10px;margin:10px 0;border-radius:5px;}.success{background:#d4edda;color:#155724;}.error{background:#f8d7da;color:#721c24;}</style>";
  html += "</head><body><h1>&#x1F30E; Bushiri WiFi</h1>";
  html += "<p>Pay <strong>KSh 50</strong> for 1 hour unlimited access</p>";
  html += "<button class=pay-button onclick=payMpesa()>Pay with M-PESA</button>";
  html += "<div id=status></div>";
  html += "<script>";
  html += "function payMpesa(){";
  html += "if(confirm('Send KSh 50 to 123456?\\nPhone: '+getPhone())){";
  html += "fetch('/mpesa-validate',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({phone:getPhone()})})";
  html += ".then(r=>r.json()).then(data=>{";
  html += "if(data.success){document.getElementById('status').innerHTML='<div class=status success>&#x2705; Payment verified! Redirecting...</div>';";
  html += "setTimeout(()=>window.location.href='http://192.168.4.1:8080/',2000);}else{";
  html += "document.getElementById('status').innerHTML='<div class=status error>&#x274C; '+data.error+'</div>';}});}}";
  html += "function getPhone(){let phone=prompt('Enter M-PESA phone:');return phone?phone:'';}";
  html += "</script></body></html>";
  
  server.send(200, "text/html", html);
}

void handleAdmin() {
  String html = "<!DOCTYPE html><html><head><title>Bushiri Admin</title>";
  html += "<meta name=viewport content=width=device-width,initial-scale=1></head>";
  html += "<body><h1>Bushiri Admin</h1>";
  html += "<p>Active sessions: " + String(activeSessions) + "</p>";
  html += "<p>Modem: " + String(modemConnected ? "Connected" : "Disconnected") + "</p>";
  html += "<h3>Sessions:</h3><ul>";
  
  for (int i = 0; i < sessionCount; i++) {
    html += "<li>" + String(sessions[i].phone) + " - " + 
            String(sessions[i].authorized ? "Active" : "Pending") + "</li>";
  }
  
  html += "</ul><button onclick=location.reload()>Refresh</button></body></html>";
  server.send(200, "text/html", html);
}

void handleMpesaValidate() {
  if (server.hasArg("plain") == false) {
    server.send(400);
    return;
  }
  
  String body = server.arg("plain");
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, body);
  
  if (error) {
    server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
    return;
  }
  
  const char* phone = doc["phone"];
  bool valid = mockMpesaValidation(phone);
  
  DynamicJsonDocument response(512);
  response["success"] = valid;
  response["phone"] = phone;
  if (!valid) {
    response["error"] = "Invalid payment";
  }
  
  String json;
  serializeJson(response, json);
  server.send(200, "application/json", json);
  
  if (valid) {
    authorizeClient((uint8_t*)phone);
  }
}

void handleNATForward() {
  if (!modemConnected) {
    natServer.send(503, "text/plain", "No internet");
    return;
  }
  
  // Simplified auth check
  bool authorized = false;
  for (int i = 0; i < sessionCount; i++) {
    if (sessions[i].authorized) {
      authorized = true;
      break;
    }
  }
  
  if (!authorized) {
    natServer.send(403, "text/plain", "Access denied");
    return;
  }
  
  // Forward through modem (simplified HTTP proxy)
  HTTPClient http;
  http.begin("http://" + natServer.hostHeader() + natServer.uri());
  int httpCode = http.GET();
  
  if (httpCode > 0) {
    String payload = http.getString();
    natServer.sendHeader("Content-Type", "text/html");
    natServer.send(httpCode, "text/html", payload);
  } else {
    natServer.send(502, "text/plain", "Forward failed");
  }
  http.end();
}

void connectModem() {
  Serial.println("Connecting modem...");
  // SIM800/SIM7600 AT commands
  modemConnected = true; // Mock for compilation
  Serial.println("Modem ready");
}

bool mockMpesaValidation(const char* phone) {
  static int count = 0;
  if (count++ < 5) return true;
  return false;
}

void authorizeClient(uint8_t* identifier) {
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (!sessions[i].authorized) {
      memcpy(sessions[i].mac, identifier, 6);
      strncpy(sessions[i].phone, (char*)identifier, 15);
      sessions[i].authorized = true;
      sessions[i].startTime = millis();
      activeSessions++;
      sessionCount = min(sessionCount + 1, MAX_CLIENTS);
      break;
    }
  }
}

bool isClientAuthorized(const uint8_t* mac) {
  for (int i = 0; i < sessionCount; i++) {
    if (sessions[i].authorized && memcmp(sessions[i].mac, mac, 6) == 0) {
      return (millis() - sessions[i].startTime < SESSION_TIMEOUT);
    }
  }
  return false;
}

void checkExpiredSessions() {
  for (int i = 0; i < sessionCount; i++) {
    if (sessions[i].authorized && 
        (millis() - sessions[i].startTime > SESSION_TIMEOUT)) {
      sessions[i].authorized = false;
      activeSessions--;
    }
  }
}

void reportToVPS() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(VPS_URL);
    DynamicJsonDocument doc(512);
    doc["sessions"] = activeSessions;
    doc["modem"] = modemConnected;
    
    String payload;
    serializeJson(doc, payload);
    http.POST(payload);
    http.end();
  }
}