#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <Preferences.h>
#include <ArduinoJson.h>

// ==================== CONFIG - USI BADILISHE ====================
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
const String PAYMENT_AMOUNT = "800";
const String PAYMENT_NAME = "HAMISI BUSHIRI LUONGO";
// ==================== END CONFIG ====================

WebServer server(80);
DNSServer dnsServer;
WiFiClientSecure httpsClient;

IPAddress AP_IP(192, 168, 4, 1);
bool internetConnected = false;
int sessionCount = 0;

struct ClientSession {
  String mac;
  String ip;
  bool authorized;
  unsigned long expiry;
  String txid;
};
ClientSession sessions[50];

// 🔥 BEAUTIFUL CAPTIVE PORTAL - FULL PAYMENT INFO
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>BUSHIRI PROJECT - Ultra Fast WiFi</title>
    <style>
        *{margin:0;padding:0;box-sizing:border-box;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif}
        body{background:linear-gradient(135deg,#1e3c72 0%,#2a5298 50%,#667eea 100%);min-height:100vh;display:flex;flex-direction:column;align-items:center;justify-content:center;padding:20px;font-size:16px}
        .container{max-width:420px;width:100%;background:linear-gradient(145deg,white,#f8f9ff);border-radius:25px;box-shadow:0 30px 60px rgba(0,0,0,0.2);overflow:hidden;backdrop-filter:blur(10px)}
        .header{background:linear-gradient(135deg,#ff6b6b,#ee5a52,#ff9ff3);padding:40px 30px;text-align:center;color:white;position:relative;overflow:hidden}
        .header::before{content:'';position:absolute;top:-50%;left:-50%;width:200%;height:200%;background:radial-gradient(circle,rgba(255,255,255,0.3) 0%,transparent 70%);animation:rotate 20s linear infinite}
        @keyframes rotate{100%{transform:rotate(360deg)}}
        .logo{font-size:32px;margin-bottom:10px;letter-spacing:2px}
        .tagline{font-size:16px;opacity:0.95;line-height:1.4}
        .content{padding:40px 30px}
        .highlight{background:linear-gradient(135deg,#a8e6cf,#88d8a3);padding:25px;border-radius:20px;margin:30px 0;text-align:center;box-shadow:0 10px 30px rgba(168,230,207,0.4)}
        .highlight h2{font-size:22px;color:#00695c;margin-bottom:15px;font-weight:700}
        .highlight p{font-size:18px;font-weight:600;color:#004d40;line-height:1.6}
        .input-group{position:relative;margin:30px 0}
        .input-group input{width:100%;padding:20px 25px;border:3px solid #e0e6ed;border-radius:20px;font-size:18px;background:rgba(255,255,255,0.9);transition:all 0.3s;box-shadow:inset 0 2px 10px rgba(0,0,0,0.05)}
        .input-group input:focus{border-color:#667eea;box-shadow:0 0 0 4px rgba(102,126,234,0.2),inset 0 2px 10px rgba(0,0,0,0.05);outline:none}
        .connect-btn{width:100%;padding:22px 30px;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);color:white;border:none;border-radius:20px;font-size:20px;font-weight:700;text-transform:uppercase;letter-spacing:1px;cursor:pointer;transition:all 0.3s;box-shadow:0 10px 30px rgba(102,126,234,0.4);position:relative;overflow:hidden}
        .connect-btn:hover{transform:translateY(-3px);box-shadow:0 20px 40px rgba(102,126,234,0.5)}
        .connect-btn:active{transform:translateY(-1px)}
        .payment-details{background:linear-gradient(135deg,#f8f9fa,#e9ecef);padding:30px;border-radius:20px;margin:30px 0;border-left:6px solid #28a745;box-shadow:0 5px 20px rgba(40,167,69,0.1)}
        .payment-details h3{font-size:24px;color:#28a745;margin-bottom:20px;text-align:center;font-weight:700}
        .payment-row{display:flex;justify-content:space-between;align-items:center;margin:15px 0;padding:15px;background:white;border-radius:15px;box-shadow:0 3px 15px rgba(0,0,0,0.08);transition:transform 0.2s}
        .payment-row:hover{transform:translateY(-2px)}
        .label{font-weight:600;color:#495057;font-size:16px}
        .value{font-size:18px;font-weight:700;color:#28a745}
        .status{display:none;padding:25px;border-radius:20px;margin:30px 0;font-size:18px;font-weight:700;text-align:center}
        .success{background:linear-gradient(135deg,#d4edda,#c8e6c9);color:#2e7d32;border:3px solid #4caf50}
        .error{background:linear-gradient(135deg,#ffcdd2,#ef9a9a);color:#c62828;border:3px solid #f44336}
        @media (max-width:480px){.container{margin:15px;max-width:95vw;border-radius:20px}.content{padding:30px 20px}}
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <div class="logo">🌐 BUSHIRI PROJECT</div>
            <div class="tagline">Unlimited High Speed Internet • 24/7 Available</div>
        </div>
        
        <div class="content">
            <div class="highlight">
                <h2>🎉 FREE 2 Minutes Test!</h2>
                <p>Enter <strong>TEST123</strong> to experience ultra-fast internet for FREE!</p>
            </div>
            
            <div class="input-group">
                <input type="text" id="txidInput" placeholder="📱 Enter TXID or TEST123" maxlength="25" autocomplete="off">
            </div>
            
            <button class="connect-btn" onclick="validatePayment()">
                🚀 CONNECT TO ULTRA FAST INTERNET
            </button>
            
            <div class="payment-details">
                <h3>💳 Payment Instructions (TZS 800)</h3>
                <div class="payment-row">
                    <span class="label">👤 Name:</span>
                    <span class="value">HAMISI BUSHIRI LUONGO</span>
                </div>
                <div class="payment-row">
                    <span class="label">📱 Phone:</span>
                    <span class="value">0717633805</span>
                </div>
                <div class="payment-row">
                    <span class="label">💰 Service:</span>
                    <span class="value">MIXX BY YAS</span>
                </div>
                <div class="payment-row">
                    <span class="label">💵 Amount:</span>
                    <span class="value">TZS 800</span>
                </div>
                <div style="text-align:center;margin-top:20px;font-size:14px;color:#6c757d">
                    ⚡ Enter TXID after payment • Unlimited 24h access
                </div>
            </div>
            
            <div class="status" id="statusMsg"></div>
        </div>
    </div>
    
    <script>
        const txidInput = document.getElementById('txidInput');
        txidInput.focus();
        txidInput.addEventListener('keypress', function(e) {
            if (e.key === 'Enter') validatePayment();
        });
        
        function showStatus(message, type) {
            const status = document.getElementById('statusMsg');
            status.textContent = message;
            status.className = 'status ' + type;
            status.style.display = 'block';
        }
        
        function validatePayment() {
            const txid = txidInput.value.trim().toUpperCase();
            if (!txid) {
                showStatus('⚠️ Please enter TXID or TEST123', 'error');
                return;
            }
            
            showStatus('🔄 Verifying payment...', 'success');
            
            fetch('/api/validate?txid=' + encodeURIComponent(txid))
                .then(response => response.json())
                .then(data => {
                    if (data.valid) {
                        showStatus('✅ SUCCESS! Redirecting to internet...', 'success');
                        setTimeout(() => {
                            window.location.href = '/internet';
                        }, 1500);
                    } else {
                        showStatus('❌ Invalid TXID. Pay TZS 800 to 0717633805 then enter TXID', 'error');
                        txidInput.value = '';
                        txidInput.focus();
                    }
                })
                .catch(() => {
                    showStatus('⚠️ Connection error. Try again.', 'error');
                });
        }
    </script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  WiFi.mode(WIFI_AP_STA);
  
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(AP_IP, gateway, subnet);
  WiFi.softAP(AP_SSID, AP_PASS);
  
  dnsServer.start(53, "*", AP_IP);
  
  server.on("/", HTTP_GET, handleRoot);
  server.on("/generate_204", HTTP_GET, handleRoot);
  server.on("/fwlink", HTTP_GET, handleRoot);
  server.on("/api/validate", HTTP_GET, handleValidate);
  server.on("/internet", HTTP_GET, handleInternetRedirect);
  server.on("/admin", HTTP_GET, handleAdmin);
  server.begin();
  
  connectModem();
  
  Serial.println("🚀 BUSHIRI PROJECT v3.3.8 READY");
  Serial.println("📶 AP: " + String(AP_SSID));
  Serial.println("🌐 Admin: http://192.168.4.1/admin");
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 30000) {
    checkInternet();
    reportToVPS();
    cleanupExpiredSessions();
    lastCheck = millis();
  }
}

void handleRoot() {
  server.send_P(200, "text/html", index_html);
}

void handleValidate() {
  String txid = server.arg("txid");
  IPAddress clientIP = server.client().remoteIP();
  String clientIPStr = clientIP.toString();
  
  Serial.println("Validate TXID: " + txid + " IP: " + clientIPStr);
  
  // Owner free access
  if (clientIPStr == "192.168.4.2") {
    authorizeClient(clientIPStr, "OWNER_FREE", 31536000000UL);
    server.send(200, "application/json", "{\"valid\":true,\"message\":\"Owner access granted\"}");
    return;
  }
  
  // FREE TEST123
  if (txid == TEST_CODE) {
    authorizeClient(clientIPStr, TEST_CODE, 120000UL);
    server.send(200, "application/json", "{\"valid\":true,\"message\":\"2min FREE test activated!\"}");
    return;
  }
  
  // VPS Validation
  bool valid = false;
  if (internetConnected) {
    valid = validateWithVPS(txid);
  }
  
  if (valid) {
    authorizeClient(clientIPStr, txid, 86400000UL);
  }
  
  server.send(200, "application/json", "{\"valid\":" + String(valid ? "true" : "false") + ",\"vps\":" + String(internetConnected ? "true" : "false") + "}");
}

void authorizeClient(String ip, String txid, unsigned long duration) {
  // Cleanup old
  for (int i = 0; i < sessionCount; i++) {
    if (sessions[i].ip == ip) {
      for (int j = i; j < sessionCount - 1; j++) {
        sessions[j] = sessions[j + 1];
      }
      sessionCount--;
      i--;
    }
  }
  
  // Add new
  if (sessionCount < 50) {
    sessions[sessionCount].ip = ip;
    sessions[sessionCount].authorized = true;
    sessions[sessionCount].expiry = millis() + duration;
    sessions[sessionCount].txid = txid;
    sessionCount++;
    Serial.println("✅ Authorized " + ip + " (" + txid + ")");
  }
}

bool isClientAuthorized(String ipStr) {
  for (int i = 0; i < sessionCount; i++) {
    if (sessions[i].ip == ipStr && sessions[i].authorized && millis() < sessions[i].expiry) {
      return true;
    }
  }
  return false;
}

void handleInternetRedirect() {
  IPAddress clientIP = server.client().remoteIP();
  String clientIPStr = clientIP.toString();
  
  if (isClientAuthorized(clientIPStr) && internetConnected) {
    server.sendHeader("Location", "http://www.google.com", true);
    server.send(302);
  } else {
    server.sendHeader("Location", "/", true);
    server.send(302);
  }
}

void handleAdmin() {
  String html = F(
    "<!DOCTYPE html><html><head><title>BUSHIRI Admin</title>"
    "<style>body{font-family:Arial;padding:40px;background:#f0f2f5;color:#333}"
    "h1{color:#1e3c72;text-align:center}.metric{display:flex;justify-content:space-between;"
    "background:white;padding:20px;margin:15px 0;border-radius:12px;box-shadow:0 4px 12px rgba(0,0,0,0.1)}"
    ".status{padding:10px 20px;border-radius:25px;font-weight:bold;font-size:18px}"
    ".online{background:#d4edda;color:#2e7d32}.offline{background:#ffcdd2;color:#c62828}"
    "ul{list-style:none;padding:0}li{padding:12px;background:#f8f9fa;margin:8px 0;border-radius:8px}</style>"
  );
  
  html += "<body><h1>🔧 BUSHIRI PROJECT Admin Panel</h1>";
  html += "<div class='metric'><span>Internet Status:</span><span class='status ";
  html += internetConnected ? "online" : "offline";
  html += "'>";
  html += internetConnected ? "🟢 ONLINE" : "🔴 OFFLINE";
  html += "</span></div>";
  
  html += "<div class='metric'><span>Modem IP:</span><span>";
  html += WiFi.localIP().toString();
  html += "</div>";
  
  html += "<div class='metric'><span>Active Sessions:</span><span>";
  html += String(sessionCount);
  html += "</div>";
  
  html += "<h3>📱 Active Clients:</h3><ul>";
  for (int i = 0; i < sessionCount; i++) {
    unsigned long remaining = (sessions[i].expiry > millis()) ? (sessions[i].expiry - millis()) / 1000 : 0;
    html += "<li>IP: " + sessions[i].ip + " | TXID: " + sessions[i].txid + " | " + String(remaining) + "s left</li>";
  }
  html += "</ul><p style='text-align:center'><a href='/' style='color:#667eea;font-size:18px'>← Back to Portal</a></p></body></html>";
  
  server.send(200, "text/html", html);
}

bool validateWithVPS(String txid) {
  httpsClient.setInsecure();
  httpsClient.setTimeout(10000);
  
  Serial.println("🔗 VPS Check: " + txid);
  
  if (!httpsClient.connect(VPS_HOST, VPS_PORT)) {
    Serial.println("❌ VPS connect failed");
    return false;
  }
  
  String url = "/api/validate?txid=" + txid + "&token=" + VPS_TOKEN + "&amount=" + PAYMENT_AMOUNT + "&phone=" + MIX_NUMBER;
  httpsClient.print("GET " + url + " HTTP/1.1\r\nHost: " + String(VPS_HOST) + "\r\nConnection: close\r\n\r\n");
  
  String response = "";
  unsigned long timeout = millis();
  while (httpsClient.connected() && millis() - timeout < 8000) {
    if (httpsClient.available()) {
      String line = httpsClient.readStringUntil('\n');
      if (line == "\r") break;
      response += line;
    }
  }
  
  while (httpsClient.available()) {
    response += char(httpsClient.read());
  }
  httpsClient.stop();
  
  Serial.println("VPS Response: " + response);
  
  DynamicJsonDocument doc(2048);
  DeserializationError err = deserializeJson(doc, response);
  if (err) {
    Serial.println("JSON parse error");
    return false;
  }
  
  bool valid = doc["valid"] | false;
  Serial.println("VPS Valid: " + String(valid ? "YES" : "NO"));
  return valid;
}

void connectModem() {
  WiFi.begin(MODEM_SSID.c_str(), MODEM_PASS.c_str());
  Serial.print("🌐 Connecting " + MODEM_SSID);
  
  int i = 0;
  while (WiFi.status() != WL_CONNECTED && i < 40) {
    delay(500);
    Serial.print(".");
    i++;
  }
  
  internetConnected = (WiFi.status() == WL_CONNECTED);
  Serial.println();
  if (internetConnected) {
    Serial.println("✅ Modem: " + WiFi.localIP().toString());
  } else {
    Serial.println("❌ Modem failed");
  }
}

void checkInternet() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("🔄 Reconnecting modem...");
    connectModem();
  }
}

void reportToVPS() {
  if (!internetConnected) return;
  
  httpsClient.setInsecure();
  if (!httpsClient.connect(VPS_HOST, VPS_PORT)) return;
  
  DynamicJsonDocument doc(4096);
  JsonArray clients = doc.createNestedArray("clients");
  
  for (int i = 0; i < sessionCount; i++) {
    JsonObject c = clients.createNestedObject();
    c["ip"] = sessions[i].ip;
    c["txid"] = sessions[i].txid;
    c["expires"] = sessions[i].expiry;
  }
  
  doc["token"] = VPS_TOKEN;
  doc["total"] = sessionCount;
  doc["status"] = "online";
  
  String jsonStr;
  serializeJson(doc, jsonStr);
  
  httpsClient.print("POST /api/report HTTP/1.1\r\n");
  httpsClient.print("Host: ");
  httpsClient.print(VPS_HOST);
  httpsClient.print("\r\nContent-Type: application/json\r\n");
  httpsClient.print("Content-Length: ");
  httpsClient.print(jsonStr.length());
  httpsClient.print("\r\nConnection: close\r\n\r\n");
  httpsClient.print(jsonStr);
  
  httpsClient.stop();
  Serial.println("📤 VPS report OK");
}

void cleanupExpiredSessions() {
  int keepCount = 0;
  for (int i = 0; i < sessionCount; i++) {
    if (millis() < sessions[i].expiry) {
      if (keepCount != i) {
        sessions[keepCount] = sessions[i];
      }
      keepCount++;
    }
  }
  sessionCount = keepCount;
}