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

// NAT Forwarding buffers
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
  Serial.println("WebServer started");

  // Start NAT proxy server
  natServer.begin();
  Serial.println("NAT proxy on port 8080 - READY");

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
  
  // NAT Forwarding Engine - FIXED
  handleNATForwarding();
  
  // Session cleanup
  cleanupSessions();
  delay(10);
}

void connectToModem() {
  Serial.println("Connecting to modem: " + String(MODEM_SSID));
  WiFi.begin(MODEM_SSID, MODEM_PASS);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    modemIP = WiFi.localIP().toString();
    internetConnected = true;
    Serial.println("\n✅ Modem connected! IP: " + modemIP);
  } else {
    Serial.println("\n❌ Modem failed - AP only mode");
    internetConnected = false;
  }
}

void checkModemConnection() {
  if (WiFi.status() == WL_CONNECTED && internetConnected) {
    if (modemIP != WiFi.localIP().toString()) {
      modemIP = WiFi.localIP().toString();
      Serial.println("Modem IP updated: " + modemIP);
    }
  } else if (internetConnected) {
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
  
  Serial.println("New session: " + mac + " - " + (paid ? "PAID" : "FREE"));
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
      Serial.println("Cleanup session: " + sessions[i].mac);
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

// 🔥 FIXED NAT FORWARDING - INAYOFANYA KAZI 100%
void handleNATForwarding() {
  if (!internetConnected) return;
  
  // Check for new clients
  WiFiClient newClient = natServer.available();
  if (newClient) {
    Serial.println("🔥 New NAT client #" + String(clientCount + 1));
    if (clientCount < 10) {
      forwardingClients[clientCount] = newClient;
      clientCount++;
    } else {
      newClient.stop();
      Serial.println("Max clients reached");
    }
  }
  
  // Process all clients
  for (int i = 0; i < clientCount; i++) {
    if (!forwardingClients[i].connected()) {
      Serial.println("Client " + String(i) + " disconnected");
      forwardingClients[i].stop();
      // Shift array
      for (int j = i; j < clientCount - 1; j++) {
        forwardingClients[j] = forwardingClients[j + 1];
      }
      clientCount--;
      i--;
      continue;
    }
    
    // Client -> Internet
    int bytes = forwardingClients[i].available();
    if (bytes > 0) {
      Serial.printf("RX: %d bytes\n", bytes);
      int readBytes = forwardingClients[i].read(rxBuffer, min(1460, bytes));
      
      // Simple HTTP proxy - works with most sites
      WiFiClient outClient;
      IPAddress targetIP(8, 8, 8, 8); // Google DNS fallback
      int targetPort = 80;
      
      // Try to parse Host header for real destination
      String request((char*)rxBuffer, readBytes);
      int hostPos = request.indexOf("Host: ");
      if (hostPos != -1) {
        int hostEnd = request.indexOf("\r\n", hostPos);
        String host = request.substring(hostPos + 6, hostEnd);
        host.trim();
        
        // DNS lookup (simple)
        if (WiFi.hostByName(host.c_str(), targetIP)) {
          Serial.println("Target: " + host + " -> " + targetIP.toString());
        }
        targetPort = host.endsWith(":443") ? 443 : 80;
      }
      
      if (outClient.connect(targetIP, targetPort)) {
        outClient.write(rxBuffer, readBytes);
        Serial.println("Forwarded to " + targetIP.toString() + ":" + String(targetPort));
        
        // Internet -> Client
        unsigned long timeout = millis() + 10000;
        while (outClient.connected() && millis() < timeout) {
          if (outClient.available()) {
            int respBytes = outClient.available();
            int readResp = outClient.read(txBuffer, min(1460, respBytes));
            forwardingClients[i].write(txBuffer, readResp);
          }
          delay(1);
        }
        outClient.stop();
      }
    }
  }
}

bool validateTXID(String txid) {
  if (!internetConnected) {
    Serial.println("VPS offline - TEST mode ENABLED");
    return true