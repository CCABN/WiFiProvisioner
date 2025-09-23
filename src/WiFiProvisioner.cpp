#include "WiFiProvisioner.h"
#include "internal/provision_html.h"
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

#define WIFI_PROVISIONER_LOG_DEBUG 0
#define WIFI_PROVISIONER_LOG_INFO 1
#define WIFI_PROVISIONER_LOG_WARN 2
#define WIFI_PROVISIONER_LOG_ERROR 3

#define WIFI_PROVISIONER_DEBUG // Comment to hide debug prints

#ifdef WIFI_PROVISIONER_DEBUG
#define WIFI_PROVISIONER_DEBUG_LOG(level, format, ...)                         \
  do {                                                                         \
    if (level >= WIFI_PROVISIONER_LOG_DEBUG) {                                 \
      Serial.printf("[WIFI_PROV][%s] " format "\n",                           \
                    (level == WIFI_PROVISIONER_LOG_DEBUG)  ? "DEBUG"           \
                    : (level == WIFI_PROVISIONER_LOG_INFO) ? "INFO"            \
                    : (level == WIFI_PROVISIONER_LOG_WARN) ? "WARN"            \
                                                           : "ERROR",          \
                    ##__VA_ARGS__);                                            \
      Serial.flush();                                                         \
    }                                                                          \
  } while (0)
#else
#define WIFI_PROVISIONER_DEBUG_LOG(level, format, ...)                         \
  do {                                                                         \
  } while (0) // Empty macro
#endif

namespace {

/**
 * @brief Converts a Received Signal Strength Indicator (RSSI) value to a signal
 * strength level.
 *
 * This function maps RSSI values to a step level ranging from 0 to 4 based on
 * predefined minimum and maximum RSSI thresholds. The returned level provides
 * an approximation of the signal quality.
 *
 * @param rssi The RSSI value (in dBm) representing the signal strength
 * of a Wi-Fi network.
 *
 * @return An integer in the range [0, 4], where 0 indicates very poor signal
 * strength and 4 indicates excellent signal strength.
 */
int convertRRSItoLevel(int rssi) {
  //  Convert RSSI to 0 - 4 Step level
  int numlevels = 4;
  int MIN_RSSI = -100;
  int MAX_RSSI = -55;

  if (rssi < MIN_RSSI) {
    return 0;
  } else if (rssi >= MAX_RSSI) {
    return numlevels;
  } else {
    int inputRange = MAX_RSSI - MIN_RSSI;
    int res = std::ceil((rssi - MIN_RSSI) * numlevels / inputRange);
    if (res == 0) {
      return 1;
    } else {
      return res;
    }
  }
}

/**
 * @brief Scans for available Wi-Fi networks and populates a JSON document with
 * the results.
 *
 * This function performs a Wi-Fi network scan, collecting information about
 * each detected network, including its SSID, signal strength (converted to a
 * level), and authentication mode. The results are stored in the provided `doc`
 * JSON document under the "network" array.
 *
 * @param doc A reference to a `JsonDocument` object where the scan results will
 * be stored. The document will contain an array of networks, each represented
 * as a JSON object with the following keys:
 *
 *            - `ssid`: The network SSID (string).
 *
 *            - `rssi`: The signal strength level (integer, 0 to 4).
 *
 *            - `authmode`: The authentication mode (0 for open, 1 for secured).
 */
void networkScan(JsonDocument &doc) {
  JsonArray networks = doc["network"].to<JsonArray>();

  WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_INFO,
                             "Starting Network Scan...");

  // Clear any previous scan results
  WiFi.scanDelete();

  // Check current WiFi mode and status
  WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_DEBUG,
                             "Current WiFi mode: %d, Status: %d",
                             WiFi.getMode(), WiFi.status());

  // Start asynchronous scan
  int n = WiFi.scanNetworks(false, false, false, 300U);
  WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_INFO,
                             "Scan returned: %d", n);

  if (n == WIFI_SCAN_RUNNING) {
    WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_DEBUG,
                               "Scan still running, waiting...");
    // Wait for scan to complete with timeout
    unsigned long scanStart = millis();
    while (WiFi.scanComplete() == WIFI_SCAN_RUNNING &&
           (millis() - scanStart) < 10000) {
      delay(100);
    }
    n = WiFi.scanComplete();
    WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_DEBUG,
                               "Scan completed with result: %d", n);
  }

  if (n > 0) {
    WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_INFO,
                               "Found %d networks", n);
    for (int i = 0; i < n; ++i) {
      String ssid = WiFi.SSID(i);
      int32_t rssi = WiFi.RSSI(i);
      wifi_auth_mode_t encryption = WiFi.encryptionType(i);

      WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_DEBUG,
                                 "Network %d: SSID='%s', RSSI=%d, Encryption=%d",
                                 i, ssid.c_str(), rssi, encryption);

      if (ssid.length() > 0 && !ssid.startsWith("\\x00")) {  // Filter out invalid SSIDs
        JsonObject network = networks.add<JsonObject>();
        network["rssi"] = convertRRSItoLevel(rssi);
        network["ssid"] = ssid;
        network["authmode"] = (encryption == WIFI_AUTH_OPEN) ? 0 : 1;

        WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_DEBUG,
                                   "Added network: %s (RSSI level: %d, Auth: %d)",
                                   ssid.c_str(), convertRRSItoLevel(rssi),
                                   (encryption == WIFI_AUTH_OPEN) ? 0 : 1);
      } else {
        WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_DEBUG,
                                   "Skipped invalid SSID at index %d", i);
      }
    }
  } else if (n == 0) {
    WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_WARN,
                               "No networks found during scan");
  } else if (n == WIFI_SCAN_FAILED) {
    WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_ERROR,
                               "Network scan failed");
  } else {
    WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_ERROR,
                               "Network scan returned unexpected result: %d", n);
  }

  WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_INFO,
                             "Network scan complete, added %d networks to JSON",
                             networks.size());
}

/**
 * @brief Sends an HTTP header response to the client.
 *
 * This function constructs and sends the HTTP response header to the connected
 * client, specifying the HTTP status code, content type, and content length.
 *
 * @param client A reference to the `WiFiClient` object representing the
 * connected client.
 * @param statusCode The HTTP status code (e.g., 200 for success, 404 for not
 * found).
 * @param contentType The MIME type of the content (e.g., "text/html",
 * "application/json").
 * @param contentLength The size of the content in bytes to be sent in the
 * response.
 */
void sendHeader(WiFiClient &client, int statusCode, const char *contentType,
                size_t contentLength) {
  if (!client.connected()) {
    return;
  }

  client.print("HTTP/1.1 ");
  client.print(statusCode);
  client.println(" OK");

  client.print("Content-Type: ");
  client.println(contentType);

  if (contentLength > 0) {
    client.print("Content-Length: ");
    client.println(contentLength);
  } else {
    client.println("Transfer-Encoding: chunked");
  }

  client.println("Connection: close");
  client.println("Cache-Control: no-cache, no-store, must-revalidate");
  client.println("Pragma: no-cache");
  client.println("Expires: 0");

  client.println();
}

void sendChunkedData(WiFiClient &client, const char *data, size_t length) {
  if (!client.connected() || length == 0) {
    return;
  }

  // Send chunk size in hex
  client.printf("%X\r\n", length);
  // Send chunk data
  client.write((const uint8_t*)data, length);
  client.print("\r\n");
}

void sendChunkedEnd(WiFiClient &client) {
  if (!client.connected()) {
    return;
  }
  client.print("0\r\n\r\n");
}

} // namespace

/**
 * @brief Default constructor for the `WiFiProvisioner::Config` struct.
 *
 * Initializes the configuration for the WiFi provisioning process with default
 * values. These defaults provide a pre-configured setup for a typical
 * provisioning page, including Access Point (AP) details, web page appearance,
 * and behavioral settings.
 *
 * Default Values:
 *
 * - `AP_NAME`: "ESP32 Wi-Fi Provisioning" - The default name for the Wi-Fi
 * Access Point.
 *
 * - `HTML_TITLE`: "Welcome to Wi-Fi Provision" - The title of the
 * provisioning web page.
 *
 * - `THEME_COLOR`: "dodgerblue" - The primary theme color used in the
 * provisioning UI.
 *
 * - `SVG_LOGO`: An SVG logo to display on the web page.
 *
 * - `PROJECT_TITLE`: "Wifi Provisioner" - The project title shown on the
 * page.
 *
 * - `PROJECT_SUB_TITLE`: "Device Setup" - The sub-title displayed below the
 * project title.
 *
 * - `PROJECT_INFO`: "Follow the steps to provision your device" -
 * Instructions displayed on the page.
 *
 * - `FOOTER_TEXT`: "All rights reserved © WiFiProvisioner" - Text displayed
 * in the footer.
 *
 * - `CONNECTION_SUCCESSFUL`: "Your device is now provisioned and ready to
 * use." - Message shown after a successful connection.
 *
 * - `RESET_CONFIRMATION_TEXT`: "This process cannot be undone." - Text for
 * factory reset confirmation.
 *
 * - `INPUT_TEXT`: "Device Key" - Label text for an additional input field (if
 * shown).
 *
 * - `INPUT_LENGTH`: 6 - Maximum length for the additional input field.
 *
 * - `SHOW_INPUT_FIELD`: `false` - Whether to display the additional input
 * field on the page.
 *
 * - `SHOW_RESET_FIELD`: `true` - Whether to display the factory reset option.
 *
 *
 * @param apName The name of the Wi-Fi Access Point (AP).
 * @param htmlTitle The title of the provisioning web page.
 * @param themeColor The primary theme color used in the UI.
 * @param svgLogo The SVG logo to display on the web page.
 * @param projectTitle The project title displayed on the page.
 * @param projectSubTitle The sub-title displayed below the project title.
 * @param projectInfo Instructions or information displayed on the page.
 * @param footerText Text displayed in the footer.
 * @param connectionSuccessful Message shown after a successful connection.
 * @param resetConfirmationText Confirmation text for factory resets.
 * @param inputText Label for an additional input field (if shown).
 * @param inputLength Maximum length for the additional input field.
 * @param showInputField Whether to display the additional input field.
 * @param showResetField Whether to display the factory reset option.
 *
 * Example Usage:
 * ```
 * WiFiProvisioner::Config customConfig(
 *     "CustomAP", "Custom Title", "darkblue", "<custom_svg>",
 *     "Custom Project", "Custom Setup", "Custom Information",
 *     "Custom Footer", "Success Message", "Are you sure?",
 *     "Custom Key", 10, true, false);
 * ```
 */
WiFiProvisioner::Config::Config(const char *apName, const char *htmlTitle,
                                const char *themeColor, const char *svgLogo,
                                const char *projectTitle,
                                const char *projectSubTitle,
                                const char *projectInfo, const char *footerText,
                                const char *connectionSuccessful,
                                const char *resetConfirmationText,
                                const char *inputText, int inputLength,
                                bool showInputField, bool showResetField)
    : AP_NAME(apName), HTML_TITLE(htmlTitle), THEME_COLOR(themeColor),
      SVG_LOGO(svgLogo), PROJECT_TITLE(projectTitle),
      PROJECT_SUB_TITLE(projectSubTitle), PROJECT_INFO(projectInfo),
      FOOTER_TEXT(footerText), CONNECTION_SUCCESSFUL(connectionSuccessful),
      RESET_CONFIRMATION_TEXT(resetConfirmationText), INPUT_TEXT(inputText),
      INPUT_LENGTH(inputLength), SHOW_INPUT_FIELD(showInputField),
      SHOW_RESET_FIELD(showResetField) {}

/**
 * @brief Constructs a new `WiFiProvisioner` instance with the specified
 * configuration.
 *
 * Initializes the WiFiProvisioner with either the provided configuration or the
 * default configuration. The configuration dictates the behavior and appearance
 * of the WiFi provisioning process, including AP details, UI elements, and
 * behavioral options.
 *
 * @param config A reference to a `WiFiProvisioner::Config` structure containing
 * the configuration for the WiFi provisioning process. If no configuration is
 * provided, the default configuration is used.
 *
 * Example Usage:
 *
 * Default configuration
 *
 * ```
 * WiFiProvisioner provisioner;
 * ```
 *
 *
 * Custom configuration
 *
 * ```
 * WiFiProvisioner::Config customConfig(
 *     "CustomAP", "Custom Title", "darkblue", "<custom_svg>",
 *     "Custom Project", "Custom Setup", "Custom Information",
 *     "Custom Footer", "Success Message", "Are you sure?",
 *     "Custom Key", 10, true, false);
 * WiFiProvisioner provisioner(customConfig);
 * ```
 *
 * @note Modifications to the configuration can be made after initialization via
 * the `getConfig()` method:
 * ```
 * provisioner.getConfig().AP_NAME = "UpdatedAP";
 * ```
 */
WiFiProvisioner::WiFiProvisioner(const Config &config)
    : _config(config), _server(nullptr), _dnsServer(nullptr),
      _apIP(192, 168, 4, 1), _netMsk(255, 255, 255, 0), _dnsPort(53),
      _serverPort(80), _wifiDelay(100), _wifiConnectionTimeout(10000),
      _serverLoopFlag(false) {}

WiFiProvisioner::~WiFiProvisioner() { releaseResources(); }

/**
 * @brief Provides access to the configuration structure.
 *
 * This method returns a reference to the `Config` structure, allowing
 * controlled access to modify the configuration values after the instance
 * has been created. Users should always modify the configuration through
 * this method and never directly edit the `Config` object submitted to
 * the constructor. This ensures consistent behavior and avoids unexpected
 * results during the provisioning process.
 *
 * @return A reference to the `Config` structure of the current WiFiProvisioner
 * instance.
 *
 * @note Modifications to the configuration should always be done through
 * this method to ensure that changes are properly reflected within the
 * `WiFiProvisioner` instance.
 *
 * Example Usage:
 * ```
 * provisioner.getConfig().AP_NAME = "UpdatedAP";
 * provisioner.getConfig().SHOW_INPUT_FIELD = true;
 * ```
 */
WiFiProvisioner::Config &WiFiProvisioner::getConfig() { return _config; }

/**
 * @brief Releases resources allocated during the provisioning process.
 *
 * This method stops the web server, DNS server, and resets the Wi-Fi mode to
 * `WIFI_STA`. It is called to clean up resources once the provisioning process
 * is complete or aborted.
 */
void WiFiProvisioner::releaseResources() {
  _serverLoopFlag = false;

  // Webserver
  if (_server != nullptr) {
    WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_INFO, "Stopping server");
    _server->stop();
    delete _server;
    _server = nullptr;
  }

  // DNS
  if (_dnsServer != nullptr) {
    WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_INFO,
                               "Stopping DNS server");
    _dnsServer->stop();
    delete _dnsServer;
    _dnsServer = nullptr;
  }

  // WiFi
  if (WiFi.getMode() != WIFI_STA) {
    WiFi.mode(WIFI_STA);
    delay(_wifiDelay);
  }
}

/**
 * @brief Starts the provisioning process, setting up the device in Access
 * Point (AP) mode with a captive portal for Wi-Fi configuration.
 *
 * Access Instructions:
 *
 * 1. Open your device's Wi-Fi settings.
 *
 * 2. Connect to the Wi-Fi network specified by `_config.AP_NAME`.
 *    - Default: "ESP32 Wi-Fi Provisioning".
 *
 * 3. Once connected, the provisioning page should open automatically. If it
 * does not, open a web browser and navigate to `192.168.4.1`.
 *
 * @return `true` if provisioning was successful `false` otherwise.
 *
 * Example Usage:
 * ```
 * WiFiProvisioner provisioner;
 * if (!provisioner.startProvisioning()) {
 *     Serial.println("Provisioning failed. Check logs for details.");
 * }
 * ```
 *
 * @note
 * - The `Config` object within the `WiFiProvisioner` is used to customize the
 * behavior and appearance of the provisioning system.
 */
bool WiFiProvisioner::startProvisioning() {
  WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_INFO,
                             "Starting WiFi provisioning process...");

  // Check current WiFi status before starting
  WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_DEBUG,
                             "Current WiFi mode: %d, Status: %d",
                             WiFi.getMode(), WiFi.status());

  WiFi.disconnect(false, true);
  delay(_wifiDelay);

  releaseResources();

  WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_DEBUG,
                             "Creating server instances...");
  _server = new WebServer(_serverPort);
  _dnsServer = new DNSServer();

  WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_DEBUG,
                             "Setting WiFi mode to AP+STA...");
  if (!WiFi.mode(WIFI_AP_STA)) {
    WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_ERROR,
                               "Failed to switch to AP+STA mode");
    return false;
  }
  delay(_wifiDelay);

  WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_DEBUG,
                             "Configuring Access Point with IP: %s, name: %s",
                             _apIP.toString().c_str(), _config.AP_NAME);

  if (!WiFi.softAPConfig(_apIP, _apIP, _netMsk)) {
    WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_ERROR,
                               "Failed to configure AP IP settings");
    return false;
  }

  if (!WiFi.softAP(_config.AP_NAME)) {
    WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_ERROR,
                               "Failed to start Access Point: %s", _config.AP_NAME);
    return false;
  }
  delay(_wifiDelay);

  // Verify AP is actually started
  WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_INFO,
                             "Access Point started. IP: %s, Clients: %d",
                             WiFi.softAPIP().toString().c_str(),
                             WiFi.softAPgetStationNum());

  WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_DEBUG,
                             "Starting DNS server on port %d...", _dnsPort);
  _dnsServer->setErrorReplyCode(DNSReplyCode::NoError);
  if (!_dnsServer->start(_dnsPort, "*", _apIP)) {
    WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_ERROR,
                               "Failed to start DNS server on port %d", _dnsPort);
    return false;
  }

  WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_DEBUG,
                             "Registering HTTP request handlers...");
  _server->on("/", [this]() { this->handleRootRequest(); });
  _server->on("/configure", HTTP_POST,
              [this]() { this->handleConfigureRequest(); });
  _server->on("/update", [this]() { this->handleUpdateRequest(); });
  _server->on("/generate_204", [this]() { this->handleRootRequest(); });
  _server->on("/fwlink", [this]() { this->handleRootRequest(); });
  _server->on("/hotspot-detect.html", [this]() { this->handleRootRequest(); });
  _server->on("/library/test/success.html", [this]() { this->handleRootRequest(); });
  _server->on("/ncsi.txt", [this]() { this->handleRootRequest(); });
  _server->on("/connecttest.txt", [this]() { this->handleRootRequest(); });
  _server->on("/factoryreset", HTTP_POST,
              [this]() { this->handleResetRequest(); });
  _server->onNotFound([this]() { this->handleRootRequest(); });

  WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_DEBUG,
                             "Starting HTTP server on port %d...", _serverPort);
  _server->begin();
  WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_INFO,
                             "Provision server started at %s",
                             WiFi.softAPIP().toString().c_str());

  // Do an initial WiFi scan to check if scanning works
  WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_DEBUG,
                             "Performing initial WiFi scan test...");
  int networkCount = WiFi.scanNetworks(false, false);
  WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_INFO,
                             "Initial scan found %d networks", networkCount);

  WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_INFO,
                             "Entering main provisioning loop...");
  loop();
  return true;
}

/**
 * @brief Handles the main loop for the Wi-Fi provisioning process.
 *
 * This function continuously processes DNS and HTTP server requests while the
 * provisioning process is active. It ensures that DNS requests are resolved to
 * redirect clients to the provisioning page and handles HTTP client
 * interactions.
 *
 * The loop runs until the `_serverLoopFlag` is set to `true`, indicating that
 * provisioning is complete or the server needs to shut down.
 */
void WiFiProvisioner::loop() {
  while (!_serverLoopFlag) {
    // DNS
    if (_dnsServer) {
      _dnsServer->processNextRequest();
    }

    // HTTP
    if (_server) {
      _server->handleClient();
    }
  }
  releaseResources();
}

/**
 * @brief Registers a callback function to handle provisioning events.
 *
 * This callback is invoked whenever provisioning starts, allowing the user
 * to, for example, dynamically adjust the configuration (e.g., showing or
 * hiding the input field)
 *
 * @param callback A callable object or lambda that performs operations when
 * provisioning starts.
 *
 * @return A reference to the `WiFiProvisioner` instance for method chaining.
 *
 * Example:
 * ```
 * provisioner.onProvision([]() {
 *     if (hasApiKey()) {
 *         provisioner.getConfig().SHOW_INPUT_FIELD = false;
 *     } else {
 *         provisioner.getConfig().SHOW_INPUT_FIELD = true;
 *     }
 *     Serial.println("Provisioning process has started.");
 * });
 * ```
 */
WiFiProvisioner &WiFiProvisioner::onProvision(ProvisionCallback callback) {
  provisionCallback = std::move(callback);
  return *this;
}

/**
 * @brief Registers a callback function to validate user input during
 * provisioning.
 *
 * This callback is invoked to validate the additional input field (if enabled)
 * during the provisioning process. The callback should return `true` if the
 * input is valid, or `false` otherwise.
 *
 * @param callback A callable object or lambda that accepts a `const char*`
 * representing the user input and returns a `bool` indicating its validity.
 *
 * @return A reference to the `WiFiProvisioner` instance for method chaining.
 *
 * Example:
 * ```
 * provisioner.onInputCheck([](const char* input) -> bool {
 *     return strcmp(input, "1234") == 0; // Validate the input
 * });
 * ```
 */
WiFiProvisioner &WiFiProvisioner::onInputCheck(InputCheckCallback callback) {
  inputCheckCallback = std::move(callback);
  return *this;
}

/**
 * @brief Registers a callback function to handle factory reset operations.
 *
 * The callback function is triggered when a factory reset is initiated by the
 * user. It should perform necessary cleanup or reinitialization tasks required
 * for a factory reset.
 *
 * @param callback A callable object or lambda that performs operations when
 * a factory reset is triggered.
 *
 * @return A reference to the `WiFiProvisioner` instance for method chaining.
 *
 * Example:
 * ```
 * provisioner.onFactoryReset([]() {
 *     Serial.println("Factory reset triggered!");
 *     // Additional cleanup logic here
 * });
 * ```
 */
WiFiProvisioner &
WiFiProvisioner::onFactoryReset(FactoryResetCallback callback) {
  factoryResetCallback = std::move(callback);
  return *this;
}

/**
 * @brief Registers a callback function to handle successful provisioning
 * events.
 *
 * This callback is invoked after the device successfully connects to the
 * configured Wi-Fi network and validates optional user input (if required).
 *
 * @param callback A callable object or lambda that accepts the following
 * parameters:
 * - `const char* ssid`: The SSID of the connected Wi-Fi network.
 * - `const char* password`: The password of the Wi-Fi network. If the network
 * is open, this parameter will be `nullptr`.
 * - `const char* input`: The user-provided input (if enabled in the
 * configuration). If the input field is disabled, this parameter will be
 * `nullptr`.
 *
 * @return A reference to the `WiFiProvisioner` instance for method chaining.
 *
 * @note
 * - If the `SHOW_INPUT_FIELD` configuration was not enabled, the `input`
 * parameter will be `nullptr`.
 * - If the Wi-Fi network is open (no password required), the `password`
 * parameter will be `nullptr`.
 *
 * Example:
 * ```
 * provisioner.onSuccess([](const char* ssid, const char* password, const char*
 * input) { Serial.printf("Connected to SSID: %s\n", ssid); if (password) {
 *         Serial.printf("Password: %s\n", password);
 *     }
 *     if (input) {
 *         Serial.printf("Input: %s\n", input);
 *     }
 * });
 * ```
 */
WiFiProvisioner &WiFiProvisioner::onSuccess(SuccessCallback callback) {
  onSuccessCallback = std::move(callback);
  return *this;
}

/**
 * @brief Handles the HTTP `/` request.
 *
 * This function responds to the root URL (`/`) by sending an HTML page
 * composed of several predefined fragments and dynamic content based on the
 * Wi-Fi provisioning configuration.
 *
 */
void WiFiProvisioner::handleRootRequest() {
  WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_INFO,
                             "Handling root request from %s",
                             _server->client().remoteIP().toString().c_str());

  if (provisionCallback) {
    provisionCallback();
  }

  const char *showResetField = _config.SHOW_RESET_FIELD ? "true" : "false";

  char inputLengthStr[12];
  snprintf(inputLengthStr, sizeof(inputLengthStr), "%d", _config.INPUT_LENGTH);

  WiFiClient client = _server->client();
  if (!client.connected()) {
    WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_WARN,
                               "Client disconnected before sending response");
    return;
  }

  WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_DEBUG,
                             "Sending chunked HTML response to client");

  // Use chunked transfer for large content
  sendHeader(client, 200, "text/html", 0);  // 0 = use chunked transfer

  if (client.connected()) {
    // Send HTML in chunks to prevent buffer overflow
    char chunkBuffer[512];  // 512 byte chunks

    // Chunk 1: HTML start and title
    sendChunkedData(client, index_html1, strlen_P(index_html1));
    if (!client.connected()) goto cleanup;
    sendChunkedData(client, _config.HTML_TITLE, strlen(_config.HTML_TITLE));
    if (!client.connected()) goto cleanup;

    // Chunk 2: CSS and theme
    sendChunkedData(client, index_html2, strlen_P(index_html2));
    if (!client.connected()) goto cleanup;
    sendChunkedData(client, _config.THEME_COLOR, strlen(_config.THEME_COLOR));
    if (!client.connected()) goto cleanup;

    // Chunk 3: SVG and header
    sendChunkedData(client, index_html3, strlen_P(index_html3));
    if (!client.connected()) goto cleanup;
    sendChunkedData(client, _config.SVG_LOGO, strlen(_config.SVG_LOGO));
    if (!client.connected()) goto cleanup;

    // Chunk 4: Project info
    sendChunkedData(client, index_html4, strlen_P(index_html4));
    if (!client.connected()) goto cleanup;
    sendChunkedData(client, _config.PROJECT_TITLE, strlen(_config.PROJECT_TITLE));
    if (!client.connected()) goto cleanup;

    sendChunkedData(client, index_html5, strlen_P(index_html5));
    if (!client.connected()) goto cleanup;
    sendChunkedData(client, _config.PROJECT_SUB_TITLE, strlen(_config.PROJECT_SUB_TITLE));
    if (!client.connected()) goto cleanup;

    sendChunkedData(client, index_html6, strlen_P(index_html6));
    if (!client.connected()) goto cleanup;
    sendChunkedData(client, _config.PROJECT_INFO, strlen(_config.PROJECT_INFO));
    if (!client.connected()) goto cleanup;

    // Chunk 5: Input and form elements
    sendChunkedData(client, index_html7, strlen_P(index_html7));
    if (!client.connected()) goto cleanup;
    sendChunkedData(client, _config.INPUT_TEXT, strlen(_config.INPUT_TEXT));
    if (!client.connected()) goto cleanup;

    sendChunkedData(client, index_html8, strlen_P(index_html8));
    if (!client.connected()) goto cleanup;
    sendChunkedData(client, inputLengthStr, strlen(inputLengthStr));
    if (!client.connected()) goto cleanup;

    // Chunk 6: Success message and footer
    sendChunkedData(client, index_html9, strlen_P(index_html9));
    if (!client.connected()) goto cleanup;
    sendChunkedData(client, _config.CONNECTION_SUCCESSFUL, strlen(_config.CONNECTION_SUCCESSFUL));
    if (!client.connected()) goto cleanup;

    sendChunkedData(client, index_html10, strlen_P(index_html10));
    if (!client.connected()) goto cleanup;
    sendChunkedData(client, _config.FOOTER_TEXT, strlen(_config.FOOTER_TEXT));
    if (!client.connected()) goto cleanup;

    // Chunk 7: Reset and final HTML
    sendChunkedData(client, index_html11, strlen_P(index_html11));
    if (!client.connected()) goto cleanup;
    sendChunkedData(client, _config.RESET_CONFIRMATION_TEXT, strlen(_config.RESET_CONFIRMATION_TEXT));
    if (!client.connected()) goto cleanup;

    sendChunkedData(client, index_html12, strlen_P(index_html12));
    if (!client.connected()) goto cleanup;
    sendChunkedData(client, showResetField, strlen(showResetField));
    if (!client.connected()) goto cleanup;

    sendChunkedData(client, index_html13, strlen_P(index_html13));
    if (!client.connected()) goto cleanup;

    // End chunked transfer
    sendChunkedEnd(client);

    WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_DEBUG,
                               "Chunked HTML response sent successfully");
  } else {
    WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_WARN,
                               "Client disconnected before sending HTML content");
  }

cleanup:
  client.stop();
}

/**
 * @brief Handles the HTTP `/update` request.
 *
 * This function serves the `/update` endpoint of the web server. It generates
 * a JSON response that includes a list of available Wi-Fi networks with
 * details such as SSID, signal strength (RSSI), and authentication mode. It
 * also includes a flag `show_code` indicating whether the input field for
 * additional credentials is enabled.
 *
 * Example JSON Response:
 * ```
 * {
 *   "show_code": "false",
 *   "network": [
 *     { "ssid": "Network1", "rssi": 4, "authmode": 1 },
 *     { "ssid": "Network2", "rssi": 2, "authmode": 0 },
 *     { "ssid": "Network3", "rssi": 3, "authmode": 1 }
 *   ]
 * }
 * ```
 *
 * @note
 * - The `authmode` field indicates the security mode of the network:
 *   - `0`: Open (no password required)
 *   - `1`: Secured (password required)
 */
void WiFiProvisioner::handleUpdateRequest() {
  WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_INFO,
                             "Handling /update request from %s",
                             _server->client().remoteIP().toString().c_str());

  JsonDocument doc;

  doc["show_code"] = _config.SHOW_INPUT_FIELD;
  WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_DEBUG,
                             "Starting network scan for /update request...");
  networkScan(doc);

  WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_DEBUG,
                             "JSON document size: %zu bytes", measureJson(doc));

  WiFiClient client = _server->client();
  if (!client.connected()) {
    WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_WARN,
                               "Client disconnected before sending /update response");
    return;
  }

  sendHeader(client, 200, "application/json", measureJson(doc));
  if (client.connected()) {
    serializeJson(doc, client);
    client.flush();
    WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_DEBUG,
                               "JSON response sent successfully for /update");
  } else {
    WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_WARN,
                               "Client disconnected while sending /update JSON");
  }
  client.stop();
}

/**
 * @brief Handles the `/configure` HTTP request.
 *
 * This function expects a JSON payload containing Wi-Fi credentials and an
 * optional input field. It attempts to connect to the specified network and
 * validates the optional input field if provided.
 *
 * 1. Parses the incoming JSON payload for:
 *    - `ssid` (required): The Wi-Fi network name.
 *    - `password` (optional): The Wi-Fi password.
 *    - `code` (optional): Additional input for custom validation.
 *
 * 2. Attempts to connect to the network.
 *
 * 3. If the `inputCheckCallback` is set, invokes it and returns an
 * unsuccessful response if the validation fails.
 *
 * 4. If the connection and input check is successful, invokes the
 * `onSuccessCallback` with the `ssid`, `password` and `input`.
 *
 * Example JSON Payload:
 * ```
 * {
 *   "ssid": "MyNetwork",
 *   "password": "securepassword",
 *   "code": "1234"
 * }
 * ```
 */
void WiFiProvisioner::handleConfigureRequest() {
  if (!_server->hasArg("plain")) {
    WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_WARN,
                               "No 'plain' argument found in request");
    sendBadRequestResponse();
    return;
  }

  JsonDocument doc;
  auto error = deserializeJson(doc, _server->arg("plain"));
  if (error) {
    WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_WARN,
                               "JSON parsing failed: %s", error.c_str());
    sendBadRequestResponse();
    return;
  }

  const char *ssid_connect = doc["ssid"];
  const char *pass_connect = doc["password"];
  const char *input_connect = doc["code"];

  WIFI_PROVISIONER_DEBUG_LOG(
      WIFI_PROVISIONER_LOG_INFO, "SSID: %s, PASSWORD: %s, INPUT: %s",
      ssid_connect ? ssid_connect : "", pass_connect ? pass_connect : "",
      input_connect ? input_connect : "");

  if (!ssid_connect) {
    WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_WARN,
                               "SSID missing from request");
    sendBadRequestResponse();
    return;
  }

  WiFi.disconnect(false, true);
  delay(_wifiDelay);

  if (!connect(ssid_connect, pass_connect)) {
    WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_WARN,
                               "Failed to connect to WiFi: %s with password %s",
                               ssid_connect, pass_connect ? pass_connect : "");
    handleUnsuccessfulConnection("ssid");
    return;
  }

  if (input_connect && inputCheckCallback &&
      !inputCheckCallback(input_connect)) {
    WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_INFO,
                               "Input check callback failed.");
    handleUnsuccessfulConnection("code");
    return;
  }

  handleSuccesfulConnection();

  if (onSuccessCallback) {
    onSuccessCallback(ssid_connect, pass_connect, input_connect);
  }

  // Show success page for a while before closing the server
  delay(7000);

  // Signal to break from loop
  _serverLoopFlag = true;
}

/**
 * @brief Attempts to connect to the specified Wi-Fi network.
 *
 * @param ssid The SSID of the Wi-Fi network.
 * @param password The password for the Wi-Fi network. Pass `nullptr` or an
 * empty string for open networks.
 * @return `true` if the connection is successful; `false` otherwise.
 */
bool WiFiProvisioner::connect(const char *ssid, const char *password) {
  WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_INFO,
                             "Attempting to connect to SSID: %s", ssid);

  if (!ssid || strlen(ssid) == 0) {
    WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_ERROR,
                               "Invalid SSID provided");
    return false;
  }

  if (password && strlen(password) > 0) {
    WiFi.begin(ssid, password);
  } else {
    WiFi.begin(ssid);
  }

  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(_wifiDelay);

    if (millis() - startTime >= _wifiConnectionTimeout) {
      WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_ERROR,
                                 "WiFi connection timeout reached for SSID: %s",
                                 ssid);
      return false;
    }
  }

  WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_INFO,
                             "Successfully connected to SSID: %s", ssid);
  return true;
}

/**
 * @brief Sends a generic HTTP 400 Bad Request response.
 */
void WiFiProvisioner::sendBadRequestResponse() {
  WiFiClient client = _server->client();
  if (!client.connected()) {
    return;
  }

  sendHeader(client, 400, "text/html", 0);

  WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_WARN,
                             "Sent 400 Bad Request response to client");

  if (client.connected()) {
    client.flush();
  }
  client.stop();
}

/**
 * @brief Sends a success response to the HTTP client after a successful Wi-Fi
 * connection.
 */
void WiFiProvisioner::handleSuccesfulConnection() {
  JsonDocument doc;
  doc["success"] = true;

  WiFiClient client = _server->client();
  if (!client.connected()) {
    return;
  }

  sendHeader(client, 200, "application/json", measureJson(doc));

  if (client.connected()) {
    serializeJson(doc, client);
    client.flush();
  }
  client.stop();
}

/**
 * @brief Sends a failure response to the HTTP client when a Wi-Fi connection
 * or input check attempt fails.
 *
 * @param reason The reason for the failure (e.g., "ssid" or "code").
 */
void WiFiProvisioner::handleUnsuccessfulConnection(const char *reason) {
  JsonDocument doc;
  doc["success"] = false;
  doc["reason"] = reason;

  WiFiClient client = _server->client();
  if (!client.connected()) {
    WiFi.disconnect(false, true);
    return;
  }

  sendHeader(client, 200, "application/json", measureJson(doc));

  if (client.connected()) {
    serializeJson(doc, client);
    client.flush();
  }
  client.stop();

  WiFi.disconnect(false, true);
}

/**
 * @brief Handles the factory reset request and invokes the registered reset
 * callback.
 *
 * This function triggers the `factoryResetCallback` if set and performs any
 * required reset operations. After the reset, the provisioning UI is displayed
 * again.
 */
void WiFiProvisioner::handleResetRequest() {
  if (factoryResetCallback) {
    factoryResetCallback();
  }
  WIFI_PROVISIONER_DEBUG_LOG(WIFI_PROVISIONER_LOG_INFO,
                             "Factory reset completed. Reloading UI.");

  WiFiClient client = _server->client();
  if (!client.connected()) {
    return;
  }

  sendHeader(client, 200, "text/html", 0);

  if (client.connected()) {
    client.flush();
  }
  client.stop();
}
