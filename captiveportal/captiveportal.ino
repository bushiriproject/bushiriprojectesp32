#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>

const char* AP_SSID = "Bushiri WiFi";
const char* AP_PASS = "";

DNSServer dnsServer;
WebServer server(80);

void handlePage1() {
  String page = "<html><body style='background:linear-gradient(to right,orange,yellow,green,blue);color:white;text-align:center;'>";
  page += "<h1>BUSHIRI HOTSPOT</h1>";
  page += "<p>Bei: TZS 800 = Masaa 15</p>";
  page += "<a href='/page2'>Soma zaidi</a>";
  page += "</body></html>";
  server.send(200, "text/html", page);
}

void handlePage2() {
  String page = "<html><body style='background:linear-gradient(to right,blue,green);color:white;text-align:center;'>";
  page += "<h1>Faida za Huduma</h1>";
  page += "<p>Internet ya haraka, salama na nafuu.</p>";
  page += "</body></html>";
  server.send(200, "text/html", page);
}

void setup() {
  WiFi.softAP(AP_SSID, AP_PASS);
  dnsServer.start(53, "*", WiFi.softAPIP());
  server.on("/", handlePage1);
  server.on("/page2", handlePage2);
  server.begin();
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
}