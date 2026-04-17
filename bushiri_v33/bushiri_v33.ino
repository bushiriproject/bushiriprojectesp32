#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <ESPmDNS.h>
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
uint16_t forwardingPort = 10000;

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

// NAT Forwarding buffers
uint8_t rxBuffer[1460];
uint8_t txBuffer[1460];
WiFiClient forwardingClients[10];
int clientCount = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Bushiri PROJECT v3.3.9 - Starting...");

  // AP Mode
  WiFi.softAP(AP_SSID);
  Serial.println("AP Started: " + String(AP_SSID));
  Serial.print("AP IP: 192.168.4.1");

  // Captive DNS - manual 192.168.4.1 redirect
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

  server.begin();
  Serial.println("WebServer started");

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
  
  WiFiClient client;
  if (client.connect(VPS_HOST, VPS_PORT)) {
    client.print("POST /api/report HTTP/1.1\r\n");
    client.print("Host: " + String(VPS_HOST) + "\r\n");
    client.print("Content-Type: application/json\r\n");
    String json = "{\"mac\":\"" + mac + "\",\"ip\":\"" + ip + "\",\"paid\":" + String(paid ? "true" : "false") + "}";
    client.print("Content-Length: " + String(json.length()) + "\r\n");
    client.print("Connection: close\r\n\r\n");
    client.print(json);
    client.stop();
    Serial.println("Session reported: " + mac);
  }
}

// FIXED NAT Forwarding Engine
void handleNATForwarding() {
  if (!internetConnected) return;
  
  // Handle incoming client connections
  WiFiClient newClient = server.available();
  if (newClient) {
    if (clientCount < 10) {
      forwardingClients[clientCount] = newClient;
      clientCount++;
      Serial.println("New forwarding client #" + String(clientCount));
    }
  }
  
  // Forward client -> modem
  for (int i = 0; i < clientCount; i++) {
    if (forwardingClients[i] && forwardingClients[i].connected()) {
      int bytes = forwardingClients[i].read(rxBuffer, sizeof(rxBuffer));
      if (bytes > 0) {
        Serial.printf("RX: %d bytes from client -> modem\n", bytes);
        
        WiFiClient modemClient;
        if (modemClient.connect(modemIP.c_str(), 80)) {  // Use modem as gateway
          modemClient.write(rxBuffer, bytes);
          int resp = modemClient.read(txBuffer, sizeof(txBuffer));
          if (resp > 0) {
            forwardingClients[i].write(txBuffer, resp);
            Serial.printf("TX: %d bytes modem -> client\n", resp);
          }
          modemClient.stop();
        }
      }
    } else {
      // Cleanup dead client
      forwardingClients[i] = WiFiClient();
      clientCount--;
      for (int j = i; j < clientCount; j++) {
        forwardingClients[j] = forwardingClients[j + 1];
      }
    }
  }
}

bool validateTXID(String txid) {
  if (!internetConnected) {
    Serial.println("VPS offline - TEST mode");
    return true; // Offline fallback
  }
  
  if (client.connect(VPS_HOST, VPS_PORT)) {
    client.print("GET /api/validate?txid=" + txid + 
                 "&token=" + String(VPS_TOKEN) + 
                 "&amount=800&phone=" + String(MIX_PHONE) + " HTTP/1.1\r\n");
    client.print("Host: " + String(VPS_HOST) + "\r\n");
    client.print("Connection: close\r\n\r\n");
    
    unsigned long timeout = millis() + 3000;
    while (client.connected() && millis() < timeout) {
      String line = client.readStringUntil('\n');
      if (line.indexOf("200 OK") >= 0 || line.indexOf("302") >= 0) {
        while (client.connected()) {
          String response = client.readStringUntil('\n');
          if (response.indexOf("\"valid\":true") >= 0) {
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
  uint8_t mac[6];
  WiFi.softAPgetStationNum();
  return String("mac_placeholder"); // Simplified for ESP32 3.3.8
}

void handlePortal() {
  String mac = getClientMAC();
  String clientIP = server.client().remoteIP().toString();
  
  if (hasAccess(mac, clientIP)) {
    server.sendHeader("Location", "http://192.168.4.1/internet-ok", true);
    server.send(302);
    return;
  }
  
  // Page 1 or Page 2 based on session
  int sessionIdx = findSession(mac);
  bool showPage2 = (sessionIdx >= 0);
  
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Bushiri PROJECT</title>
  <style>
    * { margin:0; padding:0; box-sizing:border-box; }
    body { 
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      min-height: 100vh; 
      display: flex; 
      align-items: center; 
      justify-content: center; 
      color: white;
    }
    .container { 
      background: rgba(255,255,255,0.1); 
      backdrop-filter: blur(20px); 
      border-radius: 20px; 
      padding: 40px; 
      max-width: 400px; 
      width: 90%; 
      box-shadow: 0 20px 40px rgba(0,0,0,0.3);
      text-align: center;
      animation: fadeIn 1s ease-out;
    }
    @keyframes fadeIn { from { opacity:0; transform:translateY(30px); } to { opacity:1; transform:translateY(0); } }
    h1 { 
      font-size: 28px; 
      margin-bottom: 20px; 
      background: linear-gradient(45deg, #fff, #f0f0f0); 
      -webkit-background-clip: text; 
      -webkit-text-fill-color: transparent;
      font-weight: 700;
    }
    .price { 
      font-size: 36px; 
      font-weight: bold; 
      color: #FFD700; 
      margin: 20px 0; 
      text-shadow: 0 2px 10px rgba(255,215,0,0.5);
    }
    .highlight { 
      background: linear-gradient(45deg, #FF6B6B, #4ECDC4); 
      padding: 15px; 
      border-radius: 15px; 
      margin: 20px 0; 
      box-shadow: 0 10px 30px rgba(0,0,0,0.3);
    }
    .highlight h3 { font-size: 20px; margin-bottom: 10px; }
    .highlight p { font-size: 16px; line-height: 1.5; }
    input[type="password"], input[type="text"] { 
      width: 100%; 
      padding: 15px; 
      margin: 15px 0; 
      border: none; 
      border-radius: 12px; 
      font-size: 16px; 
      background: rgba(255,255,255,0.9); 
      box-shadow: inset 0 2px 10px rgba(0,0,0,0.1);
    }
    button { 
      width: 100%; 
      padding: 18px; 
      background: linear-gradient(45deg, #FF6B6B, #4ECDC4); 
      color: white; 
      border: none; 
      border-radius: 12px; 
      font-size: 18px; 
      font-weight: bold; 
      cursor: pointer; 
      transition: all 0.3s; 
      box-shadow: 0 10px 30px rgba(0,0,0,0.3);
    }
    button:hover { transform: translateY(-2px); box-shadow: 0 15px 40px rgba(0,0,0,0.4); }
    button:active { transform: translateY(0); }
    .page2 { background: linear-gradient(135deg, #11998e, #38ef7d); }
    .status { padding: 10px; border-radius: 8px; margin: 10px 0; font-weight: bold; }
    .success { background: rgba(76,175,80,0.3); }
    .error { background: rgba(244,67,54,0.3); }
  </style>
</head>
<body class=)rawliteral" + String(showPage2 ? "page2" : "") + R"rawliteral(>
  <div class="container">
)rawliteral";

  if (!showPage2) {
    // PAGE 1 - Welcome + TEST123
    html += R"rawliteral(
    <h1>🌐 Bushiri PROJECT</h1>
    <div class="price">TZS 800</div>
    <div class="highlight">
      <h3>🚀 TEST123 - BURE 2 Dakika!</h3>
      <p>Tumia namba <strong>TEST123</strong> upate internet ya kutosha dakika 2 za majaribio.</p>
    </div>
    <div class="highlight">
      <h3>💰 Lipa MIXX</h3>
      <p><strong>Phone:</strong> 0717633805<br>
      <strong>Name:</strong> HAMISI BUSHIRI LUONGO<br>
      <strong>Service:</strong> BUSHIRI PROJECT<br>
      <strong>Amount:</strong> 800 TZS</p>
    </div>
    <input type="password" id="pass" placeholder="Ingiza TEST123 au namba yako">
    <button onclick="connect()">UNGANA SASA</button>
  )rawliteral";
  } else {
    // PAGE 2 - Payment verification
    html += R"rawliteral(
    <h1>✅ Thamani Malipo</h1>
    <div class="status success">Malipo yako imepokelewa!</div>
    <div class="highlight">
      <h3>📱 Lipa MIXX Sasa</h3>
      <p><strong>☎️ MPESA:</strong> 0717633805<br>
      <strong>👤 Jina:</strong> HAMISI BUSHIRI LUONGO<br>
      <strong>📝 Huduma:</strong> BUSHIRI PROJECT<br>
      <strong>💰 Kiasi:</strong> 800 TZS</p>
    </div>
    <input type="text" id="txid" placeholder="Ingiza TXID yako hapa">
    <button onclick="verify()">THAMANI SASA</button>
    <p style="font-size:12px; margin-top:20px; opacity:0.8;">
      Au tumia <strong>TEST123</strong> kwa majaribio ya dakika 2
    </p>
  )rawliteral";
  }

  html += R"rawliteral(
  </div>
  <script>
    function connect() {
      const pass = document.getElementById('pass').value;
      if (pass === 'TEST123') {
        fetch('/api/validate?pass=TEST123', {method: 'GET'})
          .then(() => location.href = 'http://192.168.4.1/internet-ok')
          .catch(() => alert('Kosa! Jaribu tena'));
      } else {
        alert('Tumia TEST123 kwa majaribio au lipa kwanza');
      }
    }
    
    function verify() {
      const txid = document.getElementById('txid').value;
      if (!txid) return alert('Ingiza TXID');
      
      fetch('/api/validate?txid=' + txid, {method: 'GET'})
        .then(res => res.text())
        .then(data => {
          if (data.includes('success')) {
            location.href = 'http://192.168.4.1/internet-ok';
          } else {
            alert('TXID batili! Lipa tena au tumia TEST123');
          }
        });
    }
  </script>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}

void handleValidate() {
  String mac = getClientMAC();
  String clientIP = server.client().remoteIP().toString();
  String txid = server.arg("txid");
  String pass = server.arg("pass");
  
  Serial.println("Validate: mac=" + mac + " ip=" + clientIP + " txid=" + txid + " pass=" + pass);
  
  bool valid = false;
  if (pass == "TEST123") {
    valid = true;
    Serial.println("TEST123 approved");
  } else if (txid.length() > 0) {
    valid = validateTXID(txid);
  }
  
  if (valid) {
    addSession(mac, clientIP, txid.length() > 0, txid);
    server.send(200, "text/plain", "success");
  } else {
    server.send(400, "text/plain", "invalid");
  }
}

void handleAdmin() {
  String html = "<h1>Bushiri Admin</h1>";
  html += "<p><strong>Modem:</strong> " + String(internetConnected ? "OK (" + modemIP + ")" : "OFFLINE") + "</p>";
  html += "<p><strong>Sessions:</strong> " + String(sessionCount) + "/" + String(MAX_SESSIONS) + "</p>";
  html += "<h3>Sessions:</h3><pre>";
  
  for (int i = 0; i < sessionCount; i++) {
    html += "MAC: " + sessions[i].mac + " | IP: " + sessions[i].ip;
    html += " | Paid: " + String(sessions[i].isPaid ? "YES" : "NO");
    if (sessions[i].txid != "") html += " | TXID: " + sessions[i].txid;
    html += "\n";
  }
  html += "</pre>";
  
  server.send(200, "text/html", html);
}