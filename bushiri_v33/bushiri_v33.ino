// FIXED NAT Forwarding - ESP32 3.3.8 COMPATIBLE
void handleNATForwarding() {
  if (!internetConnected) return;
  
  // Check for new NAT clients on port 8080
  WiFiClient newClient = natServer.available();
  if (newClient) {
    if (clientCount < 10) {
      forwardingClients[clientCount] = newClient;
      clientCount++;
      Serial.println("New NAT client #" + String(clientCount));
    } else {
      newClient.stop();
    }
  }
  
  // Process all forwarding clients
  for (int i = 0; i < clientCount; i++) {
    if (forwardingClients[i] && forwardingClients[i].connected()) {
      // Client -> Internet (outbound)
      int bytes = forwardingClients[i].available();
      if (bytes > 0) {
        bytes = forwardingClients[i].read(rxBuffer, min((int)sizeof(rxBuffer), bytes));
        Serial.printf("RX: %d bytes -> internet\n", bytes);
        
        // Use modem gateway IP directly
        IPAddress gatewayIP = WiFi.gatewayIP();
        WiFiClient modemClient;
        if (modemClient.connect(gatewayIP, 80)) {
          modemClient.write(rxBuffer, bytes);
          int resp = modemClient.available();
          if (resp > 0) {
            resp = modemClient.read(txBuffer, min((int)sizeof(txBuffer), resp));
            forwardingClients[i].write(txBuffer, resp);
            Serial.printf("TX: %d bytes from internet\n", resp);
          }
          modemClient.stop();
        }
      }
    }
    
    // Cleanup disconnected clients
    if (!forwardingClients[i] || !forwardingClients[i].connected()) {
      if (forwardingClients[i]) forwardingClients[i].stop();
      forwardingClients[i] = WiFiClient();
      clientCount--;
      // Shift array
      for (int j = i; j < clientCount; j++) {
        forwardingClients[j] = forwardingClients[j + 1];
      }
      i--; // Check same index again
    }
  }
}