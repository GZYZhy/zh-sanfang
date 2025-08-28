#include <WiFi.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <FastLED.h>
#include <FS.h>
#include <LittleFS.h>

// === LED Configuration ===
#define LED_PIN 5
#define NUM_LEDS 8
CRGB leds[NUM_LEDS];

// === vMix Configuration ===
String vmixIP;
int inputNumber = 2; // Default vMix input
const int VMIX_TCP_PORT = 8099;
WiFiClient vmixClient;
bool connectedToVmix = false;
String tallyState = "Inactive"; // Track tally state
String styleContent;
int ledBrightness = 255; // default full brightness



// === Global Variables ===
WiFiManager wm;
WebServer server(80);
Preferences prefs;
bool apModeActive = false;
String deviceName = "Tallylight";
String activeHex = "#ff0000";
String previewHex = "#ffff00";


// === LED Functions ===
void blinkThreeTimes(CRGB color) {
  for (int i = 0; i < 3; i++) {
    fill_solid(leds, NUM_LEDS, color); // Set all LEDs to specified color
    FastLED.show();
    delay(500); // On for 500ms
    fill_solid(leds, NUM_LEDS, CRGB::Black); // Turn off
    FastLED.show();
    delay(500); // Off for 500ms
  }
}

void setAll(CRGB color) {
  fill_solid(leds, NUM_LEDS, color);
  FastLED.show();
}

void rainbowCycle(uint8_t wait, uint16_t cycles = 3) {
  uint16_t totalSteps = 256 * cycles;
  for (uint16_t j = 0; j < totalSteps; j++) {
    for (uint8_t i = 0; i < NUM_LEDS; i++) {
      leds[i] = CHSV((i * 256 / NUM_LEDS + j) % 256, 255, 255);
    }
    FastLED.show();
    delay(wait);
  }
  fill_solid(leds, NUM_LEDS, CRGB::Black); // turn off after animation
  FastLED.show();
}

void smoothPulseBlueToSolid(uint16_t duration_ms = 3000, float cycles = 2.0, uint8_t minBrightness = 40) {
  uint32_t start = millis();
  const uint8_t hue = 160; // Blue in HSV
  const uint16_t delay_ms = 10;
  const float twoPi = 2.0 * PI;

  while (millis() - start < duration_ms) {
    float elapsed = millis() - start;
    float progress = elapsed / (float)duration_ms; // 0.0 to 1.0
    float angle = progress * cycles * twoPi;
    float brightness = (sin(angle) + 1.0) / 2.0; // 0.0 to 1.0

    // Scale brightness from minBrightness to 255
    uint8_t scaled = minBrightness + brightness * (255 - minBrightness);
    fill_solid(leds, NUM_LEDS, CHSV(hue, 255, scaled));
    FastLED.show();
    delay(delay_ms);
  }

}

// Website Functions //
void updateVmixIp(String newIP) {
  if (newIP.length() && newIP != vmixIP) {
    prefs.begin("tally", false);
    vmixIP = newIP;
    prefs.putString("vmix_ip", vmixIP);
    prefs.end();
    Serial.println("💾 Saved new vMix IP: " + vmixIP);
    connectToVmix(); // Restart vMix connection
  }
}

void updateInputNumber(int newInput) {
  if (newInput > 0 && newInput != inputNumber) {
    prefs.begin("tally", false);
    inputNumber = newInput;
    prefs.putInt("input_number", inputNumber);
    prefs.end();
    Serial.println("💾 Saved new Input Number: " + String(inputNumber));
  }
}

void handleSave() {
  String newIP = server.arg("vmix_ip"); newIP.trim();
  int newInput = server.arg("input_number").toInt();
  String newDeviceName = server.arg("device_name"); newDeviceName.trim();
  activeHex = server.arg("active_color");
  previewHex = server.arg("preview_color");
  int newBrightness = server.arg("brightness").toInt();
  Serial.println("🎨 Received active color: " + activeHex);
  Serial.println("🎨 Received preview color: " + previewHex);

  bool nameChanged = false;

  prefs.begin("tally", false);

  if (newIP != vmixIP) {
    vmixIP = newIP;
    prefs.putString("vmix_ip", vmixIP);
  }
  if (newInput > 0 && newInput != inputNumber) {
    inputNumber = newInput;
    prefs.putInt("input_number", inputNumber);
  }
  if (newDeviceName != deviceName) {
    deviceName = newDeviceName;
    prefs.putString("device_name", deviceName);
    nameChanged = true;
  }
 if (newBrightness >= 1 && newBrightness <= 255 && newBrightness != ledBrightness) {
    ledBrightness = newBrightness;
    prefs.putInt("brightness", ledBrightness);
    FastLED.setBrightness(ledBrightness);
    // Reapply current tally color
    if (tallyState == "Program") {
      CRGB color = strtol(activeHex.c_str() + 1, nullptr, 16);
      setAll(color);
    } else if (tallyState == "Preview") {
      CRGB color = strtol(previewHex.c_str() + 1, nullptr, 16);
      setAll(color);
    } else {
      setAll(CRGB::Black);
    }
    Serial.println("💾 Saved Brightness: " + String(ledBrightness));
  }

  prefs.putString("active_color", activeHex);
  prefs.putString("preview_color", previewHex);
  prefs.end();

  if (nameChanged) {
    MDNS.end();
    delay(100);
    if (MDNS.begin(deviceName.c_str())) {
      Serial.println("✅ mDNS restarted with new name: " + deviceName);
    } else {
      Serial.println("❌ Failed to restart mDNS with new name");
    }
    String redirectHTML = "<html><head><meta http-equiv='refresh' content='3;url=http://" + deviceName + ".local/' /></head><body><h3>Device name changed to <b>" + deviceName + "</b>.</h3><p>Redirecting to <a href='http://" + deviceName + ".local/'>http://" + deviceName + ".local/</a> in 3 seconds...</p></body></html>";
    server.send(200, "text/html", redirectHTML);
  } else {
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "Settings saved, redirecting...");
  }
}

void handleRoot() {
  File file = LittleFS.open("/index.html", "r");
  if (!file) {
    server.send(404, "text/plain", "File not found");
    return;
  }
  String html = file.readString();
  file.close();

  html.replace("%VMIX_IP%", vmixIP);
  html.replace("%INPUT_NUMBER%", String(inputNumber));
  html.replace("%DEVICE_NAME%", deviceName);
  html.replace("%PREVIEW_COLOR%", previewHex);
  html.replace("%ACTIVE_COLOR%", activeHex);
  html.replace("%BRIGHTNESS%", String(ledBrightness));

  server.send(200, "text/html", html);
}

void handleReset() {
  Serial.println("⚠️ Reset requested via /reset – Clearing settings");

  String resetHTML = "<html><head><style>body{font-family:Arial;text-align:center;padding:2rem;background-color:#0c0c0c;color:#f3f3f3;}</style></head><body><h2>Tally Light is resetting...</h2><p>When the light turns solid blue, connect to the Wi-Fi network:</p><p><strong>Tallylight-Setup</strong></p><p>This page will no longer be accessible until Wi-Fi is configured.</p></body></html>";
  server.send(200, "text/html", resetHTML);

  server.client().flush();
  server.client().stop();
  delay(2000);

  wm.resetSettings();
  prefs.begin("tally", false);
  prefs.clear();
  prefs.end();

  Serial.println("🔄 Rebooting...");
  delay(1000);
  ESP.restart();
}


// === AP Mode Callback for Logging ===
void configModeCallback(WiFiManager* myWiFiManager) {
  Serial.println("⚙️ Entered AP mode – Connect to SSID: TallyLight-Setup");
  Serial.println("   AP IP: " + WiFi.softAPIP().toString());
  apModeActive = true;
}

void setupNetworkServices() {
  IPAddress localIP = WiFi.localIP();
  if (localIP == IPAddress(0, 0, 0, 0)) {
    Serial.println("⚠️ Invalid local IP (0.0.0.0) – Skipping mDNS and HTTP server setup");
    return;
  }
  if (MDNS.begin(deviceName.c_str())) {
    Serial.println("🌐 mDNS enabled – Access at http://" + deviceName + ".local");
  } else {
    Serial.println("❌ mDNS setup failed");
  }
  server.on("/", handleRoot);
  server.on("/reset", handleReset);
  server.on("/save", handleSave);
  server.begin();
  Serial.println("🔧 WebServer active – http://" + localIP.toString());
}

// === vMix Connection ===
void connectToVmix() {
  if (vmixIP.length() == 0) {
    Serial.println("⚠️ No vMix IP configured, skipping connection");
    connectedToVmix = false;
    return;
  }
  if (vmixClient.connect(vmixIP.c_str(), VMIX_TCP_PORT)) {
    Serial.println("✅ vMix TCP connected");
    delay(200);
    while (vmixClient.available()) vmixClient.readStringUntil('\n');
    vmixClient.println("SUBSCRIBE TALLY");
    if(!connectedToVmix)
    {
      blinkThreeTimes(CRGB::Purple);
    }
    connectedToVmix = true;
  } else {
    Serial.println("❌ vMix connect failed");
    connectedToVmix = false;
  }
}

  void doubleResetCheck() {
    static bool checkedBoot = false;
    static unsigned long bootTimestamp = 0;
    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(255);  // or lower if needed

    if (!checkedBoot) {
      prefs.begin("tally", false);
      int bootState = prefs.getInt("BootState", 0);
      bootState++;
      prefs.putInt("BootState", bootState);
      prefs.end();

      Serial.println("🚦 BootState incremented to: " + String(bootState));

      if (bootState >= 2) {
        Serial.println("🔥 BootState reached 2 — take action!");
        blinkThreeTimes(CRGB::Orange);
        handleReset();
      }

      bootTimestamp = millis();
      checkedBoot = true;
      Serial.println("3 Seconds to Unplug for Double Reset....");
      rainbowCycle(10, 2);
    }
  }

  void resetBootPrefs(){

    Serial.println("🔄 Resetting Boot Prefs - 0");
    prefs.begin("tally", false);
    prefs.putInt("BootState", 0);
    prefs.end();
  }

  void loadCustomStyle() {
  File f = LittleFS.open("/wifimanager_style.html", "r");
  if (f) {
    styleContent = f.readString();
    f.close();
    wm.setCustomHeadElement(styleContent.c_str()); // safe now!
  } else {
    Serial.println("⚠️ Failed to load style");
  }
}


void setup() {
  // Initialize serial for logging
  delay(200);
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n🚀 Tally Light Booting");
  doubleResetCheck();
  delay(500);
  resetBootPrefs();
  delay(500);
 // Initialize LittleFS
  if (!LittleFS.begin(true)) {
    Serial.println("❌ LittleFS mount failed");
    return;
  }

  prefs.begin("tally", false);

  // Load stored vMix IP
  vmixIP = prefs.getString("vmix_ip", "");
  if (vmixIP.length()) {
    Serial.println("📦 Loaded vMix IP: " + vmixIP);
  } else {
    Serial.println("📦 No vMix IP saved yet");
  }
  inputNumber = prefs.getInt("input_number", 2);
  Serial.println("📦 Loaded Input Number: " + String(inputNumber));
  int refusedCount = prefs.getInt("refused_count", 0);
  Serial.println("   Refused Count: " + String(refusedCount));
  deviceName = prefs.getString("device_name", "Tallylight");
  Serial.println("📦 Loaded Device Name: " + deviceName);
  activeHex = prefs.getString("active_color", "#ff0000");
  previewHex = prefs.getString("preview_color", "#ffff00");



  // Attempt to connect to saved Wi-Fi
  WiFi.mode(WIFI_OFF); // Fully reset Wi-Fi stack
  delay(100); // Brief delay to ensure Wi-Fi off
  WiFi.mode(WIFI_STA);
  Serial.print("🔌 Attempting to connect to saved Wi-Fi");
  Serial.println("\n   Saved SSID: " + wm.getWiFiSSID());
  Serial.println("   MAC Address: " + WiFi.macAddress());
  WiFi.disconnect(); // Disconnect any existing connections
  delay(500); // Delay to ensure disconnect completes

  unsigned long startTime = millis();
  WiFi.begin();

  // Wait up to 10 seconds for Wi-Fi connection
  int attemptCount = 0;
  bool connectionFailed = false;
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000) {
    Serial.print(".");
    delay(500);
    attemptCount++;
    // Check for repeated association failures
    if (attemptCount > 3 && WiFi.status() == WL_DISCONNECTED) {
      refusedCount++;
      prefs.putInt("refused_count", refusedCount);
      prefs.putInt("BootState", 0);
      if (refusedCount == 1) {
        Serial.println("\n⚠️ Wi-Fi association failed once – Restarting ESP32...");
        prefs.end();
        delay(1000); // Ensure NVS write completes
        ESP.restart(); // Restart to retry
      } else {
        Serial.println("\n⚠️ Wi-Fi association failed multiple times – Clearing settings and entering AP mode");
        wm.resetSettings(); // Clear Wi-Fi credentials
        prefs.clear(); // Clear all preferences, including RefusedCount
        connectionFailed = true;
        break;
      }
    }
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    prefs.putInt("refused_count", 0);
    Serial.println("✅ Wi-Fi Connected");
    Serial.println("   IP Address: " + WiFi.localIP().toString());
    setupNetworkServices();
    blinkThreeTimes(CRGB::Green);
    connectToVmix();
  } else {
    Serial.println("❌ Wi-Fi connection failed – Starting AP mode");
    smoothPulseBlueToSolid();
    WiFi.mode(WIFI_AP_STA);
    WiFiManagerParameter p_vmix("vmix_ip", "vMix IP", vmixIP.c_str(), 40);
    WiFiManagerParameter p_input("input_number", "Input Number", String(inputNumber).c_str(), 4);
    WiFiManagerParameter p_device("device_name", "Device Name", deviceName.c_str(), 32);
    wm.addParameter(&p_vmix);
    wm.addParameter(&p_input);
    wm.addParameter(&p_device);
    wm.setAPCallback(configModeCallback);
    wm.setDebugOutput(true);

    loadCustomStyle();

    bool portalOK = wm.startConfigPortal((deviceName + "-Setup").c_str());
    if (portalOK) {
      Serial.println("🧪 AP portal closed successfully");
      Serial.println("✅ Wi-Fi Connected via portal");
      Serial.println("   IP Address: " + WiFi.localIP().toString());
      String newIP = String(p_vmix.getValue());
      newIP.trim();
      int newInput = String(p_input.getValue()).toInt();
      String newDeviceName = String(p_device.getValue());
      newDeviceName.trim();
      if (newIP.length() && newIP != vmixIP) {
        vmixIP = newIP;
        prefs.putString("vmix_ip", vmixIP);
        Serial.println("💾 Saved new vMix IP: " + vmixIP);
      }
      if (newInput > 0 && newInput != inputNumber) {
        inputNumber = newInput;
        prefs.putInt("input_number", inputNumber);
        Serial.println("💾 Saved new Input Number: " + String(inputNumber));
      }
      if (newDeviceName.length() && newDeviceName != deviceName) {
        deviceName = newDeviceName;
        prefs.putString("device_name", deviceName);
        Serial.println("💾 Saved new Device Name: " + deviceName);
      }
      prefs.putInt("refused_count", 0);
      blinkThreeTimes(CRGB::Green);
      setupNetworkServices();
      connectToVmix();
    } else {
      Serial.println("🧪 AP portal failed or was interrupted");
      Serial.println("   AP mode active with IP: " + WiFi.softAPIP().toString());
      apModeActive = true;
    }
  }

  ledBrightness = prefs.getInt("brightness", 150);
  FastLED.setBrightness(ledBrightness);  // or lower if needed
  prefs.end();
  
}

void loop() {


  if (WiFi.status() == WL_CONNECTED) {
    server.handleClient();
  }

  if (WiFi.status() == WL_CONNECTED && connectedToVmix) {
    if (vmixClient.available()) {
      String line = vmixClient.readStringUntil('\n');
      line.trim();
      if (line.startsWith("TALLY OK")) {
        String tally = line.substring(9);
        char s = (inputNumber - 1 < tally.length())
                 ? tally.charAt(inputNumber - 1)
                 : '0';
        if (s == '1') {
          CRGB color = strtol(activeHex.c_str() + 1, nullptr, 16);
          setAll(color);
          tallyState = "Program";
        } else if (s == '2') {
          CRGB color = strtol(previewHex.c_str() + 1, nullptr, 16);
          setAll(color);
          tallyState = "Preview";
        } else {
          setAll(CRGB::Black);
          tallyState = "Inactive";
        }

      }
    } else {
      static unsigned long lastTry = 0;
      if (millis() - lastTry > 5000) {
        //connectToVmix();
        lastTry = millis();
      }
    }
  }
}