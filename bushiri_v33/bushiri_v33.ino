#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <DNSServer.h>

// Config - DO NOT CHANGE
const char* AP_SSID = "Bushiri PROJECT";
const char* MODEM_SSID = "PATAHUDUMA";
const char* MODEM_PASS = "AMUDUH123";
const char* VPS_HOST = "bushiri-project.onrender.com";
const int VPS_PORT = 443;
const char* VPS_TOKEN = "bushiri2026";
const char* OWNER_MAC = "bc:90:63:a2:32:83";
const char* MIX_PHONE = "0717633805";
const char* MIX_NAME = "HAMISI BUSHIRI LUONGO";
const int TEST_DURATION = 120000; // 2min
const int MAX_SESSIONS = 50;

// Globals
WebServer server(80);
DNSServer dnsServer;
WiFiClientSecure client;
String modemIP = "";
bool internetConnected = false;
unsigned long lastModemCheck = 0;

// Session tracking
struct Session {
  String mac;
  String ip;
  unsigned long startTime;
  bool isPaid;
  String txid;
};
Session sessions[MAX_SESSIONS];
int sessionCount = 0;

// NAT Forwarding buffers + clients
uint8_t rxBuffer[1460];
uint8_t txBuffer[1460];
WiFiClient forwardingClients[10];
int clientCount = 0;
WiFiServer natServer(8080);

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Bushiri PROJECT v3.3.9 - Starting...");

  // AP Mode
  WiFi.softAP(AP_SSID);
  Serial.println("AP Started: " + String(AP_SSID));
  Serial.print("AP IP: 192.168.4.1");

  // Captive DNS - redirect all to portal
  dnsServer.start(53, "192.168.4.1", WiFi.softAPIP());
  
  // OTA
  server.on("/update", HTTP_POST, []() {
    server.send(200, "text/plain", "OTA OK");
  });
  server.onNotFound(handlePortal);

  // Routes
  server.on("/", handlePortal);
  server.on("/portal", handlePortal);
  server.on("/admin", handleAdmin);
  server.on("/api/validate", handleValidate);
  server.on("/generate_204", handlePortal); // Android captive
  server.on("/fwlink", handlePortal);       // iOS captive
  server.on("/internet-ok", []() {
    server.send(200, "text/html", "<h1>🌐 Internet OK!</h1><script>window.location='http://1.1.1.1';</script>");
  });

  server.begin();
  Serial.println("WebServer + NAT proxy started");

  // Start NAT proxy server
  natServer.begin();
  Serial.println("NAT proxy on port 8080");

  connectToModem();
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  
  // Fast modem check (every 5s)
  if (millis() - lastModemCheck > 5000) {
    checkModemConnection();
    lastModemCheck = millis();
  }
  
  // NAT Forwarding Engine
  handleNATForwarding();
  
  // Session cleanup
  cleanupSessions();
}

void connectToModem() {
  Serial.println("Connecting to modem: " + String(MODEM_SSID));
  WiFi.begin(MODEM_SSID, MODEM_PASS);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    modemIP = WiFi.localIP().toString();
    internetConnected = true;
    Serial.println("\nModem connected! IP: " + modemIP);
  } else {
    Serial.println("\nModem failed - retrying...");
    internetConnected = false;
  }
}

void checkModemConnection() {
  if (WiFi.status() == WL_CONNECTED) {
    if (modemIP != WiFi.localIP().toString()) {
      modemIP = WiFi.localIP().toString();
      Serial.println("Modem IP updated: " + modemIP);
    }
  } else {
    Serial.println("Modem disconnected - reconnecting...");
    connectToModem();
  }
}

bool isOwner(String mac) {
  return mac == OWNER_MAC;
}

int findSession(String mac) {
  for (int i = 0; i < sessionCount; i++) {
    if (sessions[i].mac == mac) return i;
  }
  return -1;
}

void addSession(String mac, String ip, bool paid = false, String txid = "") {
  if (sessionCount >= MAX_SESSIONS) return;
  
  int idx = findSession(mac);
  if (idx >= 0) {
    sessions[idx].ip = ip;
    sessions[idx].isPaid = paid;
    sessions[idx].txid = txid;
    return;
  }
  
  sessions[sessionCount].mac = mac;
  sessions[sessionCount].ip = ip;
  sessions[sessionCount].startTime = millis();
  sessions[sessionCount].isPaid = paid;
  sessions[sessionCount].txid = txid;
  sessionCount++;
  
  reportSession(mac, ip, paid);
}

bool hasAccess(String mac, String ip) {
  if (isOwner(mac)) return true;
  
  int idx = findSession(mac);
  if (idx < 0) return false;
  
  Session& s = sessions[idx];
  unsigned long elapsed = millis() - s.startTime;
  
  return s.isPaid || elapsed < TEST_DURATION;
}

void cleanupSessions() {
  for (int i = 0; i < sessionCount; i++) {
    if (millis() - sessions[i].startTime > TEST_DURATION * 2) {
      for (int j = i; j < sessionCount - 1; j++) {
        sessions[j] = sessions[j + 1];
      }
      sessionCount--;
      i--;
    }
  }
}

void reportSession(String mac, String ip, bool paid) {
  if (!internetConnected) return;
  
  WiFiClient cl;
  if (cl.connect(VPS_HOST, VPS_PORT)) {
    cl.print("POST /api/report HTTP/1.1\r\n");
    cl.print("Host: " + String(VPS_HOST) + "\r\n");
    cl.print("Content-Type: application/json\r\n");
    String json = "{\"mac\":\"" + mac + "\",\"ip\":\"" + ip + "\",\"paid\":" + String(paid ? "true" : "false") + "}";
    cl.print("Content-Length: " + String(json.length()) + "\r\n");
    cl.print("Connection: close\r\n\r\n");
    cl.print(json);
    cl.stop();
    Serial.println("Session reported: " + mac);
  }
}

// FIXED NAT Forwarding - ESP32 3.3.8 COMPATIBLE
void handleNATForwarding() {
  if (!internetConnected) return;
  
  // Check for new NAT clients on port 8080
  WiFiClient newClient = natServer.available();
  if (newClient) {
    if (clientCount < 10) {
      forwardingClients[clientCount] = newClient;
      clientCount++;
      Serial.println("New NAT client #" + String(clientCount));
    } else {
      newClient.stop();
    }
  }
  
  // Process all forwarding clients
  for (int i = 0; i < clientCount; i++) {
    if (forwardingClients[i] && forwardingClients[i].connected()) {
      // Client -> Internet (outbound)
      int bytes = forwardingClients[i].available();
      if (bytes > 0) {
        bytes = forwardingClients[i].read(rxBuffer, min((int)sizeof(rxBuffer), bytes));
        Serial.printf("RX: %d bytes -> internet\n", bytes);
        
        // Use modem gateway IP directly - FIXED ESP32 3.3.8
        IPAddress gatewayIP = WiFi.gatewayIP();
        WiFiClient modemClient;
        if (modemClient.connect(gatewayIP, 80)) {
          modemClient.write(rxBuffer, bytes);
          int resp = modemClient.available();
          if (resp > 0) {
            resp = modemClient.read(txBuffer, min((int)sizeof(txBuffer), resp));
            forwardingClients[i].write(txBuffer, resp);
            Serial.printf("TX: %d bytes from internet\n", resp);
          }
          modemClient.stop();
        }
      }
    }
    
    // Cleanup disconnected clients
    if (!forwardingClients[i] || !forwardingClients[i].connected()) {
      if (forwardingClients[i]) forwardingClients[i].stop();
      forwardingClients[i] = WiFiClient();
      clientCount--;
      // Shift array
      for (int j = i; j < clientCount; j++) {
        forwardingClients[j] = forwardingClients[j + 1];
      }
      i--; // Check same index again
    }
  }
}

bool validateTXID(String txid) {
  if (!internetConnected) {
    Serial.println("VPS offline - TEST mode");
    return true; // Offline fallback
  }
  
  client.setInsecure();
  if (client.connect(VPS_HOST, VPS_PORT)) {
    client.print("GET /api/validate?txid=" + txid + 
                 "&token=" + String(VPS_TOKEN) + 
                 "&amount=800&phone=" + String(MIX_PHONE) + " HTTP/1.1\r\n");
    client.print("Host: " + String(VPS_HOST) + "\r\n");
    client.print("Connection: close\r\n\r\n");
    
    unsigned long timeout = millis() + 3000;
    while (client.connected() && millis() < timeout) {
      String line = client.readStringUntil('\n');
      if (line.indexOf("200") >= 0 || line.indexOf("302") >= 0) {
        while (client.connected()) {
          String response = client.readStringUntil('\n');
          if (response.indexOf("\"valid\":true") >= 0 || response.indexOf("success") >= 0) {
            client.stop();
            Serial.println("VPS VALID: " + txid);
            return true;
          }
        }
      }
    }
    client.stop();
  }
  Serial.println("VPS FAIL: " + txid);
  return false;
}

String getClientMAC() {
  return "client_" + String(random(10000)); // Simplified
}

void handlePortal() {
  String mac = getClientMAC();
  String clientIP = server.client().remoteIP().toString();
  
  if (hasAccess(mac, clientIP)) {
    server.sendHeader("Location", "http://192.168.4.1/internet-ok", true);
    server.send(302, "text/plain", "");
    return;
  }
  
  // Page 1 or Page 2 based on session
  int sessionIdx = findSession(mac);
  bool showPage2 = (sessionIdx >= 0);
  
  String html = R"=====(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Bushiri PROJECT</title>
  <style>
    * { margin:0; padding:0; box-sizing:border-box; }
    body { font-family: -apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif; background: linear-gradient(135deg,#667eea 0%,#764ba2 100%); min-height:100vh; display:flex; align-items:center; justify-content:center; }
    .container { background: rgba(255,255,255,0.95); backdrop-filter: blur(10px); border-radius: 20px; padding: 40px; max-width: 400px; width: 90%; box-shadow: 0 20px 40px rgba(0,0,0,0.1); text-align: center; }
    h1 { color: #333; margin-bottom: 20px; font-size: 24px; font-weight: 700; }
    .status { background: #f8f9fa; border: 2px solid #e9ecef; border-radius: 12px; padding: 20px; margin: 20px 0; }
    .status h3 { color: #495057; margin-bottom: 10px; font-size: 18px; }
    .phone { font-size: 28px; font-weight: bold; color: #28a745; letter-spacing: 2px; margin: 10px 0; }
    .name { font-size: 16px; color: #6c757d; margin-bottom: 20px; }
    input[type="text"] { width: 100%; padding: 15px; border: 2px solid #e9ecef; border-radius: 12px; font-size: 16px; margin: 10px 0; transition: all 0.3s; }
    input[type="text"]:focus { outline: none; border-color: #667eea; box-shadow: 0 0 0 3px rgba(102,126,234,0.1); }
    button { width: 100%; padding: 15px; background: linear-gradient(135deg,#28a745,#20c997); color: white; border: none; border-radius: 12px; font-size: 18px; font-weight: 600; cursor: pointer; transition: all 0.3s; margin: 10px 0; }
    button:hover { transform: translateY(-2px); box-shadow: 0 10px 20px rgba(40,167,69,0.3); }
    button:disabled { background: #6c757d; cursor: not-allowed; transform: none; }
    .price { font-size: 24px; font-weight: bold; color: #28a745; margin: 20px 0; }
    .timer { font-size: 14px; color: #6c757d; margin-top: 10px; }
    @media (max-width: 480px) { .container { padding: 30px 20px; margin: 20px; } }
  </style>
</head>
<body>
  <div class="container">
    <h1>🌐 Bushiri PROJECT</h1>
    <div class="status">
      <h3>Internet Access Required</h3>
      <div class="phone">Ksh 800</div>
      <div class="name">Pay for )" + String(MIX_NAME) + R"(</div>
      <div class="price">M-PESA Till: )" + String(MIX_PHONE) + R"(</div>
    </div>
    <input type="text" id="txid" placeholder="Enter TXID / MPESA Code" maxlength="50">
    <button onclick="validate()">VALIDATE PAYMENT</button>
    <div class="timer" id="timer">Free test: 2:00 minutes</div>
  </div>
  <script>
    let testTime = 120;
    let timer = setInterval(() => {
      testTime--;
      let mins = Math.floor(testTime / 60);
      let secs = testTime % 60;
      document.getElementById('timer').textContent = `Free test: ${mins}:${secs.toString().padStart(2,'0')} minutes`;
      if (testTime <= 0) clearInterval(timer);
    }, 1000);

    async function validate() {
      let txid = document.getElementById('txid').value.trim();
      if (!txid) return alert('Enter TXID');
      
      let btn = event.target;
      btn.disabled = true;
      btn.textContent = 'Validating...';
      
      try {
        let res = await fetch('/api/validate?txid=' + encodeURIComponent(txid));
        let data = await res.json();
        
        if (data.valid) {
          alert('✅ Payment Verified! Redirecting...');
          window.location.href = '/internet-ok';
        } else {
          alert('❌ Invalid TXID. Try again.');
        }
      } catch(e) {
        alert('Network error. Check connection.');
      }
      
      btn.disabled = false;
      btn.textContent = 'VALIDATE PAYMENT';
    }
  </script>
</body>
</html>
)=====";
  
  server.send(200, "text/html", html);
}

void handleAdmin() {
  String mac = getClientMAC();
  if (!isOwner(mac)) {
    server.send(403, "text/plain", "Access Denied");
    return;
  }
  
  String html = "<h1>Admin Panel</h1>";
  html += "<p>Sessions: " + String(sessionCount) + "</p>";
  for (int i = 0; i < sessionCount; i++) {
    html += "<p>" + sessions[i].mac + " - " + sessions[i].ip + " - " + String(sessions[i].isPaid ? "PAID" : "FREE") + "</p>";
  }
  server.send(200, "text/html", html);
}

void handleValidate() {
  if (server.hasArg("txid")) {
    String txid = server.arg("txid");
    String mac = getClientMAC();
    String clientIP = server.client().remoteIP().toString();
    
    bool valid = validateTXID(txid);
    if (valid) {
      addSession(mac, clientIP, true, txid);
      server.send(200, "application/json", "{\"valid\":true}");
    } else {
      server.send(200, "application/json", "{\"valid\":false,\"message\":\"Invalid TXID\"}");
    }
  } else {
    server.send(400, "application/json", "{\"valid\":false,\"message\":\"No TXID\"}");
  }
}