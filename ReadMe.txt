# KBeacon WiFi Beacon Spammer

**⚠️ LEGAL DISCLAIMER: This device is intended for educational and research purposes only. Broadcasting fake WiFi beacons may be illegal in your jurisdiction and could interfere with legitimate WiFi networks. Use responsibly and in compliance with local laws. The authors are not responsible for any misuse or legal consequences.**

## Overview

KBeacon is a WiFi beacon frame spammer that creates multiple fake WiFi access points (SSIDs) visible to nearby devices. It supports three different ESP32 development boards and offers two broadcasting modes: variation mode (creates multiple variations of a base SSID) and list mode (broadcasts custom SSIDs from a list).

## Supported Hardware

### 1. ESP32-C3 SuperMini
- **File**: `kbeacon_esp32c3.ino`
- **Features**: Basic version with LED status indicators
- **LED**: GPIO 8 (built-in LED, inverted logic)
- **Interface**: Web-based configuration only

### 2. ESP32-C3 with 0.42" OLED Display  
- **File**: `kbeacon_esp32c3_042.ino` (not provided in your code, but mentioned)
- **Features**: Web interface + small OLED display
- **Display**: Shows status and basic information

### 3. ESP32 CYD (Cheap Yellow Display)
- **File**: `kbeacon_cyd.ino`
- **Features**: Full touchscreen interface + web configuration
- **Display**: 320x240 TFT touchscreen with complete UI
- **LEDs**: RGB LED (GPIO 4, 16, 17)
- **Touch**: XPT2046 touchscreen controller

## Required Libraries

Install these libraries through the Arduino IDE Library Manager:

### Common Libraries (All Versions)
- `WiFi` (ESP32 built-in)
- `WebServer` (ESP32 built-in)
- `ESPmDNS` (ESP32 built-in)
- `DNSServer` (ESP32 built-in)
- `Preferences` (ESP32 built-in)

### CYD Version Additional Libraries
- `TFT_eSPI` - TFT display driver
- `XPT2046_Touchscreen` - Touchscreen driver

## Hardware Setup

### ESP32-C3 SuperMini
1. Connect via USB-C
2. No additional wiring required
3. Built-in LED on GPIO 8 provides status

### ESP32 CYD (Cheap Yellow Display)
1. Connect via USB-C or Micro-USB (depending on version)
2. All components (display, touchscreen, LEDs) are built-in
3. Ensure TFT_eSPI library is configured for your CYD variant

## Software Setup

### 1. Arduino IDE Configuration
- Install ESP32 board package: `https://dl.espressif.com/dl/package_esp32_index.json`
- Select appropriate board:
  - ESP32-C3: "ESP32C3 Dev Module"
  - CYD: "ESP32 Dev Module"

### 2. TFT_eSPI Configuration (CYD Only)
Create or modify `User_Setup.h` in your TFT_eSPI library folder:
```cpp
#define ILI9341_DRIVER
#define TFT_WIDTH  240
#define TFT_HEIGHT 320
#define TFT_MISO   12
#define TFT_MOSI   13
#define TFT_SCLK   14
#define TFT_CS     15
#define TFT_DC     2
#define TFT_RST    -1
```

### 3. Upload Code
1. Select the correct code file for your hardware
2. Compile and upload to your ESP32 device
3. Monitor serial output for setup confirmation

## Usage

### Initial Setup
1. Power on the device
2. Connect to the WiFi network "KBeacon" (default password: "KBpass123")
3. Access the web interface at:
   - `192.168.4.1` (direct IP)
   - `http://kbeacon.local` (mDNS)
   - Captive portal should appear automatically

### Operating Modes

#### Variation Mode (Default)
- Creates 50+ variations of a base SSID
- Adds different symbols, spaces, and character substitutions
- Example: "MyNetwork" becomes "MyNetwork.", "MyNetwork_", "MyNetwork ", etc.

#### List Mode
- Broadcasts custom SSIDs from a comma-separated list
- Each SSID appears as a separate network
- Maximum 32 characters per SSID

### Web Interface Controls

#### Configuration Options
- **Base Message**: Set the SSID for variation mode
- **Custom List**: Enter comma-separated SSIDs for list mode
- **Mode Switch**: Toggle between variation and list modes
- **Broadcasting**: Start/stop beacon transmission
- **WiFi Settings**: Change access point name and password

#### Status Information
- Current broadcasting status
- Active mode (Variation/List)
- Number of connected devices
- System uptime

### CYD Touchscreen Controls

The CYD version includes an intuitive touch interface:

#### Main Screen
- **INFO Button**: View WiFi connection details
- **Status Area**: Tap to toggle broadcasting on/off
- **Mode Area**: Tap to switch between modes
- **Edit Button**: Open on-screen keyboard for text input

#### On-Screen Keyboard
- Full QWERTY layout with symbols
- Shift key for uppercase letters
- Backspace, Cancel, and Save functions
- Real-time text preview

## Technical Details

### Broadcasting Specifications
- **Channels**: 1, 6, 11 (2.4GHz)
- **Packet Type**: 802.11 Beacon frames
- **Frame Rate**: ~50 beacons per variation per channel
- **TX Power**: Maximum (20.5dBm)

### Network Configuration
- **Default AP**: KBeacon / KBpass123
- **IP Address**: 192.168.4.1
- **DNS**: Captive portal enabled
- **mDNS**: kbeacon.local

### Memory Usage
- **Variation Mode**: ~50 variations stored
- **List Mode**: Dynamic SSID vector
- **Preferences**: WiFi settings saved to flash

## LED Status Indicators

### ESP32-C3 SuperMini
- **Solid Off**: Device ready/idle
- **Slow Blink**: Broadcasting active
- **Fast Blink**: Error condition
- **3 Blinks**: Successful WiFi setup
- **5 Blinks**: Setup complete

### CYD Version
- **RGB LED Colors**:
  - Red: Error or stopped
  - Green: Success flash
  - Purple (Red+Blue): Broadcasting active
  - Off: Idle

## Troubleshooting

### Common Issues

#### WiFi Access Point Not Visible
- Check if device powered on properly
- Verify LED status indicators
- Try power cycling the device
- Check for interference on channel 6

#### Web Interface Not Loading
- Ensure connected to "KBeacon" network
- Try direct IP: `192.168.4.1`
- Clear browser cache
- Disable cellular data on mobile devices

#### Touchscreen Not Responding (CYD)
- Verify TFT_eSPI configuration
- Check touchscreen calibration
- Ensure proper library versions

#### Beacons Not Broadcasting
- Confirm broadcasting is enabled
- Check that SSIDs are configured
- Verify ESP32 WiFi initialization
- Try restarting the device

### Recovery Mode
If the device becomes unresponsive:
1. Power cycle the device
2. WiFi settings reset to defaults after restart
3. Re-upload firmware if needed

## Safety and Legal Considerations

### Responsible Use
- Only use for educational/research purposes
- Test in isolated environments
- Avoid interference with critical networks
- Respect others' WiFi usage

### Legal Compliance
- Check local regulations before use
- Some jurisdictions prohibit beacon spoofing
- Consider power limitations in your area
- Obtain permission for testing in public spaces

### Privacy Considerations
- Device doesn't intercept or store data
- Only creates fake network names
- No actual network connectivity provided
- Monitor connected device count

## Technical Support

### Serial Debug Output
Connect via serial monitor (115200 baud) to view:
- Initialization status
- WiFi configuration
- Broadcasting statistics
- Error messages

### Configuration Reset
To reset to factory defaults:
1. Power off device
2. Hold reset button while powering on (if available)
3. Or re-flash firmware with fresh settings

## Version History

- **v1.0**: Basic ESP32-C3 support with web interface
- **v1.1**: Added CYD touchscreen support with full UI
- **v1.2**: Improved beacon generation and stability

## Contributing

This project is for educational purposes. When contributing:
- Ensure code follows responsible disclosure
- Test thoroughly before submitting changes
- Document all modifications
- Consider legal implications

## License

This project is provided as-is for educational purposes only. Users are responsible for compliance with local laws and regulations regarding WiFi broadcasting and spectrum usage.