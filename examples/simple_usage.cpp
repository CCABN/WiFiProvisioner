#include <WiFiProvisioner.h>
#include <WiFi.h>

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("Starting WiFi Provisioning...");

    // Create provisioner instance
    WiFiProvisioner provisioner("My Device Setup");

    // This will block until user provides credentials
    WiFiCredentials creds = provisioner.getCredentials();

    if (creds.success) {
        Serial.printf("Got credentials!\n");
        Serial.printf("SSID: %s\n", creds.ssid.c_str());
        Serial.printf("Password: %s\n", creds.password.c_str());

        // Now you can use the credentials to connect to WiFi
        Serial.println("Attempting to connect to WiFi...");
        WiFi.begin(creds.ssid.c_str(), creds.password.c_str());

        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Serial.print(".");
            attempts++;
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\nConnected to WiFi!");
            Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
        } else {
            Serial.println("\nFailed to connect to WiFi");
        }

    } else {
        Serial.printf("Failed to get credentials: %s\n", creds.error.c_str());
    }
}

void loop() {
    // Your main application code here
    delay(1000);
}