#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "esp_wifi.h"

// ==================== EDIT HAPA TU ====================

// 1. Jina la WiFi yako
const char* AP_SSID = "Bushiri PROJECT";

// 2. Password (acha "" kwa open network)
const char* AP_PASS = "";

// 3. IP ya Oracle VPS yako
const char* VPS_HOST = "bushiri-project.onrender.com";

// 4. Port ya backend yako
const int VPS_PORT = 443;

// 5. Token ya siri (lazima ilingane na backend)
const char* VPS_TOKEN = "bushiri2026";

// 6. MAC ya simu/laptop yako (free access - owner)
String authorizedMACs[10] = {"bc:90:63:a2:32:83"};

// 7. Jina la biashara yako kwenye portal page
const char* PORTAL_TITLE = "BUSHIRI PROJECT";

// 8. Nambari yako ya MIXX BY YAS
const char* MIX_NUMBER = "0717633805";

// Modem credentials
const String MODEM_SSID = "PATAHUDUMA";
const String MODEM_PASS = "AMUDUH123";

// ==================== END EDIT ====================

// Core configuration
IPAddress AP_IP(192, 168, 4, 1);
IPAddress AP_GATEWAY(192, 168, 4, 1);
IPAddress AP_SUBNET(255, 255, 255, 0);

// Payment details
const String PAYMENT_NAME = "HAMISI BUSHIRI LUONGO";
const String PAYMENT_AMOUNT = "800";
const String TEST_CODE = "TEST123";

// Server instances
WebServer server(80);
DNSServer dnsServer;
WiFiClientSecure httpsClient;

// Session tracking
struct ClientSession {
  String mac;
  String ip;
  unsigned long startTime;
  bool isPaid;
  String txid;
};
ClientSession activeSessions[50];
int sessionCount = 0;
bool internetConnected = false;
unsigned long lastVPSUpdate = 0;

// Beautiful responsive HTML
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>)rawliteral" STRINGIFY(PORTAL_TITLE) R"rawliteral( - Fast Internet</title>
    <style>
        *{margin:0;padding:0;box-sizing:border-box;font-family:system-ui,-apple-system,sans-serif}
        body{background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px}
        .container{max-width:400px;width:100%;background:white;border-radius:25px;box-shadow:0 25px 50px rgba(0,0,0,0.15);overflow:hidden}
        .header{background:linear-gradient(135deg,#ff6b6b,#feca57);padding:35px 25px;text-align:center;color:white;position:relative;overflow:hidden}
        .header::before{content:'';position:absolute;top:0;left:-100%;width:100%;height:100%;background:linear-gradient(90deg,transparent,rgba(255,255,255,0.3),transparent);transition:left 0.5s}
        .header:hover::before{left:100%}
        .header h1{font-size:26px;margin-bottom:8px;font-weight:700}
        .header p{font-size:15px;opacity:0.95}
        .form-group{padding:30px}
        .input-group{position:relative;margin-bottom:25px}
        .input-group input{width:100%;padding:18px 22px;border:2px solid #e9ecef;border-radius:15px;font-size:17px;transition:all 0.3s ease;outline:none;background:white}
        .input-group input:focus{border-color:#667eea;box-shadow:0 0 0 4px rgba(102,126,234,0.15)}
        .btn{display:block;width:100%;padding:18px;background:linear-gradient(135deg,#667eea,#764ba2);color:white;border:none;border-radius:15px;font-size:18px;font-weight:600;cursor:pointer;transition:all 0.3s;text-transform:uppercase;letter-spacing:1px;position:relative;overflow:hidden}
        .btn:hover{transform:translateY(-3px);box-shadow:0 15px 30px rgba(102,126,234,0.4)}
        .btn:active{transform:translateY(-1px)}
        .payment-info{background:linear-gradient(135deg,#d4edda,#c3e6cb);padding:25px;border-radius:15px;margin:25px 0;border-left:5px solid #28a745}
        .payment-info h3{color:#155724;font-size:18px;margin-bottom:12px;font-weight:600}
        .payment-info p{margin:8px 0;font-size:15px;font-weight:500}
        .payment-info strong{color:#28a745}
        .test-mode{background:linear-gradient(135deg,#fff3cd,#ffeaa7);color:#856404;padding:20px;border-radius:15px;margin:25px 0;text-align:center;font-weight:600;font-size:16px;border:2px dashed #ffc107}
        .status{display:none;padding:20px;border-radius:15px;margin:25px 0;font-weight:600;text-align:center;font-size:16px;transition:all 0.3s}
        .status.success{background:linear-gradient(135deg,#d4edda,#c3e6cb);color:#155724;border:2px solid #28a745}
        .status.error{background:linear-gradient(135deg,#f8d7da,#f5c6cb);color:#721c24;border:2px solid #dc3545}
        .status.warning{background:linear-gradient(135deg,#fff3cd,#ffeaa7);color:#856404;border:2px dashed #ffc107}
        @media (max-width:480px){.container{border-radius:20px;padding:10px;margin:10px}}
        @keyframes pulse{0%,100%{opacity:1}50%{opacity:0.7}}
        .pulse{animation:pulse 2s infinite}
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>🌐 )rawliteral" STRINGIFY(PORTAL_TITLE) R"rawliteral(</h1>
            <p>High Speed • Unlimited • Reliable</p>
        </div>
        <div class="form-group">
            <div class="test-mode pulse">🔥 Enter <strong>TEST123</strong> for 2 Minutes FREE Test!</div>
            
            <div class="input-group">
                <input type="text" id="txid" placeholder="TXID or TEST123" maxlength="25" autocomplete="off">
            </div>
            
            <button class="btn" onclick="validateTXID()">🚀 CONNECT NOW</button>
            
            <div class="payment-info">
                <h3>💳 Pay TZS 800 to:</h3>
                <p><strong>BUSHIRI</strong> <span style="color:#28a745;font-size:20px">)rawliteral" STRINGIFY(MIX_NUMBER) R"rawliteral(</span></p>
                <p><strong>MIXX BY YAS</strong> • <strong>HAMISI BUSHIRI LUONGO</strong></p>
            </div>
            
            <div class="status" id="status"></div>
        </div>
    </div>
    
    <script>
        const txidInput = document.getElementById('txid');
        txidInput.focus();
        txidInput.addEventListener('keypress', e => {if(e.key==='Enter') validateTXID()});
        
        function validateTXID() {
            const txid = txidInput.value.trim().toUpperCase();
            const status = document.getElementById('status');
            status.style.display = 'block';
            status.className = 'status';
            
            if(!txid) {
                status.innerHTML = '⚠️ Please enter TXID or TEST123';
                status.classList.add('warning');
                return;
            }
            
            if(txid === 'TEST123') {
                status.innerHTML = '✅ 2min FREE test activated! Connecting...';
                status.classList.add('success');
                setTimeout(() => window.location.href='http://1.1.1.1', 1500);
                return;
            }
            
            status.innerHTML = '🔄 Validating payment...';
            status.classList.add('warning');
            
            fetch('/api/validate?txid=' + encodeURIComponent(txid))
                .then(r => r.json())
                .then(data => {
                    if(data.valid) {
                        status.innerHTML = '✅ Payment verified! Enjoy unlimited internet!';
                        status.classList.remove('warning');
                        status.classList.add('success');
                        setTimeout(() => window.location.href='http://1.1.1.1', 1500);
                    } else {
                        status.innerHTML = '❌ Invalid TXID. Pay TZS 800 then enter TXID.';
                        status.classList.remove('warning');
                        status.classList.add('error');
                        txidInput.value = '';
                        txidInput.focus();
                    }
                })
                .catch(() => {
                    status.innerHTML = '⚠️ Network error. Check connection & try again.';
                    status.classList.add('warning');
                });
        }
    </script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  // Disable brownout detector
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  
  // Setup AP
  WiFi.mode(WIFI_AP_STA);
  IPAddress local_IP(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  
  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP(AP_SSID, AP_PASS);
  
  // DNS captive portal
  dnsServer.start(53, "*", local_IP);
  
  // Web server routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/generate_204", HTTP_GET, handleRoot);
  server.on("/fwlink", HTTP_GET, handleRoot);
  server.on("/api/validate", HTTP_GET, handleValidate);
  server.on("/admin", HTTP_GET, handleAdmin);
  server.on("/api/admin/config", HTTP_POST, handleConfig);
  server.on("/api/sessions", HTTP_GET, handleSessions);
  server.on("/update", HTTP_POST, handleOTA, handleOTAUpload);
  
  server.begin();
  
  // Connect to modem immediately
  connectToModem();
  
  Serial.printf("🌐 %s started\n", PORTAL_TITLE);
  Serial.printf("📶 AP IP: 192.168.4.1\n");
  Serial.printf("🔗 Admin: http://192.168.4.1/admin\n");
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 30000) {
    maintainInternet();
    cleanupSessions();
    lastCheck = millis();
  }
}

void handleRoot() {
  server.send_P(200, "text/html", index_html);
}

void handleValidate() {
  String txid = server.arg("txid").substring(0, 25);
  String clientIP = server.client().remoteIP().toString();
  String clientMAC = getClientMAC();
  
  // Check authorized MACs (owner free access)
  for (int i = 0; i < 10; i++) {
    if (authorizedMACs[i].length() > 0 && clientMAC == authorizedMACs[i]) {
      activateSession(clientMAC, clientIP, "OWNER_FREE");
      server.send(200, "application/json", "{\"valid\":true,\"message\":\"Owner access granted\"}");
      return;
    }
  }
  
  // TEST123 free test
  if (txid == TEST_CODE) {
    activateSession(clientMAC, clientIP, TEST_CODE);
    server.send(200, "application/json", "{\"valid\":true,\"message\":\"2min test activated\"}");
    return;
  }
  
  // VPS validation
  bool isValid = false;
  if (internetConnected) {
    isValid = validateTXIDVPS(txid);
  }
  
  if (isValid) {
    activateSession(clientMAC, clientIP, txid);
  }
  
  String response = "{\"valid\":" + String(isValid ? "true" : "false") + 
                   ",\"message\":\"" + (isValid ? "Payment verified" : "Invalid TXID") + "\"}";
  server.send(200, "application/json", response);
}

String getClientMAC() {
  uint8_t mac[6];
  wifi_get_macaddr(SOFTAP_IF, mac);
  char macStr[18];
  sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", 
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(macStr);
}

void activateSession(String mac, String ip, String txid) {
  // Cleanup old session first
  for (int i = 0; i < sessionCount; i++) {
    if (activeSessions[i].mac == mac) {
      // Move remaining sessions up
      for (int j = i; j < sessionCount - 1; j++) {
        activeSessions[j] = activeSessions[j + 1];
      }
      sessionCount--;
      break;
    }
  }
  
  // Add new session
  if (sessionCount < 50) {
    activeSessions[sessionCount].mac = mac;
    activeSessions[sessionCount].ip = ip;
    activeSessions[sessionCount].startTime = millis();
    activeSessions[sessionCount].isPaid = true;
    activeSessions[sessionCount].txid = txid;
    sessionCount++;
  }
}

bool isClientAuthorized(IPAddress clientIP) {
  String clientIPStr = clientIP.toString();
  for (int i = 0; i < sessionCount; i++) {
    if (activeSessions[i].ip == clientIPStr && activeSessions[i].isPaid) {
      unsigned long duration = (activeSessions[i].txid == TEST_CODE) ? 120000UL : 86400000UL;
      if (millis() - activeSessions[i].startTime < duration) {
        return true;
      }
    }
  }
  return false;
}

void connectToModem() {
  WiFi.begin(MODEM_SSID.c_str(), MODEM_PASS.c_str());
  Serial.printf("🔗 Connecting to modem %s...\n", MODEM_SSID.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(1000);
    Serial.print(".");
    attempts++;
  }
  
  internetConnected = (WiFi.status() == WL_CONNECTED);
  if (internetConnected) {
    Serial.println("\n🌍 Modem connected: " + WiFi.localIP().toString());
  } else {
    Serial.println("\n❌ Modem connection failed");
  }
}

bool validateTXIDVPS(String txid) {
  httpsClient.setInsecure(); // For Render.com SSL
  httpsClient.setTimeout(5000);
  
  if (!httpsClient.connect(VPS_HOST, VPS_PORT)) {
    return false;
  }
  
  String url = "/api/validate?txid=" + txid + "&token=" + VPS_TOKEN + 
               "&amount=" + PAYMENT_AMOUNT + "&phone=" + MIX_NUMBER;
  
  httpsClient.print(String("GET ") + url + " HTTP/1.1\r\n" +
                   "Host: " + VPS_HOST + "\r\n" +
                   "Connection: close\r\n\r\n");
  
  unsigned long timeout = millis();
  while (httpsClient.connected() && millis() - timeout < 5000) {
    String line = httpsClient.readStringUntil('\n');
    if (line == "\r") break;
  }
  
  String response = "";
  while (httpsClient.available()) {
    response += (char)httpsClient.read();
  }
  
  httpsClient.stop();
  
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, response);
  if (error) return false;
  
  return doc["valid"].as<bool>();
}

void maintainInternet() {
  if (WiFi.status() != WL_CONNECTED) {
    connectToModem();
  }
  
  // Report sessions to VPS
  if (internetConnected && sessionCount > 0) {
    reportSessionsToVPS();
  }
}

void reportSessionsToVPS() {
  httpsClient.setInsecure();
  if (!httpsClient.connect(VPS_HOST, VPS_PORT)) return;
  
  DynamicJsonDocument doc(2048);
  JsonArray sessions = doc.createNestedArray("sessions");
  
  for (int i = 0; i < sessionCount; i++) {
    JsonObject session = sessions.createNestedObject();
    session["mac"] = activeSessions[i].mac;
    session["ip"] = activeSessions[i].ip;
    session["txid"] = activeSessions[i].txid;
    session["uptime"] = millis() - activeSessions[i].startTime;
    session["active"] = true;
  }
  
  doc["token"] = VPS_TOKEN;
  doc["total_clients"] = sessionCount;
  doc["internet_status"] = internetConnected;
  
  String payload;
  serializeJson(doc, payload);
  
  String url = "/api/report";
  httpsClient.print(String("POST ") + url + " HTTP/1.1\r\n" +
                   "Host: " + VPS_HOST + "\r\n" +
                   "Content-Type: application/json\r\n" +
                   "Content-Length: " + payload.length() + "\r\n" +
                   "Connection: close\r\n\r\n");
  httpsClient.print(payload);
  
  httpsClient.stop();
}

void cleanupSessions() {
  int newCount = 0;
  for (int i = 0; i < sessionCount; i++) {
    unsigned long duration = (activeSessions[i].txid == TEST_CODE) ? 120000UL : 86400000UL;
    if (millis() - activeSessions[i].startTime < duration) {
      activeSessions[newCount] = activeSessions[i];
      newCount++;
    }
  }
  sessionCount = newCount;
}

void handleAdmin() {
  if (!server.hasArg("admin") || server.arg("admin") != "bushiri2026") {
    server.send(401, "text/plain", "Unauthorized");
    return;
  }
  
  String html = "<!DOCTYPE html><html><head><title>" + String(PORTAL_TITLE) + " Admin</title>";
  html += "<style>body{font-family:Arial;background:#f5f5f5;padding:20px;margin:0}table{width:100%;border-collapse:collapse}th,td{border:1px solid #ddd;padding:12px;text-align:left}th{background:#667eea;color:white}.status{" + 
          (internetConnected ? "color:green" : "color:red") + ";font-weight:bold}</style></head>";
  html += "<body><h1>🔧 " + String(PORTAL_TITLE) + " Admin Panel</h1>";
  html += "<h2>Internet: <span class='status'>" + String(internetConnected ? "🟢 ONLINE" : "🔴 OFFLINE") + "</span></h2>";
  html += "<h3>Modem: " + MODEM_SSID + "</h3>";
  html += "<h3>Active Sessions: " + String(sessionCount) + "</h3>";
  html += "<table><tr><th>MAC</th><th>IP</th><th>TXID</th><th>Uptime</th></tr>";
  
  for (int i = 0; i < sessionCount; i++) {
    unsigned long uptime = (millis() - activeSessions[i].startTime) / 1000;
    html += "<tr><td>" + activeSessions[i].mac + "</td><td>" + activeSessions[i].ip + 
            "</td><td>" + activeSessions[i].txid + "</td><td>" + String(uptime) + "s</td></tr>";
  }
  
  html += "</table><p><a href='/'>← Back to Portal</a> | ";
  html += "<a href='/update'>OTA Update</a></p></body></html>";
  
  server.send(200, "text/html", html);
}

void handleConfig() {
  server.send(200, "text/plain", "Modem config locked: " + MODEM_SSID);
}

void handleSessions() {
  DynamicJsonDocument doc(4096);
  JsonArray sessions = doc.createNestedArray("sessions");
  for (int i = 0; i < sessionCount; i++) {
    JsonObject s = sessions.createNestedObject();
    s["mac"] = activeSessions[i].mac;
    s["ip"] = activeSessions[i].ip;
    s["txid"] = activeSessions[i].txid;
  }
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleOTA() {
  server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
  ESP.restart();
}

void handleOTAUpload() {
  HTTPUpload& upload = server.upload();
  static size_t total = 0;
  
  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("OTA Update: %s\n", upload.filename.c_str());
    total = 0;
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }
    total += upload.currentSize;
    Serial.printf("Progress: %d%%\r", (total * 100) / 4194304);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      Serial.printf("Success: %u bytes\n", total);
    } else {
      Update.printError(Serial);
    }
  }
}