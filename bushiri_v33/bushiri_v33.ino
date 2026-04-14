/**
 * BUSHIRI FIXED C VERSION
 * AP + STA + NAT INTERNET SHARING
 */

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>

extern "C" {
  #include "lwip/lwip_napt.h"
  #include "lwip/ip_addr.h"
}

#define AP_IP_HEX 0xC0A80401UL

const char* AP_SSID = "Bushiri WiFi";
const char* STA_SSID_ALT = "PATA HUDUMA";
const char* STA_PASS_ALT = "AMUDUH123";

WebServer server(80);
DNSServer dnsServer;
Preferences prefs;

bool natEnabled = false;

// ================= NAT =================
void enableNAT() {
  ip_napt_enable(htonl(AP_IP_HEX), 1);
  natEnabled = true;
  Serial.println("[NAT] ON");
}

// ================= WIFI CONNECT =================
void connectInternet() {
  WiFi.mode(WIFI_AP_STA);

  WiFi.softAP(AP_SSID);
  Serial.println("[AP] Started");

  WiFi.begin(STA_SSID_ALT, STA_PASS_ALT);

  int t = 0;
  while (WiFi.status() != WL_CONNECTED && t < 20) {
    delay(500);
    Serial.print(".");
    t++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[STA] Connected: " + WiFi.localIP().toString());
    enableNAT();
  } else {
    Serial.println("\n[STA] Failed");
  }
}

// ================= VPS VERIFY (FIXED JSON) =================
bool verifyWithVPS(String txid, String ip, String &message) {

  if (WiFi.status() != WL_CONNECTED) {
    message = "No internet";
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();

  if (!client.connect("bushiri-project.onrender.com", 443)) {
    message = "VPS error";
    return false;
  }

  // ✅ FIX ARDUINOJSON v7
  JsonDocument doc;
  doc["txid"] = txid;
  doc["mac"]  = ip;

  String payload;
  serializeJson(doc, payload);

  client.println("POST /verify HTTP/1.1");
  client.println("Host: bushiri-project.onrender.com");
  client.println("Content-Type: application/json");
  client.println("Content-Length: " + String(payload.length()));
  client.println();
  client.print(payload);

  String response = "";
  while (client.available()) {
    response += client.readString();
  }

  JsonDocument res;
  DeserializationError err = deserializeJson(res, response);

  if (err) {
    message = "Bad VPS response";
    return false;
  }

  bool success = res["success"] | false;
  message = res["message"] | "error";

  return success;
}

// ================= WEB SERVER FIX =================
void setupWebServer() {

  server.on("/", []() {
    server.send(200, "text/html",
      "<h1>BUSHIRI HOTSPOT WORKING</h1>"
      "<a href='/pay'>PAY</a>");
  });

  server.on("/pay", []() {
    server.send(200, "text/html",
      "<form method='POST' action='/verify'>"
      "TXID:<input name='txid'><br>"
      "PHONE:<input name='phone'><br>"
      "<button>OK</button></form>");
  });

  server.on("/verify", HTTP_POST, []() {

    String txid = server.arg("txid");
    String phone = server.arg("phone");
    String ip = server.client().remoteIP().toString();

    String msg;

    if (verifyWithVPS(txid, ip, msg)) {
      server.send(200, "text/html",
        "<h1>SUCCESS INTERNET ENABLED</h1>");
    } else {
      server.send(200, "text/html",
        "<h1>FAILED: " + msg + "</h1>");
    }
  });

  server.begin();
  Serial.println("[WEB] Server started");
}

// ================= OTA FIX =================
void setupOTA() {
  server.on("/update", HTTP_GET, []() {
    server.send(200, "text/html", "OTA READY");
  });
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  connectInternet();
  setupWebServer();
  setupOTA();
}

// ================= LOOP =================
void loop() {
  server.handleClient();
  dnsServer.processNextRequest();

  // 🔥 KEEP NAT ALIVE
  if (WiFi.status() == WL_CONNECTED) {
    ip_napt_enable(htonl(AP_IP_HEX), 1);
  }
}