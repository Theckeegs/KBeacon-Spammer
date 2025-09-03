#include <WiFi.h>
#include <WebServer.h>
#include <esp_wifi.h>
#include <vector>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <Wire.h>
#include <U8g2lib.h>

WebServer server(80);
DNSServer dnsServer;
Preferences preferences;

// Define pins for OLED
#define OLED_SDA 5
#define OLED_SCL 6

// Initialize U8g2 display with proper constructor for the 0.42" OLED
// Using hardware I2C instead of software I2C for better performance
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R2, U8X8_PIN_NONE, OLED_SCL, OLED_SDA);

// Define display dimensions and offsets
const int width = 72;
const int height = 40;
const int xOffset = 30;  // = (132-w)/2
const int yOffset = 12;  // = (64-h)/2

// DNS and hostname settings
const byte DNS_PORT = 53;
const char* HOSTNAME = "KBeacon";  // This will be used for KBeacon.local

// Configuration variables
String beaconMessage = "KBeacon";
String listMessages = ""; // For storing comma-separated SSIDs
bool broadcasting = false;
bool isVariationMode = true; // true = variation mode, false = list mode
const int maxBeaconsPerChannel = 50;
const int variationsPerBeacon = 50;  // 50 variations
uint8_t beaconPacket[128];
uint8_t macAddr[6];

// WiFi settings
String current_ap_ssid = "KBeacon";    // Default value
String current_ap_password = "KBpass123"; // Default value

// Built-in LED for status (RGB LED on ESP32-C3)
const int LED_PIN = 2;

// Array of symbols and spaces for variations
const char* symbols[] = {
    ".",          // Period
    "",          // Underscore
    " ",          // Space
    "  ",         // Double space
    "   ",        // Triple space
    "..",         // Double period
    "__",         // Double underscore
    ". ",         // Period space
    " .",         // Space period
    " ",         // Underscore space
    " _"          // Space underscore
};

const char* spaces[] = {
    "",           // No space
    " ",          // Single space
    "  ",         // Double space
    "   ",        // Triple space
    ".",          // Period
    "_"           // Underscore
};

std::vector<String> customSSIDs; // Store parsed SSIDs for list mode

// HTML for the configuration page with both modes
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>KBeacon Spammer</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial; padding: 20px; }
        .button { 
            background-color: #4CAF50;
            color: white;
            padding: 10px 20px;
            border: none;
            border-radius: 4px;
            cursor: pointer;
            margin: 5px;
        }
        .stop { background-color: #f44336; }
        .mode-btn { background-color: #2196F3; }
        input[type=text], textarea {
            padding: 8px;
            margin: 8px 0;
            border: 1px solid #ccc;
            border-radius: 4px;
            width: 100%%;
            max-width: 300px;
        }
        .mode-container {
            border: 1px solid #ddd;
            padding: 15px;
            margin: 10px 0;
            border-radius: 4px;
            background: %s;
        }
    </style>
</head>
<body>
    <h2>KBeacon Spammer</h2>

    <div class="mode-container" style="background: %s">
        <h3>Mode 1: Variation Broadcasting</h3>
        <form action="/update" method="POST">
            <label>Base Message (will create 50 variations):</label><br>
            <input type="text" name="message" value="%s"><br>
            <input type="hidden" name="mode" value="variation">
            <input type="submit" class="button" value="Update Message">
        </form>
    </div>

    <div class="mode-container" style="background: %s">
        <h3>Mode 2: Custom List Broadcasting</h3>
        <form action="/update" method="POST">
            <label>Custom SSIDs (comma-separated):</label><br>
            <textarea name="list" rows="4">%s</textarea><br>
            <input type="hidden" name="mode" value="list">
            <input type="submit" class="button" value="Update List">
        </form>
    </div>

    <div class="mode-container">
        <h3>WiFi Access Point Settings</h3>
        <form action="/updatewifi" method="POST">
            <label>Access Point Name:</label><br>
            <input type="text" name="ap_ssid" value="%s"><br>
            <label>Password (minimum 8 chars):</label><br>
            <input type="text" name="ap_password" value="%s"><br>
            <input type="submit" class="button" value="Update WiFi Settings">
        </form>
        <p><small>Note: Device will restart after updating WiFi settings</small></p>
    </div>

    <br>
    <form action="/switchmode" method="POST">
        <input type="submit" class="button mode-btn" value="Switch to %s Mode">
    </form>
    <br>
    <a href="/toggle"><button class="button %s">%s Broadcasting</button></a>

    <p>Status: %s</p>
    <p>Current Mode: %s</p>
    <p>Broadcasting: %s</p>
    <p>Connected devices: %d</p>
    <p><small>Last updated: %lu ms ago</small></p>
</body>
</html>
)rawliteral";

// New function to display welcome screen with custom font sizes
void displayWelcomeScreen() {
    u8g2.clearBuffer();
    
    // KBeacon with larger font
    u8g2.setFont(u8g2_font_7x13B_tr);  // Bold 7x13 font for header
    String title = "KBeacon";
    int titleWidth = title.length() * 7;  // Approximate width
    int x1 = xOffset + (width - titleWidth) / 2;
    if (x1 < xOffset) x1 = xOffset;
    u8g2.drawStr(x1, yOffset + 10, title.c_str());
    
    // IP Address with medium font
    u8g2.setFont(u8g2_font_6x10_tr);  // Medium 6x10 font for IP
    String ipStr = "192.168.4.1";
    int ipWidth = ipStr.length() * 6;
    int x2 = xOffset + (width - ipWidth) / 2;
    if (x2 < xOffset) x2 = xOffset;
    u8g2.drawStr(x2, yOffset + 22, ipStr.c_str());
    
    // URL with normal font
    u8g2.setFont(u8g2_font_5x7_tr);  // Smaller 5x7 font for URL
    String urlStr = "kbeacon.local";
    int urlWidth = urlStr.length() * 5;
    int x3 = xOffset + (width - urlWidth) / 2;
    if (x3 < xOffset) x3 = xOffset;
    u8g2.drawStr(x3, yOffset + 32, urlStr.c_str());
    
    u8g2.sendBuffer();
}

// Display Functions using U8G2 library
void displayMessage(const String& message) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tr); // Small font that fits well on 0.42" display
    
    // Center the message horizontally
    int textWidth = message.length() * 5; // Approximate width based on 5px per character
    int x = xOffset + (width - textWidth) / 2;
    if (x < xOffset) x = xOffset;
    
    u8g2.drawStr(x, yOffset + 10, message.c_str());
    u8g2.sendBuffer();
}

void displayTwoLineMessage(const String& line1, const String& line2) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tr);
    
    // Center each line horizontally
    int textWidth1 = line1.length() * 5;
    int x1 = xOffset + (width - textWidth1) / 2;
    if (x1 < xOffset) x1 = xOffset;
    
    int textWidth2 = line2.length() * 5;
    int x2 = xOffset + (width - textWidth2) / 2;
    if (x2 < xOffset) x2 = xOffset;
    
    u8g2.drawStr(x1, yOffset + 15, line1.c_str());
    u8g2.drawStr(x2, yOffset + 30, line2.c_str());
    u8g2.sendBuffer();
}

void displayStatusScreen() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tr);
    
    // Line 1 - AP name
    String apLine = "AP:" + current_ap_ssid;
    u8g2.drawStr(xOffset, yOffset + 12, apLine.c_str());
    
    // Line 2 - Mode
    String modeStr = isVariationMode ? "Mode:Variation" : "Mode:List";
    u8g2.drawStr(xOffset, yOffset + 24, modeStr.c_str());
    
    // Line 3 - Status
    String statusStr = broadcasting ? "Status:ON" : "Status:OFF";
    u8g2.drawStr(xOffset, yOffset + 36, statusStr.c_str());
    
    u8g2.sendBuffer();
}

// Basic utility functions
void blinkLED(int times, int delayTime = 100) {
    for(int i = 0; i < times; i++) {
        digitalWrite(LED_PIN, HIGH);  // ON
        delay(delayTime);
        digitalWrite(LED_PIN, LOW);  // OFF
        delay(delayTime);
    }
}

// Split comma-separated string into vector of SSIDs
void parseSSIDList(String list) {
    customSSIDs.clear();
    int start = 0;
    int end = list.indexOf(',');
    while (end >= 0) {
        String ssid = list.substring(start, end);
        ssid.trim();
        if (ssid.length() > 0 && ssid.length() <= 32) {
            customSSIDs.push_back(ssid);
        }
        start = end + 1;
        end = list.indexOf(',', start);
    }
    String lastSSID = list.substring(start);
    lastSSID.trim();
    if (lastSSID.length() > 0 && lastSSID.length() <= 32) {
        customSSIDs.push_back(lastSSID);
    }
}

void generateRandomMac(uint8_t* mac) {
    for(int i = 0; i < 6; i++) {
        mac[i] = random(256);
    }
    mac[0] |= 0x02;  // Set locally administered bit
    mac[0] &= 0xFE;  // Ensure unicast bit
}

void createBeaconPacket(const String& ssid) {
    // Beacon packet structure setup
    uint8_t packet[128] = {
        0x80, 0x00,             // Frame Control
        0x00, 0x00,             // Duration
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,   // Destination address
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Source address
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // BSSID
        0x00, 0x00,             // Sequence number
        // Fixed parameters
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Timestamp
        0x64, 0x00,             // Beacon interval
        0x11, 0x00,             // Capability info
        // SSID
        0x00                    // SSID parameter set
    };

    // Clear buffer and copy packet header
    memset(beaconPacket, 0, sizeof(beaconPacket));
    memcpy(beaconPacket, packet, sizeof(packet));

    // Set random MAC address
    generateRandomMac(macAddr);
    memcpy(&beaconPacket[10], macAddr, 6);
    memcpy(&beaconPacket[16], macAddr, 6);

    // Set SSID
    beaconPacket[36] = 0x00;  // SSID parameter set
    beaconPacket[37] = ssid.length();  // SSID length
    memcpy(&beaconPacket[38], ssid.c_str(), ssid.length());
}

String createVariation(int index) {
    int spaceIndex = index % 6;  // 6 space variations
    int symbolIndex = (index / 6) % 11;  // 11 symbols
    return beaconMessage + spaces[spaceIndex] + symbols[symbolIndex];
}

void broadcastBeacon() {
    if (!broadcasting) return;

    static unsigned long lastStatusPrint = 0;
    static unsigned long lastLedToggle = 0;
    const unsigned long STATUS_PRINT_INTERVAL = 5000;
    const unsigned long LED_TOGGLE_INTERVAL = 100;

    // Channels to broadcast on
    int channels[] = {1, 6, 11};

    if (isVariationMode) {
        // Variation Mode: Create variations of base message
        for (int ch = 0; ch < 3; ch++) {
            esp_wifi_set_channel(channels[ch], WIFI_SECOND_CHAN_NONE);

            for (int i = 0; i < variationsPerBeacon; i++) {
                String variation = createVariation(i);
                createBeaconPacket(variation);
                int packetSize = 38 + variation.length();

                for (int j = 0; j < maxBeaconsPerChannel; j++) {
                    esp_wifi_80211_tx(WIFI_IF_AP, beaconPacket, packetSize, false);
                    delayMicroseconds(10);
                }
            }
        }
    } else {
        // List Mode: Broadcast each SSID from the list
        for (int ch = 0; ch < 3; ch++) {
            esp_wifi_set_channel(channels[ch], WIFI_SECOND_CHAN_NONE);

            for (const String& ssid : customSSIDs) {
                createBeaconPacket(ssid);
                int packetSize = 38 + ssid.length();

                for (int j = 0; j < maxBeaconsPerChannel; j++) {
                    esp_wifi_80211_tx(WIFI_IF_AP, beaconPacket, packetSize, false);
                    delayMicroseconds(10);
                }
            }
        }
    }

    // Visual feedback
    if (millis() - lastLedToggle >= LED_TOGGLE_INTERVAL) {
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        lastLedToggle = millis();
    }

    // Status printing
    if (millis() - lastStatusPrint >= STATUS_PRINT_INTERVAL) {
        if (isVariationMode) {
            Serial.printf("Broadcasting %d variations of '%s'\n", 
                         variationsPerBeacon, beaconMessage.c_str());
        } else {
            Serial.printf("Broadcasting %d custom SSIDs\n", 
                         customSSIDs.size());
        }
        lastStatusPrint = millis();
    }
}

void handleRoot() {
    char temp[4000];
    String activeColor = "rgba(144, 238, 144, 0.2)";  // Light green
    String inactiveColor = "rgba(220, 220, 220, 0.2)"; // Light gray

    snprintf(temp, sizeof(temp), index_html, 
             isVariationMode ? activeColor.c_str() : inactiveColor.c_str(),
             isVariationMode ? activeColor.c_str() : inactiveColor.c_str(),
             beaconMessage.c_str(),
             !isVariationMode ? activeColor.c_str() : inactiveColor.c_str(),
             listMessages.c_str(),
             current_ap_ssid.c_str(),
             current_ap_password.c_str(),
             isVariationMode ? "List" : "Variation",
             broadcasting ? "stop" : "",
             broadcasting ? "Stop" : "Start",
             broadcasting ? "ACTIVE" : "STOPPED",
             isVariationMode ? "Variation" : "List",
             isVariationMode ? String(String(variationsPerBeacon) + " variations of '" + beaconMessage + "'").c_str() 
                           : String(String(customSSIDs.size()) + " custom SSIDs").c_str(),
             WiFi.softAPgetStationNum(),
             millis());

    server.send(200, "text/html", temp);
}

void handleUpdate() {
    if (server.hasArg("mode")) {
        String mode = server.arg("mode");
        if (mode == "variation" && server.hasArg("message")) {
            beaconMessage = server.arg("message");
            isVariationMode = true;
            Serial.printf("Updated to variation mode with message: %s\n", beaconMessage.c_str());
            displayMessage("Updated to: " + beaconMessage);
            delay(1000);
        } else if (mode == "list" && server.hasArg("list")) {
            listMessages = server.arg("list");
            parseSSIDList(listMessages);
            isVariationMode = false;
            Serial.printf("Updated to list mode with %d SSIDs\n", customSSIDs.size());
            displayMessage("List updated: " + String(customSSIDs.size()) + " SSIDs");
            delay(1000);
        }
    }
    
    // Only show status screen if we have connected clients
    if (WiFi.softAPgetStationNum() > 0) {
        displayStatusScreen();
    } else {
        displayWelcomeScreen();
    }
    
    server.sendHeader("Location", "/");
    server.send(303);
}

void handleUpdateWifi() {
    if (server.hasArg("ap_ssid") && server.hasArg("ap_password")) {
        String new_ssid = server.arg("ap_ssid");
        String new_password = server.arg("ap_password");

        // Validate inputs
        if (new_ssid.length() < 1 || new_password.length() < 8) {
            server.send(400, "text/plain", "SSID and password must be at least 1 and 8 characters respectively");
            return;
        }

        // Save to preferences
        preferences.begin("wifi-config", false);
        preferences.putString("ap_ssid", new_ssid);
        preferences.putString("ap_password", new_password);
        preferences.end();

        // Show update on OLED - using larger font for better visibility
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_7x13B_tr);  // Bold font for better visibility
        
        // Center the text
        String line1 = "WiFi updated";
        int line1Width = line1.length() * 7;
        int x1 = xOffset + (width - line1Width) / 2;
        if (x1 < xOffset) x1 = xOffset;
        u8g2.drawStr(x1, yOffset + 15, line1.c_str());
        
        String line2 = "Restarting...";
        int line2Width = line2.length() * 7;
        int x2 = xOffset + (width - line2Width) / 2;
        if (x2 < xOffset) x2 = xOffset;
        u8g2.drawStr(x2, yOffset + 30, line2.c_str());
        
        u8g2.sendBuffer();

        // Send response before restarting
        server.send(200, "text/plain", "WiFi settings updated. Device restarting...");
        delay(2000);

        // Restart ESP32
        ESP.restart();
    }
}

void handleSwitchMode() {
    isVariationMode = !isVariationMode;
    Serial.printf("Switched to %s mode\n", isVariationMode ? "variation" : "list");

    displayMessage(isVariationMode ? "Mode: Variation" : "Mode: List");
    delay(1000);
    
    // Only show status screen if we have connected clients
    if (WiFi.softAPgetStationNum() > 0) {
        displayStatusScreen();
    } else {
        displayWelcomeScreen();
    }

    server.sendHeader("Location", "/");
    server.send(303);
}

void handleToggle() {
    broadcasting = !broadcasting;
    Serial.printf("Broadcasting %s\n", broadcasting ? "started" : "stopped");

    if (broadcasting) {
        blinkLED(4);
        displayMessage("Broadcasting ON");
    } else {
        blinkLED(2);
        digitalWrite(LED_PIN, LOW); // LED off
        displayMessage("Broadcasting OFF");
    }

    delay(1000);
    
    // Only show status screen if we have connected clients
    if (WiFi.softAPgetStationNum() > 0) {
        displayStatusScreen();
    } else {
        displayWelcomeScreen();
    }

    server.sendHeader("Location", "/");
    server.send(303);
}

void handleNotFound() {
    // For captive portal - redirect all requests to the main page
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
    server.send(302, "text/plain", "");
}

void setup() {
    randomSeed(esp_random());
    Serial.begin(115200);

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW); // LED off

    // Initialize OLED Display with U8g2
    u8g2.begin();
    u8g2.setContrast(255);  // Set max contrast for better visibility
    u8g2.setBusClock(400000);  // Set I2C speed to 400kHz
    
    displayMessage("Starting...");
    delay(1000);

    Serial.println("\n\n=== KBeacon OLED Starting ===");
    Serial.println("Board: ESP32-C3 0.42 OLED");

    // Load saved WiFi settings if they exist
    preferences.begin("wifi-config", false);
    current_ap_ssid = preferences.getString("ap_ssid", "KBeacon");
    current_ap_password = preferences.getString("ap_password", "KBpass123");
    preferences.end();

    // Initialize WiFi
    WiFi.disconnect();
    WiFi.mode(WIFI_AP);
    esp_wifi_set_mode(WIFI_MODE_AP);

    // Show WiFi setup on OLED with custom formatting
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_4x6_tr);  // Medium size font
    
    String setupMsg = "Setting up WIFI";
    int msgWidth = setupMsg.length() * 4;  // Approximate width based on 6px per character
    int x = xOffset + (width - msgWidth) / 2;
    if (x < xOffset) x = xOffset;
    
    u8g2.drawStr(x, yOffset + 20, setupMsg.c_str());
    u8g2.sendBuffer();
    delay(1000);

    if(WiFi.softAP(current_ap_ssid.c_str(), current_ap_password.c_str(), 6)) {
        Serial.println("✓ WiFi Access Point Created Successfully");
        Serial.println("  SSID: " + current_ap_ssid);
        Serial.println("  Password: " + current_ap_password);
        Serial.println("  AP IP: " + WiFi.softAPIP().toString());
        blinkLED(3);

        // Display the welcome screen with different font sizes
        displayWelcomeScreen();
        
    } else {
        Serial.println("✗ Failed to create WiFi Access Point!");

        // Show error on OLED
        displayMessage("WiFi AP Error!");

        while(1) {
            blinkLED(5, 50);  // Rapid blinking indicates error
            delay(500);
        }
    }

    // Start mDNS responder
    if(MDNS.begin(HOSTNAME)) {
        Serial.println("✓ mDNS responder started");
        Serial.println("  You can access the web interface at:");
        Serial.println("  http://" + String(HOSTNAME) + ".local");
    } else {
        Serial.println("✗ Error setting up mDNS responder!");
    }

    // Configure captive portal
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    Serial.println("✓ Captive portal started");

    // Setup web server with all handlers
    server.onNotFound(handleNotFound);  // For captive portal
    server.on("/", handleRoot);
    server.on("/update", handleUpdate);
    server.on("/updatewifi", handleUpdateWifi);
    server.on("/switchmode", handleSwitchMode);
    server.on("/toggle", handleToggle);
    server.begin();

    Serial.println("✓ Web server started");
    Serial.println("=== Setup Complete! ===");

    // Final success indication
    blinkLED(5, 200);

    // Don't show status screen at the end of setup - we'll only change display when a client connects
    // displayStatusScreen();  <-- This line is removed
}

// Modify the loop() function to only update the display when clients connect/disconnect
void loop() {
    // Handle DNS requests for captive portal
    dnsServer.processNextRequest();

    // Handle web server clients
    server.handleClient();

    // Handle beacon broadcasting
    broadcastBeacon();

    // Check for client connections
    static int lastClientCount = 0;
    static bool hasShownStatus = false;
    int currentClientCount = WiFi.softAPgetStationNum();

    // Only transition to status screen when a client connects
    if (currentClientCount > 0 && !hasShownStatus) {
        // First client connected - now show the status screen
        displayMessage("Client connected!");
        delay(1000);
        displayStatusScreen();
        hasShownStatus = true;
    } 
    // If clients just disconnected and we're back to zero clients
    else if (currentClientCount == 0 && lastClientCount > 0) {
        // All devices disconnected - go back to the welcome screen
        hasShownStatus = false;
        displayMessage("Client disconnected");
        delay(1000);
        displayWelcomeScreen();
    }
    // Just update the client count display if it changed but other clients remain
    else if (currentClientCount != lastClientCount && hasShownStatus) {
        // Client count changed but we still have clients
        if (currentClientCount > lastClientCount) {
            displayMessage("Client connected");
        } else {
            displayMessage("Client left");
        }
        delay(1000);
        displayStatusScreen();
    }

    lastClientCount = currentClientCount;
}