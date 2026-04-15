#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <esp_wifi.h>

// === CONFIGURATION ===
const char* ownerMAC = "bc:90:63:a2:32:83";  // 🔧 YOUR MAC HERE
const char* adminPassword = "Kibushi1";
const char* apSSID = "BUSHIRI";
const char* apPassword = "12345678";

const char* staSSID = "PATAHUDUMA";
const char* staPassword = "AMUDUH123";

IPAddress AP_IP_ADDR(192, 168, 4, 1);
IPAddress AP_GATEWAY(192, 168, 4, 1);
IPAddress AP_SUBNET(255, 255, 255, 0);

// === GLOBAL STATE ===
WebServer server(80);
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

// === BEAUTIFUL PORTAL ===
const char portalPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>BUSHIRI WiFi - Lipa MIXX</title>
    <style>
        *{margin:0;padding:0;box-sizing:border-box;}
        body{font-family:system-ui;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);height:100vh;display:flex;align-items:center;justify-content:center;color:white;overflow:hidden;}
        .container{background:rgba(255,255,255,0.1);backdrop-filter:blur(20px);border-radius:20px;padding:40px;max-width:400px;width:90%;box-shadow:0 20px 40px rgba(0,0,0,0.2);text-align:center;animation:fadeIn 1s ease-out;}
        @keyframes fadeIn{from{opacity:0;transform:translateY(30px);}to{opacity:1;transform:translateY(0);}}
        h1{font-size:2.2em;margin-bottom:10px;background:linear-gradient(45deg,#fff,#f0f0f0);-webkit-background-clip:text;-webkit-text-fill-color:transparent;}
        .price{font-size:3em;font-weight:bold;margin:20px 0;background:linear-gradient(45deg,#FFD700,#FFA500);-webkit-background-clip:text;-webkit-text-fill-color:transparent;}
        .instructions{background:rgba(255,255,255,0.2);border-radius:15px;padding:25px;margin:25px 0;line-height:1.6;text-align:left;}
        .instructions h3{color:#FFD700;margin-bottom:15px;text-align:center;}
        .instructions p{margin:8px 0;font-size:1.1em;}
        input[type="text"]{width:100%;padding:15px;margin:15px 0;border:none;border-radius:12px;font-size:16px;background:rgba(255,255,255,0.9);}
        button{width:100%;padding:18px;margin:10px 0;border:none;border-radius:12px;font-size:1.2em;font-weight:bold;background:linear-gradient(45deg,#4CAF50,#45a049);color:white;cursor:pointer;transition:all 0.3s;}
        button:hover{transform:translateY(-2px);box-shadow:0 10px 25px rgba(76,175,80,0.4);}
        button:disabled{background:#666;cursor:not-allowed;transform:none;}
        .test123{background:linear-gradient(45deg,#2196F3,#1976D2)!important;margin-top:20px;}
        .status{padding:15px;margin:15px 0;border-radius:10px;font-weight:bold;}
        .success{background:rgba(76,175,80,0.3);}
        .error{background:rgba(244,67,54,0.3);}
        .loading{display:none;text-align:center;margin:20px 0;}
        .spinner{border:4px solid rgba(255,255,255,0.3);border-top:4px solid white;border-radius:50%;width:40px;height:40px;animation:spin 1s linear infinite;margin:0 auto;}
        @keyframes spin{0%{transform:rotate(0deg);}100%{transform:rotate(360deg);}}
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
            <p><strong>📞 0717633805 BUSHIRI</strong></p>
            <p><strong>👤 HAMISI BUSHIRI LUONGO</strong></p>
            <p><strong>4.</strong> Tumia <strong>TXID</strong> hapa chini</p>
        </div>
        <input type="text" id="txid" placeholder="TXID (TEST123)" maxlength="20">
        <button onclick="verifyTXID()">💳 Lipa na Ungana</button>
        <button class="test123" onclick="verifyTXID('TEST123')">🧪 TEST - Dakika 2 BURE</button>
        <div id="status"></div>
        <div class="loading" id="loading"><div class="spinner"></div><p>Kinaangalia...</p></div>
    </div>
    <script>
        function showStatus(msg,type){document.getElementById('status').innerHTML=msg;document.getElementById('status').className='status '+type;}
        function showLoading(show){document.getElementById('loading').style.display=show?'block':'none';document.querySelectorAll('button').forEach(b=>b.disabled=show);}
        async function verifyTXID(txid=document.getElementById('txid').value.trim()){if(!txid){showStatus('Ingiza TXID','error');return;}showLoading(true);try{const res=await fetch('/verify?txid='+encodeURIComponent(txid));const data=await res.json();if(data.success){showStatus('✅ Malipo OK! Internet saa 15','success');setTimeout(()=>{window.location.href='http://google.com';},1500);}else{showStatus('❌ TXID batili','error');}}catch(e){showStatus('⚠️ Jaribu tena','error');}showLoading(false);}
        document.getElementById('txid').addEventListener('keypress',e=>e.key==='Enter'?verifyTXID():0);
    </script>
</body>
</html>
)rawliteral";

const char adminPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>BUSHIRI Admin</title><style>body{font-family:Arial;margin:20px;background:#f5f5f5;}.card{background:white;padding:25px;margin:20px 0;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1);}h1{color:#333;}table{width:100%;border-collapse:collapse;margin:20px 0;}th,td{padding:12px;text-align:left;border-bottom:1px solid #ddd;}th{background:#4CAF50;color:white;}.online{color:#4CAF50;font-weight:bold;}.expired{color:#f44336;}input,button{padding:10px;margin:5px;border:1px solid #ddd;border-radius:5px;}button{background:#4CAF50;color:white;border:none;cursor:pointer;}button:hover{background:#45a049;}.stats{display:flex;gap:20px;flex-wrap:wrap;}.stat{background:#e3f2fd;padding:15px;border-radius:8px;text-align:center;flex:1;min-width:120px;}</style></head><body><div class="card"><h1>🔧 BUSHIRI Admin</h1><div class="stats"><div class="stat"><strong id="totalClients">0</strong><br>Clients</div><div class="stat"><strong id="onlineClients">0</strong><br>Online</div><div class="stat"><strong id="wifiStatus"></strong><br>Modem</div></div></div><div class="card"><h2>📶 WiFi Config</h2><input type="text" id="newSSID" placeholder="Modem SSID"><input type="password" id="newPass" placeholder="Password"><button onclick="updateWiFi()">💾 Save</button><p id="wifiMsg"></p></div><div class="card"><h2>👥 Clients</h2><table id="clientsTable"><thead><tr><th>MAC</th><th>Status</th><th>Expires</th></tr></thead><tbody></tbody></table><button onclick="clearAll()">🗑️ Clear All</button></div><script>async function loadData(){const res=await fetch('/admin/data');const data=await res.json();document.getElementById('totalClients').textContent=data.total;document.getElementById('onlineClients').textContent=data.online;document.getElementById('wifiStatus').textContent=data.wifi?'✅ '+data.wifiSSID:'❌ Offline';const tbody=document.querySelector('#clientsTable tbody');tbody.innerHTML='';data.clients.forEach(c=>{const row=tbody.insertRow();row.innerHTML=`<td>${c.mac}</td><td>${c.authorized?(Date.now()>c.expiry?'⏰ Expired':'✅ Online'):'❌ No Pay'}</td><td>${new Date(c.expiry).toLocaleString()}</td>`;});}async function updateWiFi(){const ssid=document.getElementById('newSSID').value;const pass=document.getElementById('newPass').value;const res=await fetch('/admin/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid,pass})});document.getElementById('wifiMsg').textContent=await res.text();loadData();}async function clearAll(){await fetch('/admin/clear',{method:'POST'});loadData();}setInterval(loadData,5000);loadData();</script></body></html>
)rawliteral";

// === CORE FUNCTIONS ===
String getClientMAC() {
  wifi_sta_list_t sta_list;
  if (esp_wifi_ap_get_sta_list(&sta_list) == ESP_OK && sta_list.num > 0) {
    char macStr[18];
    sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
            sta_list.sta[0].mac[0], sta_list.sta[0].mac[1], sta_list.sta[0].mac[2],
            sta_list.sta[0].mac[3], sta_list.sta[0].mac[4], sta_list.sta[0].mac[5]);
    return String(macStr);
  }
  return "";
}

bool isOwnerMAC(String mac) { return mac == ownerMAC; }

bool isAuthorized(String mac) {
  for (int i = 0; i < clientCount; i++) {
    if (clients[i].mac == mac && clients[i].authorized && millis() < clients[i].expiry) {
      return true;
    }
  }
  return false;
}

void authorizeClient(String mac, bool isTest = false) {
  for (int i = 0; i < clientCount; i++) {
    if (clients[i].mac == mac) {
      clients[i].authorized = true;
      clients[i].expiry = millis() + (isTest ? 120000UL : 15*3600000UL); // 2min or 15hr
      return;
    }
  }
  if (clientCount < 50) {
    clients[clientCount].mac = mac;
    clients[clientCount].authorized = true;
    clients[clientCount].expiry = millis() + (isTest ? 120000UL : 15*3600000UL);
    clientCount++;
  }
}

void handleCaptivePortal() {
  String mac = getClientMAC();
  if (mac.length() > 0) Serial.printf("👤 %s\n", mac.c_str());
  
  if (isOwnerMAC(mac) || isAuthorized(mac)) {
    server.sendHeader("Location", "http://www.google.com/", true);
    server.send(302, "text/plain", "Redirecting...");
    return;
  }
  
  server.send(200, "text/html", portalPage);
}

void setup() {
  Serial.begin(115200);
  Serial.println("🚀 BUSHIRI v4.3.0 starting...");
  
  // AP + STA mode
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(AP_IP_ADDR, AP_GATEWAY, AP_SUBNET);
  WiFi.softAP(apSSID, apPassword);
  
  // Routes for internet sharing (NAT alternative)
  delay(2000);
  
  // Captive portals
  server.on("/generate_204", handleCaptivePortal);
  server.on("/gen_204", handleCaptivePortal);
  server.on("/hotspot-detect.html", handleCaptivePortal);
  server.on("/connecttest.txt", handleCaptivePortal);
  server.on("/fwlink", handleCaptivePortal);
  server.on("/", handleCaptivePortal);
  
  // Payment
  server.on("/verify", []() {
    String txid = server.arg("txid");
    String mac = getClientMAC();
    
    DynamicJsonDocument doc(512);
    if (txid == "TEST123") {
      authorizeClient(mac, true);
      doc["success"] = true;
      doc["minutes"] = 2;
    } else {
      doc["success"] = false;
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
  
  server.on("/admin/data", HTTP_GET, []() {
    if (!server.authenticate("admin", adminPassword)) return server.requestAuthentication();
    
    DynamicJsonDocument doc(2048);
doc["total"] = clientCount;

// hakikisha online inaanza 0 kisha ongeza
int onlineCount = doc["online"].is<int>() ? doc["online"].as<int>() : 0;
doc["online"] = onlineCount + 1;

doc["wifi"] = wifiConnected ? currentSSID.c_str() : "";
    
    JsonArray clientsList = doc.createNestedArray("clients");
    for (int i = 0; i < clientCount; i++) {
      JsonObject c = clientsList.createNestedObject();
      c["mac"] = clients[i].mac;
c["authorized"] = clients[i].authorized;
c["expiry"] = clients[i].expiry;

if (clients[i].authorized && millis() < clients[i].expiry) {
    doc["online"] = doc["online"].as<int>() + 1;
}
    
    String resp;
    serializeJson(doc, resp);
    server.send(200, "application/json", resp);
  });
  
  server.on("/admin/wifi", HTTP_POST, []() {
    if (!server.authenticate("admin", adminPassword)) return server.requestAuthentication();
    
    if (server.hasArg("plain")) {
      DynamicJsonDocument doc(256);
      DeserializationError error = deserializeJson(doc, server.arg("plain"));
      if (!error) {
        currentSSID = doc["ssid"].as<String>();
        currentPass = doc["pass"].as<String>();
        connectWiFi();
        server.send(200, "text/plain", "✅ Saved: " + currentSSID);
      } else {
        server.send(400, "text/plain", "JSON error");
      }
    }
  });
  
  server.on("/admin/clear", HTTP_POST, []() {
    if (!server.authenticate("admin", adminPassword)) return server.requestAuthentication();
    clientCount = 0;
    server.send(200, "text/plain", "✅ Cleared");
  });
  
  server.onNotFound(handleCaptivePortal);
  server.begin();
  
  connectWiFi();
  MDNS.begin("bushiri");
  Serial.println("✅ http://192.168.4.1/admin (Kibushi1)");
}

void loop() {
  server.handleClient();
  
  // Cleanup expired
  for (int i = 0; i < clientCount; i++) {
    if (clients[i].authorized && millis() > clients[i].expiry) {
      clients[i].authorized = false;
    }
  }
  
  delay(10);
}

void connectWiFi() {
  WiFi.disconnect();
  delay(1000);
  WiFi.begin(currentSSID.c_str(), currentPass.c_str());
  
  Serial.print("📶 " + currentSSID + "...");
  int i = 0;
  while (WiFi.status() != WL_CONNECTED && i++ < 20) {
    delay(500); Serial.print(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("\n✅ " + WiFi.localIP().toString());
  } else {
    wifiConnected = false;
    Serial.println("\n❌ Failed");
  }
}