#include <WiFi.h>
#include <WiFiAP.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <mbedtls/md5.h>

// Bushiri PROJECT - ESP32 Captive Portal with M-PESA & NAT
// Platform: ESP32 3.3.8 compatible

// Configuration
const char* AP_SSID = "Bushiri-WiFi";
const char* AP_PASS = "12345678";
const char* VPS_URL = "https://your-vps.com/report";
const int AP_CHANNEL = 6;
const int MAX_CLIENTS = 8;
const int NAT_PORT = 8080;
const unsigned long SESSION_TIMEOUT = 3600000; // 1 hour
const unsigned long CHECK_INTERVAL = 30000;    // 30 seconds

// Modem APN (adjust for your carrier)
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
  String phone;
  String mpesaTxId;
  unsigned long startTime;
  bool authorized;
};

ClientSession sessions[MAX_CLIENTS];
int sessionCount = 0;

// DNS responses for captive portal detection
const char* dnsResponse = "1.1.1.1";

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("Bushiri PROJECT Starting...");
  
  // Initialize preferences
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
  
  // Start servers
  server.on("/", handlePortal);
  server.on("/admin", handleAdmin);
  server.on("/mpesa-validate", handleMpesaValidate);
  server.on("/generate_204", handlePortal);  // Captive portal detection
  server.on("/fwlink", handlePortal);        // Android captive detection
  server.onNotFound(handlePortal);
  server.begin();
  
  // NAT server for port forwarding
  natServer.onNotFound(handleNATForward);
  natServer.begin();
  
  // DNS server for captive portal
  startDNSServer();
  
  // Connect modem
  connectModem();
  
  Serial.println("Bushiri PROJECT Ready!");
}

void loop() {
  server.handleClient();
  natServer.handleClient();
  
  unsigned long now = millis();
  
  // Periodic tasks
  if (now - lastCheck > CHECK_INTERVAL) {
    checkExpiredSessions();
    reportToVPS();
    lastCheck = now;
  }
  
  // Reconnect modem if needed
  if (!modemConnected) {
    connectModem();
  }
  
  delay(10);
}

// Captive portal page
void handlePortal() {
  String clientIP = server.client().remoteIP().toString();
  uint8_t mac[6];
  WiFi.softAPgetStationNum();
  
  // Check if client is authorized
  if (isClientAuthorized(mac)) {
    server.sendHeader("Location", "http://" + clientIP + ":8080/", true);
    server.send(302);
    return;
  }
  
  String html = R"(
<!DOCTYPE html>
<html>
<head>
    <title>Bushiri WiFi - Pay KSh 50</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial; max-width: 400px; margin: 50px auto; padding: 20px; text-align: center; }
        .pay-button { background: #25D366; color: white; padding: 15px; border: none; border-radius: 10px; font-size: 18px; width: 100%; margin: 10px 0; }
        .status { padding: 10px; margin: 10px 0; border-radius: 5px; }
        .success { background: #d4edda; color: #155724; }
        .error { background: #f8d7da; color: #721c24; }
    </style>
</head>
<body>
    <h1>🌐 Bushiri WiFi</h1>
    <p>Pay <strong>KSh 50</strong> for 1 hour unlimited access</p>
    <button class="pay-button" onclick="payMpesa()">Pay with M-PESA</button>
    <div id="status"></div>
    
    <script>
        function payMpesa() {
            if (confirm('Send KSh 50 to 123456?\nPhone: ' + getPhone())) {
                fetch('/mpesa-validate', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({phone: getPhone()})
                })
                .then(r => r.json())
                .then(data => {
                    if (data.success) {
                        document.getElementById('status').innerHTML = 
                            '<div class="status success">✅ Payment verified! Redirecting...</div>';
                        setTimeout(() => window.location.href = 'http://192.168.4.1:8080/', 2000);
                    } else {
                        document.getElementById('status').innerHTML = 
                            '<div class="status error">❌ ' + data.error + '</div>';
                    }
                });
            }
        }
        
        function getPhone() {
            let phone = prompt('Enter your M-PESA phone number:');
            return phone ? phone : '';
        }
    </script>
</body>
</html>
  )";
  
  server.send(200, "text/html", html);
}

// Admin panel
void handleAdmin() {
  String adminHtml = R"(
<!DOCTYPE html>
<html>
<head><title>Bushiri Admin</title><meta name="viewport" content="width=device-width, initial-scale=1"></head>
<body>
    <h1>Bushiri Admin Panel</h1>
    <p>Active sessions: )" + String(activeSessions) + R"(</p>
    <p>Modem: )" + String(modemConnected ? "Connected" : "Disconnected") + R"(</p>
    <h3>Sessions:</h3>
    <ul>";
  
  for (int i = 0; i < sessionCount; i++) {
    adminHtml += "<li>" + sessions[i].phone + " - " + 
                 (sessions[i].authorized ? "Active" : "Pending") + "</li>";
  }
  
  adminHtml += R"(
    </ul>
    <button onclick="location.reload()">Refresh</button>
</body>
</html>
  )";
  
  server.send(200, "text/html", adminHtml);
}

// M-PESA validation (mock - replace with real STK API)
void handleMpesaValidate() {
  if (server.method() != HTTP_POST) {
    server.send(405);
    return;
  }
  
  String body = server.arg("plain");
  DynamicJsonDocument doc(1024);
  deserializeJson(doc, body);
  
  String phone = doc["phone"];
  
  // Mock validation - replace with real M-PESA STK Push
  bool valid = mockMpesaValidation(phone);
  
  DynamicJsonDocument response(512);
  response["success"] = valid;
  response["phone"] = phone;
  if (!valid) {
    response["error"] = "Invalid payment or phone number";
  }
  
  String json;
  serializeJson(response, json);
  server.send(200, "application/json", json);
  
  if (valid) {
    authorizeClient(phone);
  }
}

// NAT port forwarding - redirect to modem internet
void handleNATForward() {
  WiFiClient client = natServer.client();
  
  if (!modemConnected) {
    natServer.send(503, "text/plain", "No internet connection");
    return;
  }
  
  // Check if client is authorized by IP/MAC
  uint8_t mac[6];
  if (!isClientAuthorized(mac)) {
    natServer.send(403, "text/plain", "Access denied");
    return;
  }
  
  // Forward request through modem connection
  HTTPClient http;
  http.begin("http://" + server.hostHeader() + server.uri());
  int httpCode = http.GET();
  
  if (httpCode > 0) {
    String payload = http.getString();
    natServer.sendHeader("Content-Type", http.header("Content-Type"));
    natServer.send(httpCode, http.header("Content-Type"), payload);
  } else {
    natServer.send(502, "text/plain", "Forwarding failed");
  }
  
  http.end();
}

// Modem connection (replace with your modem library)
void connectModem() {
  Serial.println("Connecting modem...");
  
  // Example for SIM800/SIM7600 - adjust for your modem
  // Send AT commands to connect to internet via APN
  Serial2.begin(115200); // Modem serial
  
  // AT commands sequence
  sendModemAT("AT");
  delay(1000);
  sendModemAT("AT+CPIN?");
  sendModemAT("AT+CREG?");
  sendModemAT("AT+CGATT=1");
  sendModemAT("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"");
  sendModemAT("AT+SAPBR=3,1,\"APN\",\"" + String(MODEM_APN) + "\"");
  sendModemAT("AT+SAPBR=1,1");
  sendModemAT("AT+HTTPINIT");
  
  // Check connection status
  if (checkModemSignal()) {
    modemConnected = true;
    Serial.println("Modem connected!");
  } else {
    modemConnected = false;
    Serial.println("Modem connection failed");
  }
}

bool mockMpesaValidation(String phone) {
  // Replace with real M-PESA Daraja API integration
  // For now, mock validation (first 3 payments valid)
  static int paymentCount = 0;
  if (paymentCount < 3) {
    paymentCount++;
    return true;
  }
  return false;
}

void authorizeClient(String phone) {
  uint8_t mac[6];
  WiFi.softAPgetStationInfo(mac, 0); // Simplified - get current client
  
  // Find empty session slot
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (sessions[i].authorized == false) {
      memcpy(sessions[i].mac, mac, 6);
      sessions[i].phone = phone;
      sessions[i].authorized = true;
      sessions[i].startTime = millis();
      activeSessions++;
      sessionCount = min(sessionCount + 1, MAX_CLIENTS);
      break;
    }
  }
}

bool isClientAuthorized(uint8_t* mac) {
  for (int i = 0; i < sessionCount; i++) {
    if (memcmp(sessions[i].mac, mac, 6) == 0 && sessions[i].authorized) {
      if (millis() - sessions[i].startTime < SESSION_TIMEOUT) {
        return true;
      }
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
  if (WiFi.status() == WL_CONNECTED) { // STA mode for VPS reporting
    HTTPClient http;
    http.begin(VPS_URL);
    
    DynamicJsonDocument doc(1024);
    doc["sessions"] = activeSessions;
    doc["modem"] = modemConnected;
    doc["uptime"] = millis() / 1000;
    
    String payload;
    serializeJson(doc, payload);
    
    http.POST(payload);
    http.end();
  }
}

void startDNSServer() {
  // Simple DNS server to redirect all to captive portal
  // Implementation would use AsyncUDP or similar
  Serial.println("DNS server started for captive portal");
}

void sendModemAT(String cmd) {
  Serial2.println(cmd);
  delay(500);
  while (Serial2.available()) {
    Serial.write(Serial2.read());
  }
}

bool checkModemSignal() {
  sendModemAT("AT+CSQ");
  // Parse response for signal strength
  return true; // Simplified
}