#ifndef WIFIPROVISIONER_H
#define WIFIPROVISIONER_H

#include <IPAddress.h>

class WebServer;
class DNSServer;

struct WiFiCredentials {
  String ssid;
  String password;
  bool success;
  String error;
};

class WiFiProvisioner {
public:
  explicit WiFiProvisioner(const char* apName = "ESP32 Wi-Fi Setup");
  ~WiFiProvisioner();

  // Blocking function that returns credentials or error
  WiFiCredentials getCredentials();

private:
  void setupAP();
  void startServers();
  void handleClient();
  void releaseResources();

  // Request handlers
  void handleRootRequest();
  void handleConnectRequest();

  // Utility functions
  String loadHTMLTemplate();
  String generateNetworksList();
  String getSignalStrength(int rssi);

  const char* _apName;
  WebServer* _server;
  DNSServer* _dnsServer;
  IPAddress _apIP;
  IPAddress _netMask;

  // State variables
  bool _credentialsReceived;
  WiFiCredentials _credentials;
};

#endif // WIFIPROVISIONER_H