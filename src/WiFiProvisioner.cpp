#include "WiFiProvisioner.h"
#include "html_template.h"
#include <WebServer.h>
#include <DNSServer.h>
#include <WiFi.h>

#define DEBUG_WIFI_PROV 1

#if DEBUG_WIFI_PROV
#define DEBUG_LOG(fmt, ...) Serial.printf("[WiFiProv] " fmt "\n", ##__VA_ARGS__)
#else
#define DEBUG_LOG(fmt, ...)
#endif

WiFiProvisioner::WiFiProvisioner(const char* apName)
  : _apName(apName), _server(nullptr), _dnsServer(nullptr),
    _apIP(192, 168, 4, 1), _netMask(255, 255, 255, 0),
    _credentialsReceived(false), _lastScanTime(0) {

  DEBUG_LOG("WiFiProvisioner initialized with AP name: %s", _apName);
}

WiFiProvisioner::~WiFiProvisioner() {
  releaseResources();
}

WiFiCredentials WiFiProvisioner::getCredentials() {
  DEBUG_LOG("Starting credential collection process...");

  // Reset state
  _credentialsReceived = false;
  _credentials = {"", "", false, ""};

  // Setup Access Point and servers
  setupAP();
  startServers();

  DEBUG_LOG("Entering blocking loop, waiting for credentials...");

  // Blocking loop until credentials are received
  while (!_credentialsReceived) {
    handleClient();
    delay(10);  // Small delay to prevent watchdog timeout
  }

  DEBUG_LOG("Credentials received, cleaning up...");
  releaseResources();

  return _credentials;
}

void WiFiProvisioner::setupAP() {
  DEBUG_LOG("Setting up Access Point...");

  // Disconnect from any existing connections
  WiFi.disconnect(true);
  delay(100);

  // Set WiFi mode to AP+STA (needed for captive portal detection)
  WiFi.mode(WIFI_AP_STA);
  delay(100);

  // Configure AP IP settings
  if (!WiFi.softAPConfig(_apIP, _apIP, _netMask)) {
    DEBUG_LOG("Failed to configure AP IP settings");
    return;
  }

  // Start Access Point
  if (!WiFi.softAP(_apName)) {
    DEBUG_LOG("Failed to start Access Point");
    return;
  }

  DEBUG_LOG("Access Point '%s' started successfully", _apName);
  DEBUG_LOG("AP IP address: %s", WiFi.softAPIP().toString().c_str());
}

void WiFiProvisioner::startServers() {
  DEBUG_LOG("Starting web and DNS servers...");

  // Initialize web server
  _server = new WebServer(80);
  _dnsServer = new DNSServer();

  // Setup DNS server (captive portal)
  _dnsServer->setErrorReplyCode(DNSReplyCode::NoError);
  _dnsServer->start(53, "*", _apIP);

  // Setup web server routes
  _server->on("/", [this]() { handleRootRequest(); });
  _server->on("/connect", HTTP_POST, [this]() { handleConnectRequest(); });
  _server->on("/favicon.ico", [this]() { _server->send(404, "text/plain", "Not found"); });

  // Captive portal detection endpoints for different devices
  _server->on("/generate_204", [this]() { handleRootRequest(); });          // Android
  _server->on("/gen_204", [this]() { handleRootRequest(); });              // Android (short version)
  _server->on("/fwlink", [this]() { handleRootRequest(); });               // Microsoft
  _server->on("/hotspot-detect.html", [this]() { handleRootRequest(); });  // iOS
  _server->on("/library/test/success.html", [this]() { handleRootRequest(); }); // iOS
  _server->on("/ncsi.txt", [this]() { handleRootRequest(); });             // Windows
  _server->on("/connecttest.txt", [this]() { handleRootRequest(); });      // Android
  _server->on("/redirect", [this]() { handleRootRequest(); });             // Generic

  _server->onNotFound([this]() {
    DEBUG_LOG("Unknown request: %s %s", _server->method() == HTTP_GET ? "GET" : "POST", _server->uri().c_str());
    handleRootRequest();
  });  // Catch-all

  _server->begin();
  DEBUG_LOG("Servers started successfully");

  // Start initial network scan in background (non-blocking)
  DEBUG_LOG("Starting background network scan...");
  WiFi.scanNetworks(true); // Async scan
}

void WiFiProvisioner::handleClient() {
  if (_dnsServer) {
    _dnsServer->processNextRequest();
  }
  if (_server) {
    _server->handleClient();
  }
}

void WiFiProvisioner::handleRootRequest() {
  DEBUG_LOG("Handling root request from: %s", _server->client().remoteIP().toString().c_str());

  String html = loadHTMLTemplate();
  if (html.length() == 0) {
    _server->send(500, "text/plain", "Failed to load HTML template");
    return;
  }

  // Check if user explicitly requested a refresh via the refresh button
  bool forceRefresh = _server->hasArg("refresh");

  // Replace the networks placeholder with cached or fresh networks
  String networksList = generateNetworksList(forceRefresh);
  html.replace("{{NETWORKS_LIST}}", networksList);

  _server->send(200, "text/html", html);
  DEBUG_LOG("HTML page sent successfully");
}

void WiFiProvisioner::handleConnectRequest() {
  DEBUG_LOG("Handling connect request...");

  if (!_server->hasArg("ssid")) {
    _server->send(400, "text/plain", "Missing SSID");
    return;
  }

  String ssid = _server->arg("ssid");
  String password = _server->arg("password");

  DEBUG_LOG("Received credentials - SSID: '%s', Password: '%s'",
            ssid.c_str(), password.length() > 0 ? "[PROVIDED]" : "[EMPTY]");

  // Store credentials
  _credentials.ssid = ssid;
  _credentials.password = password;
  _credentials.success = true;
  _credentials.error = "";
  _credentialsReceived = true;

  // Send success response
  String successPage = R"(
<!DOCTYPE html>
<html>
<head><title>Success</title></head>
<body style="font-family: Arial; text-align: center; padding: 50px;">
  <h1 style="color: green;">✓ Credentials Saved!</h1>
  <p>WiFi credentials have been saved successfully.</p>
  <p>The device will now attempt to connect...</p>
</body>
</html>)";

  _server->send(200, "text/html", successPage);
  DEBUG_LOG("Success page sent, credentials collection complete");
}

String WiFiProvisioner::loadHTMLTemplate() {
  // Load embedded HTML template (converted from binary at runtime)
  DEBUG_LOG("Loading embedded HTML template");
  return htmlContent;
}

String WiFiProvisioner::generateNetworksList(bool forceRefresh) {
  unsigned long currentTime = millis();

  // Check if we need to refresh the cache
  bool needsRefresh = forceRefresh ||
                     _cachedNetworksList.length() == 0 ||
                     (currentTime - _lastScanTime > SCAN_INTERVAL);

  if (!needsRefresh) {
    DEBUG_LOG("Using cached networks list");
    return _cachedNetworksList;
  }

  // Check if async scan is running
  int scanResult = WiFi.scanComplete();

  if (scanResult == WIFI_SCAN_RUNNING) {
    DEBUG_LOG("Scan in progress, showing loading indicator");
    return "<div class=\"scanning\">📶 Scanning for networks... <div class=\"spinner\"></div></div>";
  }

  // If no scan running and we need refresh, start async scan
  if (scanResult == WIFI_SCAN_FAILED || forceRefresh) {
    DEBUG_LOG("Starting async network scan (refresh=%s)...", forceRefresh ? "forced" : "auto");
    WiFi.scanNetworks(true); // Start async scan
    return "<div class=\"scanning\">📶 Scanning for networks... <div class=\"spinner\"></div></div>";
  }

  // Process completed scan results
  int networkCount = scanResult;
  DEBUG_LOG("Processing scan results: %d networks found", networkCount);

  if (networkCount == 0) {
    _cachedNetworksList = "<div class=\"no-networks\">No networks found. Try refreshing.</div>";
    _lastScanTime = currentTime;
    return _cachedNetworksList;
  }

  String networksList = "";

  for (int i = 0; i < networkCount; i++) {
    String ssid = WiFi.SSID(i);
    int32_t rssi = WiFi.RSSI(i);
    bool isSecured = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);

    if (ssid.length() == 0) continue;  // Skip hidden networks

    String signalStrength = getSignalStrength(rssi);
    String lockIcon = isSecured ? " 🔒" : "";

    networksList += "<div class=\"network\" data-ssid=\"" + ssid + "\" data-secured=\"" +
                   (isSecured ? "true" : "false") + "\">";
    networksList += "<span>" + ssid + lockIcon + "</span>";
    networksList += "<span class=\"signal-strength\">" + signalStrength + "</span>";
    networksList += "</div>";

    DEBUG_LOG("Network %d: %s (%s, %s)", i, ssid.c_str(),
              signalStrength.c_str(), isSecured ? "Secured" : "Open");
  }

  // Cache the results and clear scan results
  _cachedNetworksList = networksList;
  _lastScanTime = currentTime;
  WiFi.scanDelete(); // Free memory

  DEBUG_LOG("Generated and cached networks list with %d networks", networkCount);
  return networksList;
}

String WiFiProvisioner::getSignalStrength(int rssi) {
  if (rssi > -50) return "Excellent";
  if (rssi > -60) return "Good";
  if (rssi > -70) return "Fair";
  return "Weak";
}

void WiFiProvisioner::releaseResources() {
  DEBUG_LOG("Releasing resources...");

  if (_server) {
    _server->stop();
    delete _server;
    _server = nullptr;
  }

  if (_dnsServer) {
    _dnsServer->stop();
    delete _dnsServer;
    _dnsServer = nullptr;
  }

  // Stop AP mode
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);

  DEBUG_LOG("Resources released successfully");
}