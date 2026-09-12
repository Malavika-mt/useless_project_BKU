#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <WiFi.h>
#include <WebServer.h>

// ESP32-C3 specific low-level hardware register headers
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"

#define SDA_PIN 8
#define SCL_PIN 9
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C

Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
WebServer server(80);

// Wi-Fi hotspot credentials
const char* ssid = "HairClip-Mood";
const char* password = "password123";

// Current active state
String currentDisplay = "arada\nnaari\nnee";
bool isCatMood = false;
String catMoodType = "";

int bounceOffset = 0;
int bounceDir = 1;
unsigned long lastFrame = 0;
bool eyesClosed = false;
unsigned long lastBlink = 0;

// Text bounce coordinates
int textX = 5;
int textY = 5;
int textVx = 1;
int textVy = 1;

void getTextBounds(const String &str, uint8_t textSize, int &w, int &h) {
  int maxLineChars = 0;
  int currentLineChars = 0;
  int lineCount = 1;

  for (size_t i = 0; i < str.length(); i++) {
    if (str[i] == '\n') {
      if (currentLineChars > maxLineChars) maxLineChars = currentLineChars;
      currentLineChars = 0;
      lineCount++;
    } else {
      currentLineChars++;
    }
  }
  if (currentLineChars > maxLineChars) maxLineChars = currentLineChars;
  w = maxLineChars * (6 * textSize);
  h = lineCount * (8 * textSize);
}

// Mobile Web Interface HTML
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Mood Controller</title>
  <style>
    body { font-family: -apple-system, sans-serif; background: #0f172a; color: #f8fafc; text-align: center; margin: 0; padding: 20px; }
    h2 { margin-bottom: 20px; font-weight: 600; }
    .btn-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; margin-bottom: 24px; }
    button { background: #3b82f6; border: none; color: white; padding: 14px 10px; font-size: 15px; font-weight: bold; border-radius: 10px; cursor: pointer; box-shadow: 0 4px 6px rgba(0,0,0,0.3); }
    button:active { background: #1d4ed8; transform: scale(0.98); }
    .cat-btn { background: #ec4899; }
    .cat-btn:active { background: #be185d; }
    .custom-box { background: #1e293b; padding: 16px; border-radius: 12px; }
    input[type="text"] { width: 85%; padding: 12px; border-radius: 8px; border: 1px solid #475569; background: #0f172a; color: #fff; font-size: 16px; margin-bottom: 12px; text-align: center; }
    .send-btn { background: #10b981; width: 90%; }
    .send-btn:active { background: #047857; }
  </style>
</head>
<body>
  <h2>✨ Hair Clip Dashboard</h2>
  <div class="btn-grid">
    <button onclick="setMood('arada\\nnaari\\nnee', false)">Arada Naari</button>
    <button onclick="setMood('Aare\\nKanikkana', false)">Aare Kanikkana</button>
    <button onclick="setMood('Thookiyallo\\nnadhaa', false)">Thookiyallo</button>
    <button class="cat-btn" onclick="setMood('angry', true)">🐱 Angry</button>
    <button class="cat-btn" onclick="setMood('sleepy', true)">🐱 Sleepy</button>
    <button class="cat-btn" onclick="setMood('hungry', true)">🐱 Hungry</button>
    <button class="cat-btn" onclick="setMood('sassy', true)">🐱 Sassy</button>
  </div>
  <div class="custom-box">
    <input type="text" id="customInput" placeholder="Type custom text...">
    <button class="send-btn" onclick="sendCustom()">Send Text</button>
  </div>
  <script>
    function setMood(val, isCat) {
      fetch('/set?text=' + encodeURIComponent(val) + '&cat=' + (isCat ? '1' : '0'));
    }
    function sendCustom() {
      let val = document.getElementById('customInput').value;
      if(val) setMood(val, false);
    }
  </script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send(200, "text/html", index_html);
}

void handleSet() {
  if (server.hasArg("text")) {
    currentDisplay = server.arg("text");
    currentDisplay.replace("\\n", "\n");
    
    isCatMood = (server.arg("cat") == "1");
    if (isCatMood) {
      catMoodType = currentDisplay;
    }

    textX = 5;
    textY = 5;
  }
  server.send(200, "text/plain", "OK");
}

void setup() {
  // 1. Completely disable the hardware brownout reset detector on ESP32-C3
  REG_CLR_BIT(RTC_CNTL_BROWN_OUT_REG, RTC_CNTL_BROWN_OUT_ENA);

  // 2. Reduce CPU clock frequency from 160MHz to 80MHz to cut power draw
  setCpuFrequencyMhz(80);

  Serial.begin(115200);
  delay(100);

  Wire.begin(SDA_PIN, SCL_PIN);
  display.begin(OLED_ADDR, true);
  display.clearDisplay();
  display.display();

  // 3. Wi-Fi SoftAP setup
  WiFi.persistent(false);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);

  // Set lower RF transmission power to avoid voltage drops
  WiFi.setTxPower(WIFI_POWER_8_5dBm);

  // Channel 1, WPA2 Auth
  bool apStarted = WiFi.softAP(ssid, password, 1, 0, 4);

  if (apStarted) {
    Serial.println("Wi-Fi Hotspot Started Successfully!");
    Serial.print("SSID: ");
    Serial.println(ssid);
    Serial.print("IP Address: ");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("Failed to start Wi-Fi Hotspot.");
  }

  // 4. Web server routes
  server.on("/", handleRoot);
  server.on("/set", handleSet);
  server.begin();
  Serial.println("Web server active on port 80");
}

void loop() {
  server.handleClient();

  // Animation update (~25 FPS)
  if (millis() - lastFrame > 40) {
    bounceOffset += bounceDir;
    if (bounceOffset > 4 || bounceOffset < -4) bounceDir *= -1;

    if (!isCatMood) {
      int textW, textH;
      getTextBounds(currentDisplay, 2, textW, textH);

      textX += textVx;
      textY += textVy;

      if (textX <= 0) { textX = 0; textVx = -textVx; }
      else if (textX + textW >= SCREEN_WIDTH) { textX = SCREEN_WIDTH - textW; textVx = -textVx; }

      if (textY <= 0) { textY = 0; textVy = -textVy; }
      else if (textY + textH >= SCREEN_HEIGHT) { textY = SCREEN_HEIGHT - textH; textVy = -textVy; }
    }

    lastFrame = millis();
  }

  // Face blinking
  if (millis() - lastBlink > 2500) {
    eyesClosed = true;
    if (millis() - lastBlink > 2650) {
      eyesClosed = false;
      lastBlink = millis();
    }
  }

  drawScreen();
  delay(5);
}

void drawScreen() {
  display.clearDisplay();

  if (isCatMood) {
    int cx = 64;
    int cy = 24 + bounceOffset;

    display.fillTriangle(cx - 25, cy - 10, cx - 15, cy - 25, cx - 8, cy - 10, SH110X_WHITE);
    display.fillTriangle(cx + 25, cy - 10, cx + 15, cy - 25, cx + 8, cy - 10, SH110X_WHITE);
    display.drawCircle(cx, cy, 20, SH110X_WHITE);

    if (eyesClosed) {
      display.drawLine(cx - 18, cy - 3, cx - 8, cy - 3, SH110X_WHITE);
      display.drawLine(cx + 8, cy - 3, cx + 18, cy - 3, SH110X_WHITE);
    } else if (catMoodType == "angry") {
      display.drawLine(cx - 18, cy - 8, cx - 8, cy - 3, SH110X_WHITE);
      display.drawLine(cx + 18, cy - 8, cx + 8, cy - 3, SH110X_WHITE);
    } else if (catMoodType == "sleepy") {
      display.drawLine(cx - 18, cy - 2, cx - 8, cy - 2, SH110X_WHITE);
      display.drawLine(cx + 8, cy - 2, cx + 18, cy - 2, SH110X_WHITE);
    } else {
      display.fillCircle(cx - 13, cy - 3, 3, SH110X_WHITE);
      display.fillCircle(cx + 13, cy - 3, 3, SH110X_WHITE);
    }

    display.drawLine(cx - 22, cy + 8, cx - 35, cy + 4, SH110X_WHITE);
    display.drawLine(cx - 22, cy + 10, cx - 35, cy + 10, SH110X_WHITE);
    display.drawLine(cx + 22, cy + 8, cx + 35, cy + 4, SH110X_WHITE);
    display.drawLine(cx + 22, cy + 10, cx + 35, cy + 10, SH110X_WHITE);

    if (catMoodType == "angry") {
      for (int i = -6; i <= 6; i++) display.drawPixel(cx + i, cy + 12 + (i * i) / 8, SH110X_WHITE);
    } else if (catMoodType == "hungry") {
      display.drawCircle(cx, cy + 13, 4, SH110X_WHITE);
    } else {
      for (int i = -6; i <= 6; i++) display.drawPixel(cx + i, cy + 12 - (i * i) / 12, SH110X_WHITE);
    }

    int textWidth = catMoodType.length() * 6;
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(cx - (textWidth / 2), 54);
    display.println(catMoodType);

  } else {
    display.setTextSize(2);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(textX, textY);
    display.println(currentDisplay);
  }

  display.display();
}