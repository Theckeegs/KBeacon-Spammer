#include <WiFi.h>
#include <WebServer.h>
#include <esp_wifi.h>
#include <vector>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <Preferences.h>

// Include TFT and Touchscreen libraries for CYD
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

// Global color definitions for consistent UI
#define PURPLE_DARK  0x4013  // Dark purple
#define PURPLE_MID   0x601F  // Medium purple
#define PURPLE_LIGHT 0x909F  // Light purple
#define RED_DEEP     0x8800  // Deep red
#define RED_BRIGHT   0xF800  // Bright red
#define GREEN_STATUS 0x0480  // Dark green for "OFF" status

WebServer server(80);
DNSServer dnsServer;
Preferences preferences;

// Initialize TFT display and touchscreen
TFT_eSPI tft = TFT_eSPI();

// Touchscreen pins - these are predefined in User_Setup.h
#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

// Initialize SPI for touchscreen and create touchscreen instance
SPIClass touchscreenSPI = SPIClass(HSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);

// Define display dimensions
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define FONT_SIZE 2

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
const int LED_RED = 4;
const int LED_GREEN = 16;
const int LED_BLUE = 17;

// Array of symbols and spaces for variations
const char* symbols[] = {
    ".",          // Period
    "_",          // Underscore
    " ",          // Space
    "  ",         // Double space
    "   ",        // Triple space
    "..",         // Double period
    "__",         // Double underscore
    ". ",         // Period space
    " .",         // Space period
    "_ ",         // Underscore space
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
        body { font-family: Arial; padding: 20px; background-color: #1a0120; color: white; }
        .button { 
            background-color: #4f0064;
            color: white;
            padding: 10px 20px;
            border: none;
            border-radius: 4px;
            cursor: pointer;
            margin: 5px;
        }
        .stop { background-color: #c00000; }
        .mode-btn { background-color: #5e2075; }
        input[type=text], textarea {
            padding: 8px;
            margin: 8px 0;
            border: 1px solid #9933cc;
            border-radius: 4px;
            width: 100%%;
            max-width: 300px;
            background-color: #2a0130;
            color: white;
        }
        .mode-container {
            border: 1px solid #9933cc;
            padding: 15px;
            margin: 10px 0;
            border-radius: 4px;
            background: %s;
        }
        h2, h3 { color: #ff0000; }
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

// Set RGB LED state
void setRgbLed(int r, int g, int b) {
    digitalWrite(LED_RED, r ? HIGH : LOW);
    digitalWrite(LED_GREEN, g ? HIGH : LOW);
    digitalWrite(LED_BLUE, b ? HIGH : LOW);
}

// Basic utility functions
void blinkLED(int times, int delayTime = 100) {
    for(int i = 0; i < times; i++) {
        setRgbLed(1, 0, 0);  // Red
        delay(delayTime);
        setRgbLed(0, 0, 0);  // Off
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
    // There are 6 different space types and 11 different symbol types
    // Create an index system to get more unique combinations
    int spaceIndex = index % 6;
    int symbolIndex = (index / 6) % 11;
    
    // Get the base variation
    String result = beaconMessage + spaces[spaceIndex] + symbols[symbolIndex];
    
    // For additional variations, add character substitutions
    if (index > 60) {
        // Replace some characters with similar looking ones
        result.replace('a', '@');
        result.replace('e', '3');
        result.replace('i', '1');
        result.replace('o', '0');
    }
    
    return result;
}

// Display a simple message with purple/red theme
void displayMessage(const String& message) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(PURPLE_LIGHT);
    
    // Center the message horizontally
    int textWidth = message.length() * 12; // Approximate width based on text size
    int x = (SCREEN_WIDTH - textWidth) / 2;
    if (x < 0) x = 0;
    
    // Add a background box for the message
    int boxWidth = textWidth + 20;
    int boxHeight = 40;
    int boxX = (SCREEN_WIDTH - boxWidth) / 2;
    int boxY = SCREEN_HEIGHT/2 - 20;
    
    tft.fillRoundRect(boxX, boxY, boxWidth, boxHeight, 8, PURPLE_DARK);
    tft.drawRoundRect(boxX, boxY, boxWidth, boxHeight, 8, PURPLE_LIGHT);
    
    tft.drawString(message, x, SCREEN_HEIGHT/2 - 10);
}

// Display two lines of text with purple/red theme
void displayTwoLineMessage(const String& line1, const String& line2) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    
    // Center each line horizontally
    int textWidth1 = line1.length() * 12;
    int x1 = (SCREEN_WIDTH - textWidth1) / 2;
    if (x1 < 0) x1 = 0;
    
    int textWidth2 = line2.length() * 12;
    int x2 = (SCREEN_WIDTH - textWidth2) / 2;
    if (x2 < 0) x2 = 0;
    
    // Add a background box for the messages
    int boxWidth = max(textWidth1, textWidth2) + 20;
    int boxHeight = 80;
    int boxX = (SCREEN_WIDTH - boxWidth) / 2;
    int boxY = SCREEN_HEIGHT/2 - 40;
    
    tft.fillRoundRect(boxX, boxY, boxWidth, boxHeight, 8, PURPLE_DARK);
    tft.drawRoundRect(boxX, boxY, boxWidth, boxHeight, 8, PURPLE_LIGHT);
    
    tft.setTextColor(RED_BRIGHT);
    tft.drawString(line1, x1, SCREEN_HEIGHT/2 - 30);
    
    tft.setTextColor(TFT_WHITE);
    tft.drawString(line2, x2, SCREEN_HEIGHT/2 + 10);
}

// Display four lines of text with purple/red theme and a Continue button
void displayFourLineMessage(const String& line1, const String& line2, const String& line3, const String& line4, bool showButton = true) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    
    // Center each line horizontally
    int textWidth1 = line1.length() * 12;
    int x1 = (SCREEN_WIDTH - textWidth1) / 2;
    if (x1 < 0) x1 = 0;
    
    int textWidth2 = line2.length() * 12;
    int x2 = (SCREEN_WIDTH - textWidth2) / 2;
    if (x2 < 0) x2 = 0;
    
    int textWidth3 = line3.length() * 12;
    int x3 = (SCREEN_WIDTH - textWidth3) / 2;
    if (x3 < 0) x3 = 0;
    
    int textWidth4 = line4.length() * 12;
    int x4 = (SCREEN_WIDTH - textWidth4) / 2;
    if (x4 < 0) x4 = 0;
    
    // Add a background box for the messages - make it taller
    int boxWidth = max(max(textWidth1, textWidth2), max(textWidth3, textWidth4)) + 20;
    int boxHeight = 140; // Increased height for 4 lines
    int boxX = (SCREEN_WIDTH - boxWidth) / 2;
    int boxY = SCREEN_HEIGHT/2 - 70; // Shifted up slightly
    
    tft.fillRoundRect(boxX, boxY, boxWidth, boxHeight, 8, PURPLE_DARK);
    tft.drawRoundRect(boxX, boxY, boxWidth, boxHeight, 8, PURPLE_LIGHT);
    
    tft.setTextColor(RED_BRIGHT);
    tft.drawString(line1, x1, SCREEN_HEIGHT/2 - 60);
    
    tft.setTextColor(TFT_WHITE);
    tft.drawString(line2, x2, SCREEN_HEIGHT/2 - 30);
    
    tft.setTextColor(TFT_WHITE);
    tft.drawString(line3, x3, SCREEN_HEIGHT/2);
    
    tft.setTextColor(TFT_WHITE);
    tft.drawString(line4, x4, SCREEN_HEIGHT/2 + 30);
    
    // Add Continue button if requested
    if (showButton) {
        // Draw Continue button
        tft.fillRoundRect(110, 200, 100, 30, 5, PURPLE_MID);
        tft.drawRoundRect(110, 200, 100, 30, 5, PURPLE_LIGHT);
        tft.setTextSize(1);
        tft.setTextColor(TFT_WHITE);
        tft.drawCentreString("CONTINUE", 160, 210, 1);
    }
}

// Wait for Continue button press
void waitForContinueButton() {
    bool buttonPressed = false;
    while (!buttonPressed) {
        if (touchscreen.tirqTouched() && touchscreen.touched()) {
            TS_Point p = touchscreen.getPoint();
            
            // Map touch coordinates to screen
            int x = map(p.x, 200, 3700, 0, SCREEN_WIDTH);
            int y = map(p.y, 200, 3700, 0, SCREEN_HEIGHT);
            
            // Check if continue button was pressed
            if (x >= 110 && x <= 210 && y >= 200 && y <= 230) {
                // Visual feedback
                tft.fillRoundRect(110, 200, 100, 30, 5, PURPLE_LIGHT);
                tft.drawRoundRect(110, 200, 100, 30, 5, TFT_WHITE);
                tft.setTextColor(TFT_BLACK);
                tft.drawCentreString("CONTINUE", 160, 210, 1);
                delay(100);
                
                buttonPressed = true;
            }
            
            // Clear any remaining touch events
            while (touchscreen.touched()) {
                touchscreen.getPoint();
                delay(10);
            }
        }
        delay(10);
    }
}

void broadcastBeacon() {
    if (!broadcasting) return;

    static unsigned long lastStatusPrint = 0;
    static unsigned long lastLedToggle = 0;
    const unsigned long STATUS_PRINT_INTERVAL = 5000;
    const unsigned long LED_TOGGLE_INTERVAL = 100;

    // Channels to broadcast on - use all three main channels for better visibility
    int channels[] = {1, 6, 11};
    
    // Visual feedback
    if (millis() - lastLedToggle >= LED_TOGGLE_INTERVAL) {
        static bool ledState = false;
        ledState = !ledState;
        if (ledState) {
            setRgbLed(1, 0, 1); // Purple when broadcasting (Red + Blue)
        } else {
            setRgbLed(0, 0, 0);
        }
        lastLedToggle = millis();
    }
    
    // Status printing
    if (millis() - lastStatusPrint >= STATUS_PRINT_INTERVAL) {
        if (isVariationMode) {
            Serial.printf("Broadcasting variations of '%s'\n", beaconMessage.c_str());
        } else {
            Serial.printf("Broadcasting %d custom SSIDs\n", customSSIDs.size());
        }
        lastStatusPrint = millis();
    }
    
    // Force disable promiscuous mode before broadcasting
    esp_wifi_set_promiscuous(false);
    delay(1);
    
    if (isVariationMode) {
        // Cycle through the channels
        for (int ch = 0; ch < 3; ch++) {
            esp_wifi_set_channel(channels[ch], WIFI_SECOND_CHAN_NONE);
            delay(1);
            
            // Create and broadcast all variations - using the full set
            for (int i = 0; i < variationsPerBeacon && i < 30; i++) {
                String variation = createVariation(i);
                createBeaconPacket(variation);
                int packetSize = 38 + variation.length();
                
                // Try both interfaces for broadcasting
                esp_wifi_80211_tx(WIFI_IF_STA, beaconPacket, packetSize, false);
                esp_wifi_80211_tx(WIFI_IF_AP, beaconPacket, packetSize, false);
                
                // Small delay between variations to ensure they're all visible
                delay(1);
            }
        }
    } else {
        // List Mode: Broadcast each SSID from the list
        if (customSSIDs.size() > 0) {
            // Cycle through the channels
            for (int ch = 0; ch < 3; ch++) {
                esp_wifi_set_channel(channels[ch], WIFI_SECOND_CHAN_NONE);
                delay(1);
                
                for (const String& ssid : customSSIDs) {
                    createBeaconPacket(ssid);
                    int packetSize = 38 + ssid.length();
                    
                    // Try both interfaces
                    esp_wifi_80211_tx(WIFI_IF_STA, beaconPacket, packetSize, false);
                    esp_wifi_80211_tx(WIFI_IF_AP, beaconPacket, packetSize, false);
                    
                    // Small delay between SSIDs
                    delay(1);
                }
            }
        } else {
            Serial.println("No SSIDs in list to broadcast");
        }
    }
}

// Function to create touch-sensitive button areas on the status screen
void displayStatusScreen() {
    tft.fillScreen(TFT_BLACK);
    
    // Draw info button in top left
    tft.fillRoundRect(5, 5, 50, 30, 5, PURPLE_MID);
    tft.drawRoundRect(5, 5, 50, 30, 5, PURPLE_LIGHT);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE);
    tft.drawCentreString("INFO", 30, 15, 1);
    
    // Main title - moved slightly to the right to make room for info button
    tft.setTextSize(3);
    tft.setTextColor(RED_BRIGHT);
    tft.drawCentreString("KBeacon", SCREEN_WIDTH/2 + 20, 5, 1);
    
    // Draw status section with touch-sensitive button areas
    // 1. Status Button
    tft.fillRoundRect(10, 50, 300, 50, 8, PURPLE_DARK);
    tft.drawRoundRect(10, 50, 300, 50, 8, PURPLE_LIGHT);
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("Status:", 30, 65);
    tft.setTextColor(broadcasting ? RED_BRIGHT : GREEN_STATUS);
    tft.drawString(broadcasting ? "BROADCASTING" : "OFF", 150, 65);
    
    // 2. Mode Button
    tft.fillRoundRect(10, 110, 300, 50, 8, PURPLE_DARK);
    tft.drawRoundRect(10, 110, 300, 50, 8, PURPLE_LIGHT);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("Mode:", 30, 125);
    tft.setTextColor(PURPLE_LIGHT);
    tft.drawString(isVariationMode ? "Variation" : "List", 150, 125);
    
    // 3. Content area - shows current SSID or list count
    tft.fillRoundRect(10, 170, 300, 60, 8, PURPLE_DARK);
    tft.drawRoundRect(10, 170, 300, 60, 8, PURPLE_LIGHT);
    tft.setTextColor(TFT_WHITE);
    String infoLabel = isVariationMode ? "Base SSID:" : "SSIDs in list:";
    String infoValue = isVariationMode ? beaconMessage : String(customSSIDs.size());
    tft.drawString(infoLabel, 30, 180);
    
    // Truncate if too long
    if (infoValue.length() > 15 && isVariationMode) {
        infoValue = infoValue.substring(0, 12) + "...";
    }
    tft.setTextColor(TFT_WHITE);
    tft.drawString(infoValue, 30, 205);
    
    // 4. Edit button - for editing the message or list
    if (isVariationMode || customSSIDs.size() == 0) {
        tft.fillRoundRect(220, 180, 80, 40, 8, RED_DEEP);
        tft.drawRoundRect(220, 180, 80, 40, 8, RED_BRIGHT);
        tft.setTextSize(1);
        tft.setTextColor(TFT_WHITE);
        tft.drawCentreString("EDIT", 260, 195, 2);
    }
}

// Check for touchscreen input with enhanced areas
void checkTouchInput() {
    if (touchscreen.tirqTouched() && touchscreen.touched()) {
        TS_Point p = touchscreen.getPoint();
        
        // Map touch coordinates to screen
        int x = map(p.x, 200, 3700, 0, SCREEN_WIDTH);
        int y = map(p.y, 200, 3700, 0, SCREEN_HEIGHT);
        
        // Check for info button press
        if (x >= 5 && x <= 55 && y >= 5 && y <= 35) {
            // Show WiFi info on display
            displayFourLineMessage(
                "AP: " + current_ap_ssid, 
                "IP: " + WiFi.softAPIP().toString(),
                "Portal: KBeacon.local",
                "Pass: " + current_ap_password
            );
            
            // Wait for button press to continue
            waitForContinueButton();
            
            // Return to main screen
            displayStatusScreen();
            delay(100); // Debounce
            return;
        }
        
        // Status button (toggle broadcasting)
        if (x >= 10 && x <= 310 && y >= 50 && y <= 100) {
            broadcasting = !broadcasting;
            if (broadcasting) {
                blinkLED(2);
                // Check if list mode but no SSIDs
                if (!isVariationMode && customSSIDs.size() == 0) {
                    broadcasting = false;
                    // Show error message
                    tft.fillRect(0, 240, SCREEN_WIDTH, 40, TFT_BLACK);
                    tft.setTextColor(RED_BRIGHT);
                    tft.setTextSize(1);
                    tft.drawCentreString("No SSIDs in list! Add SSIDs first.", SCREEN_WIDTH/2, 250, 2);
                    delay(2000);
                } else {
                    // Show success message
                    setRgbLed(0, 1, 0); // Green flash
                    delay(200);
                    setRgbLed(0, 0, 0);
                }
            } else {
                setRgbLed(1, 0, 0); // Red flash
                delay(200);
                setRgbLed(0, 0, 0);
            }
            displayStatusScreen();
            delay(200); // Debounce
        }
        
        // Mode button (switch between modes)
        else if (x >= 10 && x <= 310 && y >= 110 && y <= 160) {
            // If broadcasting, stop it when switching modes
            if (broadcasting) {
                broadcasting = false;
                setRgbLed(0, 0, 0);
            }
            
            isVariationMode = !isVariationMode;
            displayStatusScreen();
            delay(200); // Debounce
        }
        
        // Edit button
        else if (x >= 220 && x <= 300 && y >= 180 && y <= 220) {
            // Stop broadcasting while editing
            bool wasBroadcasting = broadcasting;
            if (broadcasting) {
                broadcasting = false;
                setRgbLed(0, 0, 0);
            }
            
            // Show input screen
            showInputScreen();
            
            // Resume broadcasting if it was on before
            broadcasting = wasBroadcasting;
            displayStatusScreen();
            delay(200); // Debounce
        }
    }
}

// Simple keyboard for text input - with dark purple and red color scheme
void showInputScreen() {
    String currentInput = isVariationMode ? beaconMessage : listMessages;
    bool inputDone = false;
    bool shiftMode = false;  // Track shift/caps state
    
    while (!inputDone) {
        // Draw input screen
        tft.fillScreen(TFT_BLACK);
        tft.setTextSize(2);
        tft.setTextColor(RED_BRIGHT);
        tft.drawCentreString("Edit Base SSID", SCREEN_WIDTH/2, 5, 1);
        
        // Input field - with purple borders
        tft.fillRoundRect(10, 40, 300, 30, 5, PURPLE_DARK);
        tft.drawRoundRect(10, 40, 300, 30, 5, PURPLE_LIGHT);
        
        // Truncate if too long for display
        String displayText = currentInput;
        if (displayText.length() > 22) {
            displayText = "..." + displayText.substring(displayText.length() - 19);
        }
        
        tft.setTextColor(TFT_WHITE);
        tft.setTextSize(1);
        tft.drawString(displayText, 15, 50);
        
        // Keyboard rows - we'll use String objects to avoid type conversion issues
        String keysLower[4][10] = {
            {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"},
            {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p"},
            {"a", "s", "d", "f", "g", "h", "j", "k", "l", "."},
            {"z", "x", "c", "v", "b", "n", "m", "_", ",", " "}
        };
        
        String keysUpper[4][10] = {
            {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"},
            {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P"},
            {"A", "S", "D", "F", "G", "H", "J", "K", "L", "."},
            {"Z", "X", "C", "V", "B", "N", "M", "_", ",", " "}
        };
        
        int keyW = 26;
        int keyH = 26;
        int startX = 15;
        int startY = 80;
        
        // Draw keyboard with purple keys
        for (int row = 0; row < 4; row++) {
            for (int col = 0; col < 10; col++) {
                int x = startX + col * (keyW + 2);
                int y = startY + row * (keyH + 4); // Reduced vertical spacing
                
                // Key background and borders
                tft.fillRoundRect(x, y, keyW, keyH, 3, PURPLE_DARK);
                tft.drawRoundRect(x, y, keyW, keyH, 3, PURPLE_MID);
                
                // Key text - use the String's c_str() method to get a const char*
                tft.setTextSize(1);
                tft.setTextColor(TFT_WHITE);
                tft.drawCentreString(shiftMode ? keysUpper[row][col].c_str() : keysLower[row][col].c_str(), 
                                    x + keyW/2, y + keyH/2 - 3, 1);
            }
        }
        
        // Special keys - positioned lower and more compact
        int buttonY = 200; // Moved buttons up from 230
        
        // Shift/Caps button
        tft.fillRoundRect(10, buttonY, 65, 35, 5, shiftMode ? PURPLE_LIGHT : PURPLE_MID);
        tft.drawRoundRect(10, buttonY, 65, 35, 5, TFT_WHITE);
        tft.setTextColor(shiftMode ? TFT_BLACK : TFT_WHITE);
        tft.drawCentreString("SHIFT", 42, buttonY + 15, 1);
        
        // Delete button
        tft.fillRoundRect(85, buttonY, 65, 35, 5, RED_DEEP);
        tft.drawRoundRect(85, buttonY, 65, 35, 5, RED_BRIGHT);
        tft.setTextColor(TFT_WHITE);
        tft.drawCentreString("DEL", 118, buttonY + 15, 1);
        
        // Cancel button
        tft.fillRoundRect(160, buttonY, 65, 35, 5, PURPLE_MID);
        tft.drawRoundRect(160, buttonY, 65, 35, 5, TFT_WHITE);
        tft.drawCentreString("CANCEL", 192, buttonY + 15, 1);
        
        // Save button
        tft.fillRoundRect(235, buttonY, 75, 35, 5, GREEN_STATUS);
        tft.drawRoundRect(235, buttonY, 75, 35, 5, PURPLE_LIGHT);
        tft.drawCentreString("SAVE", 272, buttonY + 15, 1);
        
        // Wait for touch
        while (!touchscreen.tirqTouched() || !touchscreen.touched()) {
            // Wait for touch
            delay(10);
        }
        
        TS_Point p = touchscreen.getPoint();
        
        // Map touch coordinates to screen
        int touchX = map(p.x, 200, 3700, 0, SCREEN_WIDTH);
        int touchY = map(p.y, 200, 3700, 0, SCREEN_HEIGHT);
        
        // Handle special keys
        if (touchY >= buttonY && touchY <= buttonY + 35) {
            if (touchX >= 10 && touchX <= 75) {
                // Toggle Shift/Caps
                shiftMode = !shiftMode;
            } else if (touchX >= 85 && touchX <= 150) {
                // Backspace
                if (currentInput.length() > 0) {
                    currentInput.remove(currentInput.length() - 1);
                }
            } else if (touchX >= 160 && touchX <= 225) {
                // Cancel
                inputDone = true;
            } else if (touchX >= 235 && touchX <= 310) {
                // Save
                if (isVariationMode) {
                    beaconMessage = currentInput;
                } else {
                    listMessages = currentInput;
                    parseSSIDList(listMessages);
                }
                inputDone = true;
            }
        }
        // Handle regular keys
        else if (touchY >= startY && touchY <= startY + 4*(keyH+4)) {
            for (int row = 0; row < 4; row++) {
                for (int col = 0; col < 10; col++) {
                    int x = startX + col * (keyW + 2);
                    int y = startY + row * (keyH + 4);
                    
                    if (touchX >= x && touchX <= x + keyW && 
                        touchY >= y && touchY <= y + keyH) {
                        
                        // Visual feedback - highlight pressed key
                        tft.fillRoundRect(x, y, keyW, keyH, 3, PURPLE_LIGHT);
                        tft.setTextColor(TFT_BLACK);
                        tft.drawCentreString(shiftMode ? keysUpper[row][col].c_str() : keysLower[row][col].c_str(), 
                                            x + keyW/2, y + keyH/2 - 3, 1);
                        delay(50);
                        
                        // Append character
                        currentInput += shiftMode ? keysUpper[row][col] : keysLower[row][col];
                    }
                }
            }
        }
        
        // Debounce
        delay(100);
        
        // Clear remaining touch events
        while (touchscreen.touched()) {
            touchscreen.getPoint();
            delay(10);
        }
    }
}

void handleRoot() {
    char temp[4000];
    String activeColor = "rgba(90, 0, 130, 0.4)";  // Purple for active
    String inactiveColor = "rgba(40, 0, 60, 0.2)"; // Dark purple for inactive

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
    displayStatusScreen();
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

        // Show update on display
        displayTwoLineMessage("WiFi updated", "Restarting...");

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
    displayStatusScreen();

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
        setRgbLed(0, 0, 0); // LED off
        displayMessage("Broadcasting OFF");
    }

    delay(1000);
    displayStatusScreen();

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

    // Initialize RGB LED pins
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_BLUE, OUTPUT);
    setRgbLed(0, 0, 0); // All LEDs off

    // Initialize TFT Display
    tft.init();
    tft.setRotation(1); // Landscape mode
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    
    // Start the SPI for the touchscreen and initialize it
    touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    touchscreen.begin(touchscreenSPI);
    touchscreen.setRotation(1); // Match TFT rotation
    
    // Show welcome screen
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(3);
    tft.setTextColor(RED_BRIGHT);
    tft.drawCentreString("KBeacon", SCREEN_WIDTH/2, SCREEN_HEIGHT/2 - 50, 2);
    tft.setTextSize(2);
    tft.setTextColor(PURPLE_LIGHT);
    tft.drawCentreString("WiFi Beacon Spammer", SCREEN_WIDTH/2, SCREEN_HEIGHT/2, 1);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE);
    tft.drawCentreString("Touchscreen Edition", SCREEN_WIDTH/2, SCREEN_HEIGHT/2 + 30, 1);
    
    delay(1000);

    Serial.println("\n\n=== KBeacon CYD Starting ===");
    Serial.println("Board: ESP32 Cheap Yellow Display");

    // Load saved WiFi settings if they exist
    preferences.begin("wifi-config", false);
    current_ap_ssid = preferences.getString("ap_ssid", "KBeacon");
    current_ap_password = preferences.getString("ap_password", "KBpass123");
    preferences.end();

    // Initialize WiFi for raw packet broadcasting with more advanced setup
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_OFF);
    delay(100);
    
    // Try mixed mode to enable both interfaces
    WiFi.mode(WIFI_AP_STA);
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    delay(100);
    
    // Try enabling promiscuous mode first, then disabling
    // This sometimes helps initialize the WiFi hardware properly
    esp_wifi_set_promiscuous(true);
    delay(50);
    esp_wifi_set_promiscuous(false);
    delay(50);

    // Show WiFi setup on display
    displayMessage("Setting up WiFi...");

    if(WiFi.softAP(current_ap_ssid.c_str(), current_ap_password.c_str(), 6)) {
        Serial.println("✓ WiFi Access Point Created Successfully");
        Serial.println("  SSID: " + current_ap_ssid);
        Serial.println("  Password: " + current_ap_password);
        Serial.println("  AP IP: " + WiFi.softAPIP().toString());
        blinkLED(3);

        // Show extended WiFi info on display with 4 lines and Continue button
        displayFourLineMessage(
            "AP: " + current_ap_ssid, 
            "IP: " + WiFi.softAPIP().toString(),
            "Portal: KBeacon.local",
            "Pass: " + current_ap_password
        );
        
        // Wait for button press instead of a timed delay
        waitForContinueButton();
    } else {
        Serial.println("✗ Failed to create WiFi Access Point!");

        // Show error on display
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

    // Increase WiFi TX power to maximum
    esp_wifi_set_max_tx_power(84);  // This is the maximum allowed value (20.5dBm)

    // Final success indication
    blinkLED(5, 200);

    // Show status screen
    displayStatusScreen();
}

void loop() {
    // Handle DNS requests for captive portal
    dnsServer.processNextRequest();

    // Handle web server clients
    server.handleClient();

    // Handle beacon broadcasting
    broadcastBeacon();
    
    // Check for touch input
    checkTouchInput();

    // Check for client connections
    static int lastClientCount = 0;
    int currentClientCount = WiFi.softAPgetStationNum();

    if (currentClientCount > lastClientCount) {
        // New device connected
        displayMessage("Client connected!");
        delay(1000);
        displayStatusScreen();
    } else if (currentClientCount < lastClientCount) {
        // Device disconnected
        displayMessage("Client left");
        delay(1000);
        displayStatusScreen();
    }

    lastClientCount = currentClientCount;
}