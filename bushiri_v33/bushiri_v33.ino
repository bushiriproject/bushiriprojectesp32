#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <DNSServer.h>

// ==================== CONFIG - USIABADILISHE ====================
const char* AP_SSID = "Bushiri PROJECT";
const char* MODEM_SSID = "PATAHUDUMA";
const char* MODEM_PASS = "AMUDUH123";
const char* VPS_HOST = "bushiri-project.onrender.com";
const int VPS_PORT = 443;
const char* VPS_TOKEN = "bushiri2026";
const char* OWNER_MAC = "bc:90:63:a2:32:83";
const char* MIX_PHONE = "0717633805";
const char* MIX_NAME = "HAMISI BUSHIRI LUONGO";
const int TEST_DURATION = 120000; // 2 dakika
const int MAX_SESSIONS = 50;

// ==================== GLOBAL VARIABLES ====================
WebServer server(80);
DNSServer dnsServer;
WiFiClientSecure httpsClient;
String modemIP = "";
bool internetOK = false;
unsigned long lastCheck = 0;

// NAT Buffers
uint8_t rxBuf[1460];
uint8_t txBuf[1460];

// Sessions
struct ClientSession {
  String mac;
  String ip;
  unsigned long start;
  bool paid;
  String txid;
};
ClientSession activeSessions[50];
int sessionNum = 0;
WiFiServer natProxy(8080);

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  Serial.println("\n🚀 Bushiri PROJECT v3.4.0 - ESP32 3.3.8");
  
  // WiFi AP (open)
  WiFi.softAP(AP_SSID);
  Serial.println("✅ AP: " + String(AP_SSID) + " @ 192.168.4.1");
  
  // Captive Portal DNS
  dnsServer.start(53, "192.168.4.1", WiFi.softAPIP());
  
  // NAT Proxy Server
  natServer.begin();
  Serial.println("✅ NAT Proxy: port 8080");
  
  // Web Routes
  server.on("/", handlePortal);
  server.on("/admin", handleAdmin);
  server.on("/api/validate", handleValidate);
  server.on("/generate_204", handlePortal);
  server.on("/fwlink", handlePortal);
  server.on("/internet-ok", handleInternetOK);
  server.onNotFound(handlePortal);
  
  server.begin();
  Serial.println("✅ WebServer started");
  
  connectModem();
}

// ==================== MAIN LOOP ====================
void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  
  // Check modem every 10s
  if (millis() - lastCheck > 10000) {
    checkModem();
    lastCheck = millis();
  }
  
  // Handle NAT forwarding
  handleInternetForwarding();
  
  // Clean expired sessions
  cleanSessions();
}

// ==================== MODEM INTERNET ====================
void connectModem() {
  Serial.print("🔗 Connecting modem " + String(MODEM_SSID) + "...");
  WiFi.begin(MODEM_SSID, MODEM_PASS);
  
  int tries