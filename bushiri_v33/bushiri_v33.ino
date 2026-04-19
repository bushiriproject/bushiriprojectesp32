/*
  PROJECT BUSHIRI v3.1
  MPESA/MIXX Captive Portal + NAT Router + VPS Verify + WiFi Repeater
  Bei: TZS 800 = Masaa 15
*/

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <DNSServer.h>
#include <Update.h>

const char* AP_SSID      = "Bushiri WiFi";
const char* AP_PASS      = "";
const char* VPS_HOST     = "bushiri-project.onrender.com";
const int   VPS_PORT     = 443;
const char* VPS_TOKEN    = "bushiri2026";
const char* PORTAL_TITLE = "BUSHIRI HOTSPOT";
const char* MIXX_NUMBER  = "0717633805";
const char* STA_SSID_ALT = "PATAHUDUMA";
const char* STA_PASS_ALT = ".AMUDUH123";

String ownerMAC = "bc:90:63:a2:32:83"; // weka MAC ya simu yako bure
String freeTrialMAC = "";
unsigned long freeTrialStart = 0;

DNSServer dnsServer;
WebServer server(80);

bool isPaidClient(String mac) {
  if (mac == ownerMAC) return true; // mwenyewe bure
  HTTPClient http;
  String url = String("https://") + VPS_HOST + "/verify?mac=" + mac + "&token=" + VPS_TOKEN;
  http.begin(url);
  int httpCode = http.GET();
  if (httpCode == 200) {
    String payload = http.getString();
    if (payload.indexOf("ALLOW") >= 0) return true;
  }
  http.end();
  return false;
}

void handleRoot() {
  String mac = WiFi.macAddress();
  if (isPaidClient(mac)) {
    server.send(200, "text/html", "<h1>Karibu " + PORTAL_TITLE + "</h1><p>Umeunganishwa na internet.</p>");
  } else {
    String page = "<html><head><title>" + PORTAL_TITLE + "</title></head><body style='background:linear-gradient(to right,orange,yellow,green,blue);color:white;text-align:center;'>";
    page += "<h1>" + PORTAL_TITLE + "</h1>";
    page += "<p>Bei: TZS 800 = Masaa 15</p>";
    page += "<p>Tuma pesa kwa namba: " + String(MIXX_NUMBER) + "</p>";
    page += "<p>Utapokea TXID, weka hapa ili kuunganishwa.</p>";
    page += "<form action='/login'><input name='txid'><input type='submit' value='Login'></form>";
    page += "<h3>Free Trial (dakika 2)</h3>";
    page += "<form action='/trial'><input name='phone'><input type='submit' value='Jaribu'></form>";
    page += "</body></html>";
    server.send(200, "text/html", page);
  }
}

void handleLogin() {
  String txid = server.arg("txid");
  String mac = WiFi.macAddress();
  HTTPClient http;
  String url = String("https://") + VPS_HOST + "/login?txid=" + txid + "&mac=" + mac + "&token=" + VPS_TOKEN;
  http.begin(url);
  int httpCode = http.GET();
  if (httpCode == 200) {
    server.send(200, "text/html", "<h1>Umeidhinishwa! Karibu internet.</h1>");
  } else {
    server.send(200, "text/html", "<h1>TXID haijakubalika.</h1>");
  }
  http.end();
}

void handleTrial() {
  String mac = WiFi.macAddress();
  if (freeTrialMAC == mac && millis() - freeTrialStart < 86400000) {
    server.send(200, "text/html", "<h1>Samahani, free trial inapatikana tena baada ya masaa 24.</h1>");
    return;
  }
  freeTrialMAC = mac;
  freeTrialStart = millis();
  server.send(200, "text/html", "<h1>Free trial imeanza, dakika 2 pekee.</h1>");
}

void setup() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  WiFi.begin(STA_SSID_ALT, STA_PASS_ALT);

  dnsServer.start(53, "*", WiFi.softAPIP());
  server.on("/", handleRoot);
  server.on("/login", handleLogin);
  server.on("/trial", handleTrial);
  server.begin();
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
}