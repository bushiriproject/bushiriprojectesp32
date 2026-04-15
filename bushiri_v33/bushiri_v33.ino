#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <ESP32httpUpdate.h>
#include <esp_wifi.h>
#include <lwip/napt.h>
#include <lwip/inet.h>

// === CONFIGURATION ===
const char* ownerMAC = "bc:90:63:a2:32:83";  // SET YOUR MAC HERE (replace XX:XX:XX)
const char* adminPassword = "Kibushi1";
const char* apSSID = "BUSHIRI";
const char* apPassword =";

const char* staSSID = "PASSWORD";     // CHANGE IN ADMIN
const char* staPassword = "AMUDUH123"; // CHANGE IN ADMIN

IPAddress AP_IP_ADDR(192, 168, 4, 1);
IPAddress AP_GATEWAY(192, 168, 4, 1);
IPAddress AP_SUBNET(255, 255, 255, 0);
IPAddress DNS_IP(8, 8, 8, 8);

// === GLOBAL STATE ===
WebServer server(80);
bool naptEnabled = false;
bool wifiConnected = false;
String currentSSID = staSSID;
String currentPass = staPassword;

struct ClientInfo {
  String mac;
  bool authorized;
  unsigned long expiry;
};
ClientInfo clients[50];
int clientCount = 0;

// === HTML PAGES ===
const char portalPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>BUSHIRI WiFi - Lipa MIXX</title>
    <style>
        * { margin:0; padding:0; box-sizing:border-box; }
        body { 
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            height: 100vh; display:flex; align-items:center; justify-content:center;
            color: white; overflow: hidden;
        }
        .container { 
            background: rgba(255,255,255,0.1); backdrop-filter: blur(20px);
            border-radius: 20px; padding: 40px; max-width: 400px; width: 90%;
            box-shadow: 0 20px 40px rgba(0,0,0,0.2); text-align: center;
            animation: fadeIn 1s ease-out;
        }
        @keyframes fadeIn { from { opacity:0; transform:translateY(30px); } to { opacity:1; transform:translateY(0); } }
        h1 { font-size: 2.2em; margin-bottom: 10px; background: linear-gradient(45deg, #fff, #f0f0f0); -webkit-background-clip: text; -webkit-text-fill-color: transparent; }
        .price { font-size: 3em; font-weight: bold; margin: 20px 0; background: linear-gradient(45deg, #FFD700, #FFA500); -webkit-background-clip: text; -webkit-text-fill-color: transparent; }
        .instructions { background: rgba(255,255,255,0.2); border-radius: 15px; padding: 25px; margin: 25px 0; line-height: 1.6; text-align: left; }
        .instructions h3 { color: #FFD700; margin-bottom: 15px; text-align: center; }
        .instructions p { margin: 8px 0; font-size: 1.1em; }
        input[type="text"] { width: 100%; padding: 15px; margin: 15px 0; border: none; border-radius: 12px; font-size: 16px; background: rgba(255,255,255,0.9); }
        button { width: 100%; padding: 18px; margin: 10px 0; border: none; border-radius: 12px; font-size: 1.2em; font-weight: bold; 
                 background: linear-gradient(45deg, #4CAF50, #45a049); color: white; cursor: pointer; transition: all 0.3s; }
        button:hover { transform: translateY(-2px); box-shadow: 0 10px 25px rgba(76,175,80,0.4); }
        button:disabled { background: #666; cursor: not-allowed; transform: none; }
        .test123 { background: linear-gradient(45deg, #2196F3, #1976D2) !important; margin-top: 20px; }
        .status { padding: 15px; margin: 15px 0; border-radius: 10px; font-weight: bold; }
        .success { background: rgba(76,175,80,0.3); }
        .error { background: rgba(244,67,54,0.3); }
        .loading { display: none; text-align: center; margin: 20px 0; }
        .spinner { border: 4px solid rgba(255,255,255,0.3); border-top: 4px solid white; border-radius: 50%; width: 40px; height: 40px; animation: spin 1s linear infinite; margin: 0 auto; }
        @keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }
    </style>
</head>
<body>
    <div class="container">
        <h1>🌐 BUSHIRI WiFi</h1>
        <div class="price">TZS 800</div>
        <div class="instructions">
            <h3>📱 Malipo ya MIXX</h3>
            <p><strong>1.</strong> Fungua <strong>MIXX BY YAS</strong> app</p>
            <p><strong>2.</strong> Chagua <strong>Lipa na M-PESA/STK</strong></p>
            <p><strong>3.</strong> Lipa <strong>TZS 800</strong> kwenda</p>
            <p><strong>📞 BUSHIRI namba 0717633805</strong></p>
            <p><strong>👤 Jina: HAMISI BUSHIRI LUONGO</strong></p>
            <p><strong>4.</strong> Tumia <strong>TXID</strong> hapa chini</p>
        </div>
        <input type="text" id="txid" placeholder="Ingiza TXID yako (mfano: TEST123)" maxlength="20">
        <button onclick="verifyTXID()">Lipa na Ungana</button>
        <button class="test123" onclick="verifyTXID('TEST123')">🧪 TEST - Dakika 5 BURE</button>
        <div id="status"></div>
        <div class="loading" id="loading"><div class="spinner"></div><p>Kinaangalia malipo...</p></div>
    </div>
    <script>
        function showStatus(msg, type) {
            const status = document.getElementById('status');
            status.textContent = msg;
            status.className = 'status ' + type;
        }
        function showLoading(show) {
            document.getElementById('loading').style.display = show ? 'block' : 'none';
            document.querySelectorAll('button').forEach(b => b.disabled = show);
        }
        async function verifyTXID(txid = document.getElementById('txid').value.trim()) {
            if (!txid) { showStatus('Tafadhali ingiza TXID', 'error'); return; }
            showLoading(true);
            try {
                const res = await fetch('/verify?txid=' + encodeURIComponent(txid));
                const data = await res.json();
                if (data.success) {
                    showStatus('✅ Malipo yameshinda! Una internet saa 15', 'success');
                    setTimeout(() => { window.location.href = 'http://google.com'; }, 2000);
                } else {
                    showStatus('❌ TXID si sahihi. Jaribu tena.', 'error');
                }
            } catch(e) {
                showStatus('⚠️ Tatizo la mtandao. Jaribu tena.', 'error');
            }
            showLoading(false);
        }
        document.getElementById('txid').addEventListener('keypress', function(e) {
            if (e.key === 'Enter') verifyTXID();
        });
    </script>
</body>
</html>
)rawliteral";

const char adminPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width">
    <title>BUSHIRI Admin Panel</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #f5f5f5; }
        .card { background: white; padding: 25px; margin: 20px 0; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
        h1 { color: #333; }
        table { width: 100%; border-collapse: collapse; margin: 20px 0; }
        th, td { padding: 12px; text-align: left; border-bottom: 1px solid #ddd; }
        th { background: #4CAF50; color: white; }
        .online { color: #4CAF50; font-weight: bold; }
        .expired { color: #f44336; }
        input, button { padding: 10px; margin: 5px; border: 1px solid #ddd; border-radius: 5px; }
        button { background: #4CAF50; color: white; border: none; cursor: pointer; }
        button:hover { background: #45a049; }
        .ota { background: #2196F3; }
        .ota:hover { background: #1976D2; }
        .stats { display: flex; gap: 20px; flex-wrap: wrap; }
        .stat { background: #e3f2fd; padding: 15px; border-radius: 8px; text-align: center; flex: 1; min-width: 120px; }
    </style>
</head>
<body>
    <div class="card">
        <h1>🔧 BUSHIRI Admin Panel</h1>
        <div class="stats">
            <div class="stat"><strong id="totalClients">0</strong><br>Jamii ya wagonjwa</div>
            <div class="stat"><strong id="onlineClients">0</strong><br>Online</div>
            <div class="stat"><strong id="wifiStatus"></strong><br>WiFi Modem</div>
        </div>
    </div>
    
    <div class="card">
        <h2>📶 WiFi Modem Config</h2>
        <input type="text" id="newSSID" placeholder="New Modem SSID" value="">
        <input type="password" id="newPass" placeholder="New Modem Password" value="">
        <button onclick="updateWiFi()">💾 Save WiFi Config</button>
        <p id="wifiMsg"></p>
    </div>
    
    <div class="card">
        <h2>👥 Wagonjwa Online</h2>
        <table id="clientsTable">
            <thead><tr><th>MAC</th><th>Status</th><th>Expired</th></tr></thead>
            <tbody></tbody>
        </table>
        <button onclick="clearAll()">🗑️ Futa Wote</button>
    </div>
    
    <div class="card">
        <h2>🔄 OTA Update</h2>
        <p>URL ya firmware mpya:</p>
        <input type="text" id="otaUrl" placeholder="https://your-server.com/firmware.bin" style="width: 70%;">
        <button class="ota" onclick="doOTA()">🚀 Update Firmware</button>
    </div>

    <script>
        async function loadData() {
            const res = await fetch('/admin/data');
            const data = await res.json();
            
            document.getElementById('totalClients').textContent = data.total;
            document.getElementById('onlineClients').textContent = data.online;
            document.getElementById('wifiStatus').textContent = data.wifi ? '✅ ' + data.wifiSSID : '❌ Hakuna';
            
            const tbody = document.querySelector('#clientsTable tbody');
            tbody.innerHTML = '';
            data.clients.forEach(c => {
                const row = tbody.insertRow();
                row.innerHTML = `<td>${c.mac}</td><td>${c.authorized ? (Date.now() > c.expiry ? '⏰ Imekwisha' : '✅ Online') : '❌ Hakuna malipo'}</td><td>${new Date(c.expiry).toLocaleString()}</td>`;
            });
        }
        
        async function updateWiFi() {
            const ssid = document.getElementById('newSSID').value;
            const pass = document.getElementById('newPass').value;
            const res = await fetch('/admin/wifi', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ssid, pass})
            });
            const msg = await res.text();
            document.getElementById('wifiMsg').textContent = msg;
            loadData();
        }
        
        async function clearAll() {
            await fetch('/admin/clear', {method: 'POST'});
            loadData();
        }
        
        async function doOTA() {
            const url = document.getElementById('otaUrl').value;
            if (!url) return alert('Ingiza OTA URL');
            if (!confirm('Una uhakika?')) return;
            window.location.href = '/update?url=' + encodeURIComponent(url);
        }
        
        setInterval(loadData, 5000);
        loadData();
    </script>
</body>
</html>
)rawliteral";

// === FUNCTIONS ===
void setupNAT() {
  if (!naptEnabled) {
    esp_ip4_addr_t ip;
    ip.addr = static_cast<uint32_t>(AP_IP_ADDR);
    ip_napt_enable(1, 32, &ip);
    Serial.println("✅ NAT enabled");
    naptEnabled = true;
  }
}

void disableNAT() {
  if (naptEnabled) {
    ip_napt_disable();
    Serial.println("❌ NAT disabled");
    naptEnabled = false;
  }
}

String getClientMAC() {
  uint8_t mac[6];
  WiFi.softAPgetStationNum();
  WiFi.softAPgetStationList();
  wifi_sta_list_t sta_list;
  esp_wifi_ap_get_sta_list(&sta_list);
  if (sta_list.num > 0) {
    for (int i = 0; i < 6; i++) {
      mac[i] = sta_list.sta[i].mac[i];
    }
    char macStr[18];
    sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(macStr);
  }
  return "";
}

bool isOwnerMAC(String mac) {
  return mac == ownerMAC;
}

bool isAuthorized(String mac) {
  for (int i = 0; i < clientCount; i++) {
    if (clients[i].mac == mac) {
      return clients[i].authorized && millis() < clients[i].expiry;
    }
  }
  return false;
}

void authorizeClient(String mac, String txid) {
  for (int i = 0; i < clientCount; i++) {
    if (clients[i].mac == mac) {
      clients[i].authorized = true;
      clients[i].expiry = millis() + (txid == "TEST123" ? 5 * 60 * 1000UL : 15 * 60 * 60 * 1000UL);
      return;
    }
  }
  if (clientCount < 50) {
    clients[clientCount].mac = mac;
    clients[clientCount].authorized = true;
    clients[clientCount].expiry = millis() + (txid == "TEST123" ? 5 * 60 * 1000UL : 15 * 60 * 60 * 1000UL);
    clientCount++;
  }
}

void handleCaptivePortal() {
  String mac = getClientMAC();
  Serial.printf("Captive request from %s\n", mac.c_str());
  
  if (isOwnerMAC(mac) || isAuthorized(mac)) {
    server.sendHeader("Location", "http://www.google.com", true);
    server.send(302, "text/plain", "");
    return;
  }
  
  server.send(200, "text/html", portalPage);
}

void setup() {
  Serial.begin(115200);
  
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(AP_IP_ADDR, AP_GATEWAY, AP_SUBNET);
  WiFi.softAP(apSSID, apPassword);
  
  delay(1000);
  setupNAT();
  
  // Captive portals
  server.on("/generate_204", handleCaptivePortal);
  server.on("/gen_204", handleCaptivePortal);
  server.on("/hotspot-detect.html", handleCaptivePortal);
  server.on("/connecttest.txt", handleCaptivePortal);
  server.on("/fwlink", handleCaptivePortal);
  
  // Payment verification
  server.on("/verify", []() {
    String txid = server.arg("txid");
    String mac = getClientMAC();
    Serial.printf("Verify TXID %s for %s\n", txid.c_str(), mac.c_str());
    
    DynamicJsonDocument doc(1024);
    if (txid == "TEST123") {
      authorizeClient(mac, txid);
      doc["success"] = true;
      doc["message"] = "TEST access granted - 5 minutes";
    } else {
      doc["success"] = false;
      doc["message"] = "Verify TEST123 or real TXID on VPS";
    }
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  });
  
  // Admin
  server.on("/admin", []() {
    if (!server.authenticate("admin", adminPassword)) {
      return server.requestAuthentication();
    }
    server.send(200, "text/html", adminPage);
  });
  
  server.on("/admin/data", []() {
    if (!server.authenticate("admin", adminPassword)) return;
    
    DynamicJsonDocument doc(4096);
    doc["total"] = clientCount;
    doc["online"] = 0;
    doc["wifi"] = wifiConnected ? currentSSID : "";
    JsonArray clientsArray = doc.createNestedArray("clients");
    
    for (int i = 0; i < clientCount; i++) {
      DynamicJsonDocument clientDoc(512);
      clientDoc["mac"] = clients[i].mac;
      clientDoc["authorized"] = clients[i].authorized;
      clientDoc["expiry"] = clients[i].expiry;
      clientsArray.add(clientDoc);
      
      if (clients[i].authorized && millis() < clients[i].expiry) doc["online"]++;
    }
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  });
  
  server.on("/admin/wifi", []() {
    if (!server.authenticate("admin", adminPassword)) return;
    if (server.hasArg("plain")) {
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, server.arg("plain"));
      currentSSID = doc["ssid"].as<String>();
      currentPass = doc["pass"].as<String>();
      connectWiFi();
      
      server.send(200, "text/plain", "WiFi config saved: " + currentSSID);
    }
  }, HTTP_POST, [](){}, [](){
    server.send(200, "text/plain", "OK");
  });
  
  server.on("/admin/clear", []() {
    if (!server.authenticate("admin", adminPassword)) return;
    clientCount = 0;
    server.send(200, "text/plain", "All clients cleared");
  }, HTTP_POST);
  
  // OTA
  server.on("/update", []() {
    String url = server.arg("url");
    if (url.length() > 0) {
      Serial.println("Starting OTA from: " + url);
      ESPhttpUpdate.rebootOnUpdate(false);
      t_httpUpdate_return ret = ESPhttpUpdate.update(url);
      if (ret == HTTP_UPDATE_OK) {
        Serial.println("OTA done");
        ESP.restart();
      }
    }
    server.send(200, "text/html", adminPage);
  });
  
  server.onNotFound(handleCaptivePortal);
  server.begin();
  
  connectWiFi();
  MDNS.begin("bushiri");
}

void loop() {
  server.handleClient();
  
  // Cleanup expired
  for (int i = 0; i < clientCount; i++) {
    if (clients[i].authorized && millis() > clients[i].expiry) {
      clients[i].authorized = false;
    }
  }
  
  // WiFi reconnect
  if (WiFi.status() != WL_CONNECTED && millis() % 30000 < 1000) {
    connectWiFi();
  }
  delay(10);
}

void connectWiFi() {
  WiFi.disconnect();
  delay(1000);
  WiFi.begin(currentSSID.c_str(), currentPass.c_str());
  Serial.printf("Connecting to %s...\n", currentSSID.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    attempts++;
    Serial.print(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("\n✅ WiFi connected: " + WiFi.localIP().toString());
    setupNAT();
  } else {
    wifiConnected = false;
    Serial.println("\n❌ WiFi failed");
  }
}