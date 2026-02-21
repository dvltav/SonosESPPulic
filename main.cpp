#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <Wire.h>
#include <U8g2lib.h>

// Network
const char* ssid = "CHANGEME";
const char* password = "CHANGEME";

// Encoder pins
#define PIN_A D3
#define PIN_B D4
#define PIN_SW D7

// OLED Display
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// Sonos speakers
const int KITCHEN_S3 = 0;
const int DINNING_S1 = 1;
const char* speakers[] = {"192.168.1.166", "192.168.1.184"};
const int speakerCount = 2;

int currentVolume = 0;
int volumeOffset = 0;
volatile uint8_t encoderB_on_A_fall = 0;
volatile bool volumeChanged = false;
volatile unsigned long lastInterruptTime = 0;
volatile int encoderDelta = 0;

// UI state: 0=main, 1=dinning adjust, 2=menu
int uiState = 0;
int menuSelection = 0;
unsigned long stateChangeTime = 0;
const char* menuItems[] = {"Play/Pause", "Stations", "None", "Exit"};
const int menuItemCount = 4;

// Function declarations
int getSonosVolume(const char* ip);
void setSonosVolume(int volume);
void setSingleSpeakerVolume(const char* ip, int volume);
void updateDisplay();
void showDinningAdjust();
void showMenu();
void executeMenuAction();

void IRAM_ATTR onEncoderA() {
  unsigned long now = millis();
  if (now - lastInterruptTime < 100) return;
  
  uint8_t a = digitalRead(PIN_A);
  
  if (a == LOW) {
    encoderB_on_A_fall = digitalRead(PIN_B);
  } else {
    uint8_t b = digitalRead(PIN_B);
    if (encoderB_on_A_fall == HIGH && b == LOW) {
      encoderDelta = -1;
      lastInterruptTime = now;
    } else if (encoderB_on_A_fall == LOW && b == HIGH) {
      encoderDelta = 1;
      lastInterruptTime = now;
    }
  }
}

void setup() {
  Serial.begin(115200);
  
  pinMode(PIN_A, INPUT_PULLUP);
  pinMode(PIN_B, INPUT_PULLUP);
  pinMode(PIN_SW, INPUT_PULLUP);
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  
  // Initialize OLED
  Wire.begin(D2, D1);
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB14_tr);
  u8g2.drawStr(20, 30, "Sonos");
  u8g2.sendBuffer();
  delay(1000);
  
  // Attach interrupt to PIN_A
  attachInterrupt(digitalPinToInterrupt(PIN_A), onEncoderA, CHANGE);
  
  // Get initial volume
  currentVolume = getSonosVolume(speakers[KITCHEN_S3]);
  Serial.printf("Initial volume: %d\n", currentVolume);
  updateDisplay();
}

int getSonosVolume(const char* ip) {
  WiFiClient client;
  HTTPClient http;
  
  String url = "http://" + String(ip) + ":1400/MediaRenderer/RenderingControl/Control";
  String body = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"\n"
                "            s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\n"
                "  <s:Body>\n"
                "    <u:GetVolume xmlns:u=\"urn:schemas-upnp-org:service:RenderingControl:1\">\n"
                "      <InstanceID>0</InstanceID>\n"
                "      <Channel>Master</Channel>\n"
                "    </u:GetVolume>\n"
                "  </s:Body>\n"
                "</s:Envelope>";
  
  http.begin(client, url);
  http.addHeader("Content-Type", "text/xml; charset=\"utf-8\"");
  http.addHeader("SOAPACTION", "\"urn:schemas-upnp-org:service:RenderingControl:1#GetVolume\"");
  
  int httpCode = http.POST(body);
  if (httpCode == 200) {
    String response = http.getString();
    int start = response.indexOf("<CurrentVolume>") + 15;
    int end = response.indexOf("</CurrentVolume>");
    if (start > 14 && end > start) {
      return response.substring(start, end).toInt();
    }
  }
  http.end();
  return 0;
}

void setSingleSpeakerVolume(const char* ip, int volume) {
  WiFiClient client;
  HTTPClient http;
  
  String url = "http://" + String(ip) + ":1400/MediaRenderer/RenderingControl/Control";
  String body = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"\n"
                "            s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\n"
                "  <s:Body>\n"
                "    <u:SetVolume xmlns:u=\"urn:schemas-upnp-org:service:RenderingControl:1\">\n"
                "      <InstanceID>0</InstanceID>\n"
                "      <Channel>Master</Channel>\n"
                "      <DesiredVolume>" + String(volume) + "</DesiredVolume>\n"
                "    </u:SetVolume>\n"
                "  </s:Body>\n"
                "</s:Envelope>";
  
  http.begin(client, url);
  http.addHeader("Content-Type", "text/xml; charset=\"utf-8\"");
  http.addHeader("SOAPACTION", "\"urn:schemas-upnp-org:service:RenderingControl:1#SetVolume\"");
  
  http.POST(body);
  http.end();
}

void setSonosVolume(int volume) {
  for (int i = 0; i < speakerCount; i++) {
    int targetVol = volume;
    if (i == DINNING_S1) {
      if (volume == 0) {
        targetVol = 0;
      } else {
        targetVol = volume + volumeOffset;
        if (targetVol < 0) targetVol = 0;
        if (targetVol > 100) targetVol = 100;
      }
    }
    setSingleSpeakerVolume(speakers[i], targetVol);
  }
}

void updateDisplay() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB14_tr);
  u8g2.drawStr(30, 25, "Volume");
  
  u8g2.setFont(u8g2_font_ncenB24_tr);
  String volStr = String(currentVolume);
  int x = 64 - (volStr.length() * 14);
  u8g2.drawStr(x, 55, volStr.c_str());
  
  // Show offset in bottom right if not 0
  if (volumeOffset != 0) {
    u8g2.setFont(u8g2_font_6x10_tr);
    String offsetStr = String(volumeOffset > 0 ? "+" : "") + String(volumeOffset);
    u8g2.drawStr(128 - (offsetStr.length() * 6), 63, offsetStr.c_str());
  }
  
  u8g2.sendBuffer();
}

void showDinningAdjust() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB10_tr);
  u8g2.drawStr(10, 15, "Dinning Adjust");
  
  u8g2.setFont(u8g2_font_ncenB24_tr);
  String offsetStr = String(volumeOffset > 0 ? "+" : "") + String(volumeOffset);
  int x = 64 - (offsetStr.length() * 14);
  u8g2.drawStr(x, 45, offsetStr.c_str());
  
  u8g2.sendBuffer();
}

void showMenu() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tr);
  
  for (int i = 0; i < menuItemCount; i++) {
    int y = 15 + (i * 15);
    if (i == menuSelection) {
      u8g2.drawStr(0, y, ">");
    }
    u8g2.drawStr(10, y, menuItems[i]);
  }
  
  u8g2.sendBuffer();
}

void executeMenuAction() {
  if (menuSelection == 3) {
    return;
  }
  Serial.printf("Execute: %s\n", menuItems[menuSelection]);
}

void loop() {
  static bool lastBtn = HIGH;
  static unsigned long lastBtnTime = 0;
  
  // Check 3 second timeout for dinning adjust
  if (uiState == 1 && (millis() - stateChangeTime) > 3000) {
    uiState = 0;
    updateDisplay();
  }
  
  // Handle encoder rotation
  if (encoderDelta != 0) {
    int delta = encoderDelta;
    encoderDelta = 0;
    
    if (uiState == 2) { // Menu
      menuSelection += delta;
      if (menuSelection < 0) menuSelection = 0;
      if (menuSelection >= menuItemCount) menuSelection = menuItemCount - 1;
      showMenu();
    } else if (uiState == 1) { // Dinning adjust
      volumeOffset += delta * 2;
      if (volumeOffset < -50) volumeOffset = -50;
      if (volumeOffset > 50) volumeOffset = 50;
      stateChangeTime = millis();
      showDinningAdjust();
      int dinningVol = currentVolume + volumeOffset;
      if (dinningVol < 0) dinningVol = 0;
      if (dinningVol > 100) dinningVol = 100;
      setSingleSpeakerVolume(speakers[DINNING_S1], dinningVol);
      Serial.printf("Dinning offset: %d\n", volumeOffset);
    } else { // Main volume
      currentVolume += delta * 2;
      if (currentVolume < 0) currentVolume = 0;
      if (currentVolume > 100) currentVolume = 100;
      if (currentVolume == 0) volumeOffset = 0; // Reset offset when kitchen reaches 0
      volumeChanged = true;
      Serial.printf("Volume: %d\n", currentVolume);
      updateDisplay();
    }
  }
  
  // Handle button press
  bool btn = digitalRead(PIN_SW);
  if (btn == LOW && lastBtn == HIGH && (millis() - lastBtnTime) > 200) {
    lastBtnTime = millis();
    
    if (uiState == 2) { // In menu
      executeMenuAction();
      uiState = 0;
      updateDisplay();
    } else if (uiState == 1) { // In dinning adjust
      uiState = 2;
      menuSelection = 0;
      showMenu();
    } else { // Main screen
      uiState = 1;
      stateChangeTime = millis();
      showDinningAdjust();
    }
  }
  lastBtn = btn;
  
  if (volumeChanged) {
    volumeChanged = false;
    setSonosVolume(currentVolume);
  }
  
  delay(10);
}
