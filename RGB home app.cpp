#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>
#include <Ticker.h>

// ========== STABILITY IMPROVEMENTS ==========
Ticker wdtTicker;
void ICACHE_RAM_ATTR resetWatchdog() {
  ESP.wdtFeed();
}

// Memory optimization
#define WIFI_RECONNECT_INTERVAL 30000
unsigned long lastWiFiCheck = 0;
int wifiRetryCount = 0;

// Stack canary for stack overflow detection
uint32_t stackCanary;
#define STACK_CANARY 0xDEADBEEF

// Fast response - no delay for color updates
volatile bool colorUpdatePending = false;
volatile int pendingR = 0;
volatile int pendingG = 0;
volatile int pendingB = 0;

// RGB slider values
int redValue = 128;
int greenValue = 128;
int blueValue = 128;
int whiteDensityValue = 128;
int effectSpeedValue = 50;
int rainbowModeValue = 0;

// Add missing sin8 function implementation
uint8_t sin8(uint8_t theta) {
  return (uint8_t)(sin(theta * TWO_PI / 255.0) * 127.5 + 127.5);
}

uint8_t cos8(uint8_t theta) {
  return (uint8_t)(cos(theta * TWO_PI / 255.0) * 127.5 + 127.5);
}

// WiFi Configuration
const char *ssid = "MATRIX";
const char *password = "12345678";

// Advanced WiFi Settings
IPAddress apIP(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

// LED Configuration
const int LED_PIN = 4;      // D2 on NodeMCU (GPIO4)
const int NUM_LEDS = 16;    // 4x4 matrix = 16 LEDs
const int MAX_BRIGHTNESS = 255;

// Performance Settings
#define MAX_EFFECTS 111      // Updated to 111 to include 5 new rainbow mode effects (106-110)

// Create server object
ESP8266WebServer webServer(80);

// Initialize NeoPixel strip
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// ========== GLOBAL STATE VARIABLES ==========
bool isPoweredOn = true;
bool isEffectRunning = false;
int currentEffect = 0;
int currentBrightness = 128;
uint32_t currentColor = strip.Color(135, 206, 235);
unsigned long lastEffectUpdate = 0;
unsigned long lastClientRequest = 0;
unsigned long systemStartTime = 0;

// Effect variables
int effectCounter = 0;
int effectPosition = 0;
int hueCounter = 0;
int spiralCounter = 0;
int beatCounter = 0;
int meteorCounter = 0;
int waveCounter = 0;
int pulseCounter = 0;
int cometCounter = 0;

// ========== STABILITY FUNCTIONS ==========
void checkStack() {
  if (stackCanary != STACK_CANARY) {
    Serial.println("CRITICAL: Stack overflow detected!");
    emergencyRestart();
  }
}

void feedWatchdog() {
  ESP.wdtFeed();
}

void emergencyRestart() {
  for (int i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, 0);
  }
  strip.show();
  delay(100);
  ESP.restart();
}

void checkMemory() {
  static unsigned long lastMemoryCheck = 0;
  unsigned long currentTime = millis();
  
  if (currentTime - lastMemoryCheck > 60000) {
    lastMemoryCheck = currentTime;
    uint32_t freeHeap = ESP.getFreeHeap();
    
    if (freeHeap < 2048) {
      Serial.println("WARNING: Low memory!");
    }
  }
}

// INSTANT color update - no delay
void updateColorNow() {
  if (colorUpdatePending) {
    currentColor = strip.Color(pendingR, pendingG, pendingB);
    isEffectRunning = false;
    
    if (isPoweredOn) {
      for (int i = 0; i < NUM_LEDS; i++) {
        strip.setPixelColor(i, currentColor);
      }
      strip.setBrightness(currentBrightness);
      strip.show();
    }
    colorUpdatePending = false;
  }
}

// INSTANT brightness update
void updateBrightnessNow() {
  strip.setBrightness(currentBrightness);
  if (!isEffectRunning && isPoweredOn) {
    strip.show();
  }
}

// ========== WEB SERVER HANDLERS ==========
void handleRoot() {
  lastClientRequest = millis();
  webServer.send(200, "text/plain", "LED_MATRIX_CONTROLLER_V2.0");
}

void handlePing() {
  lastClientRequest = millis();
  String response = "{\"status\":\"ok\",\"uptime\":" + String(millis() - systemStartTime) + 
                    ",\"effect\":" + String(currentEffect) + 
                    ",\"brightness\":" + String(currentBrightness) + 
                    ",\"power\":" + String(isPoweredOn ? "true" : "false") + "}";
  webServer.send(200, "application/json", response);
}

void handleHandshake() {
  lastClientRequest = millis();
  String response = "{\"device\":\"ESP8266_LED_Controller\",\"version\":\"2.0\",\"leds\":" + 
                    String(NUM_LEDS) + ",\"max_brightness\":" + String(MAX_BRIGHTNESS) + 
                    ",\"effects\":" + String(MAX_EFFECTS) + "}";
  webServer.send(200, "application/json", response);
}

// INSTANT color handler - no delay
void handleColor() {
  lastClientRequest = millis();
  
  if (webServer.hasArg("hex")) {
    String hexStr = webServer.arg("hex");
    
    if (hexStr.length() == 6) {
      long hexColor = strtol(hexStr.c_str(), NULL, 16);
      
      pendingR = (hexColor >> 16) & 0xFF;
      pendingG = (hexColor >> 8) & 0xFF;
      pendingB = hexColor & 0xFF;
      colorUpdatePending = true;
      
      // INSTANT update - apply immediately
      updateColorNow();
      
      webServer.send(200, "application/json", "{\"status\":\"ok\"}");
    } else {
      webServer.send(400, "application/json", "{\"error\":\"Invalid hex\"}");
    }
  } else {
    webServer.send(400, "application/json", "{\"error\":\"Missing hex\"}");
  }
}

// NEW: RGB Slider Handler
void handleRGB() {
  lastClientRequest = millis();
  
  if (webServer.hasArg("r") && webServer.hasArg("g") && webServer.hasArg("b")) {
    redValue = webServer.arg("r").toInt();
    greenValue = webServer.arg("g").toInt();
    blueValue = webServer.arg("b").toInt();
    
    pendingR = redValue;
    pendingG = greenValue;
    pendingB = blueValue;
    colorUpdatePending = true;
    
    // INSTANT update
    updateColorNow();
    
    webServer.send(200, "application/json", "{\"status\":\"ok\"}");
  } else {
    webServer.send(400, "application/json", "{\"error\":\"Missing RGB values\"}");
  }
}

// NEW: RGBW Handler (for white density)
void handleRGBW() {
  lastClientRequest = millis();
  
  if (webServer.hasArg("r") && webServer.hasArg("g") && webServer.hasArg("b") && webServer.hasArg("w")) {
    redValue = webServer.arg("r").toInt();
    greenValue = webServer.arg("g").toInt();
    blueValue = webServer.arg("b").toInt();
    whiteDensityValue = webServer.arg("w").toInt();
    
    // Mix RGB with white density
    float w = whiteDensityValue / 255.0;
    pendingR = constrain(redValue * (1 - w) + 255 * w, 0, 255);
    pendingG = constrain(greenValue * (1 - w) + 255 * w, 0, 255);
    pendingB = constrain(blueValue * (1 - w) + 255 * w, 0, 255);
    colorUpdatePending = true;
    
    // INSTANT update
    updateColorNow();
    
    webServer.send(200, "application/json", "{\"status\":\"ok\"}");
  } else {
    webServer.send(400, "application/json", "{\"error\":\"Missing RGBW values\"}");
  }
}

// NEW: Effect Speed Handler
void handleSpeed() {
  lastClientRequest = millis();
  
  if (webServer.hasArg("val")) {
    effectSpeedValue = webServer.arg("val").toInt();
    effectSpeedValue = constrain(effectSpeedValue, 0, 100);
    
    webServer.send(200, "application/json", "{\"status\":\"ok\",\"speed\":" + String(effectSpeedValue) + "}");
  } else {
    webServer.send(400, "application/json", "{\"error\":\"Missing val\"}");
  }
}

// NEW: Rainbow Mode Handler - Activates effects 106-110
void handleRainbow() {
  lastClientRequest = millis();
  
  if (webServer.hasArg("mode")) {
    rainbowModeValue = webServer.arg("mode").toInt();
    rainbowModeValue = constrain(rainbowModeValue, 0, 4);
    
    // Map mode to effect: 0=CLASS(106), 1=ROSE(107), 2=FAST(108), 3=WAVE(109), 4=PULSE(110)
    currentEffect = 106 + rainbowModeValue;
    isEffectRunning = true;
    
    // Reset effect counters
    effectCounter = 0;
    effectPosition = 0;
    hueCounter = 0;
    waveCounter = 0;
    pulseCounter = 0;
    
    webServer.send(200, "application/json", "{\"status\":\"ok\",\"mode\":" + String(rainbowModeValue) + ",\"effect\":" + String(currentEffect) + "}");
  } else {
    webServer.send(400, "application/json", "{\"error\":\"Missing mode\"}");
  }
}

// INSTANT brightness handler - no delay, no restart
void handleBrightness() {
  lastClientRequest = millis();
  
  if (webServer.hasArg("val")) {
    int val = webServer.arg("val").toInt();
    
    if (val >= 0 && val <= MAX_BRIGHTNESS) {
      currentBrightness = val;
      updateBrightnessNow();
      
      webServer.send(200, "application/json", "{\"status\":\"ok\",\"brightness\":" + String(val) + "}");
    } else {
      webServer.send(400, "application/json", "{\"error\":\"Invalid brightness\"}");
    }
  } else {
    webServer.send(400, "application/json", "{\"error\":\"Missing val\"}");
  }
}

// FIXED Power toggle - proper ON/OFF, no blinking
void handleToggle() {
  lastClientRequest = millis();
  
  isPoweredOn = !isPoweredOn;
  
  if (!isPoweredOn) {
    // TURN OFF completely
    for (int i = 0; i < NUM_LEDS; i++) {
      strip.setPixelColor(i, 0);
    }
    isEffectRunning = false;
    strip.show();
  } else {
    // TURN ON with current color
    if (!isEffectRunning) {
      for (int i = 0; i < NUM_LEDS; i++) {
        strip.setPixelColor(i, currentColor);
      }
      strip.setBrightness(currentBrightness);
      strip.show();
    } else {
      // Effect will resume in loop
      for (int i = 0; i < NUM_LEDS; i++) {
        strip.setPixelColor(i, currentColor);
      }
      strip.setBrightness(currentBrightness);
      strip.show();
    }
  }
  
  webServer.send(200, "application/json", "{\"status\":\"ok\",\"power\":\"" + String(isPoweredOn ? "ON" : "OFF") + "\"}");
}

void handleEffect() {
  lastClientRequest = millis();
  
  if (webServer.hasArg("id")) {
    int effectId = webServer.arg("id").toInt();
    
    if (effectId >= 0 && effectId < MAX_EFFECTS) {
      currentEffect = effectId;
      isEffectRunning = true;
      
      // Reset all effect counters
      effectCounter = 0;
      effectPosition = 0;
      hueCounter = 0;
      spiralCounter = 0;
      beatCounter = 0;
      meteorCounter = 0;
      waveCounter = 0;
      pulseCounter = 0;
      cometCounter = 0;
      
      webServer.send(200, "application/json", "{\"status\":\"ok\",\"effect\":" + String(effectId) + "}");
    } else {
      webServer.send(400, "application/json", "{\"error\":\"Invalid effect\"}");
    }
  } else {
    webServer.send(400, "application/json", "{\"error\":\"Missing id\"}");
  }
}

void handleStatus() {
  lastClientRequest = millis();
  
  String response = "{\"power\":" + String(isPoweredOn ? "true" : "false") + 
                    ",\"effect\":" + String(currentEffect) + 
                    ",\"effect_running\":" + String(isEffectRunning ? "true" : "false") + 
                    ",\"brightness\":" + String(currentBrightness) + 
                    ",\"uptime\":" + String(millis() - systemStartTime) + "}";
  
  webServer.send(200, "application/json", response);
}

void handleReset() {
  lastClientRequest = millis();
  
  currentEffect = 0;
  currentBrightness = 128;
  currentColor = strip.Color(135, 206, 235);
  isEffectRunning = false;
  colorUpdatePending = false;
  redValue = 128;
  greenValue = 128;
  blueValue = 128;
  whiteDensityValue = 128;
  effectSpeedValue = 50;
  rainbowModeValue = 0;
  
  for (int i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, currentColor);
  }
  strip.setBrightness(currentBrightness);
  strip.show();
  
  webServer.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleNotFound() {
  webServer.send(404, "application/json", "{\"error\":\"Not Found\"}");
}

// ========== UTILITY FUNCTIONS ==========
uint32_t Wheel(byte WheelPos) {
  WheelPos = 255 - WheelPos;
  if(WheelPos < 85) {
    return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  }
  if(WheelPos < 170) {
    WheelPos -= 85;
    return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
  WheelPos -= 170;
  return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}

// Get effect delay based on speed value
int getEffectDelay(int baseDelay) {
  // effectSpeedValue: 0 = fastest, 100 = slowest
  return map(effectSpeedValue, 0, 100, baseDelay / 2, baseDelay * 3);
}

// ========== ALL EFFECTS (0-110) ==========

// Effect 0: Solid Color
void effect0() {
  if (isPoweredOn) {
    for (int i = 0; i < NUM_LEDS; i++) {
      strip.setPixelColor(i, currentColor);
    }
    strip.show();
  }
}

// Effect 1: Rainbow
void effect1() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(20);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      strip.setPixelColor(i, Wheel((i + effectCounter) & 255));
    }
    strip.show();
    effectCounter++;
    if (effectCounter >= 256) effectCounter = 0;
  }
}

// Effect 2: Rainbow Cycle
void effect2() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(20);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      strip.setPixelColor(i, Wheel(((i * 256 / NUM_LEDS) + effectCounter) & 255));
    }
    strip.show();
    effectCounter++;
    if (effectCounter >= 256 * 5) effectCounter = 0;
  }
}

// Effect 3: Color Wipe
void effect3() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(50);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    strip.setPixelColor(effectPosition, currentColor);
    strip.show();
    effectPosition++;
    if (effectPosition >= NUM_LEDS) {
      effectPosition = 0;
      for (int j = 0; j < NUM_LEDS; j++) {
        strip.setPixelColor(j, 0);
      }
    }
  }
}

// Effect 4: Theater Chase
void effect4() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(100);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      if ((i + effectPosition) % 3 == 0) {
        strip.setPixelColor(i, currentColor);
      } else {
        strip.setPixelColor(i, 0);
      }
    }
    strip.show();
    effectPosition++;
    if (effectPosition >= 3) effectPosition = 0;
  }
}

// Effect 5: Blink
void effect5() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(500);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    if (effectCounter % 2 == 0) {
      for (int i = 0; i < NUM_LEDS; i++) {
        strip.setPixelColor(i, currentColor);
      }
    } else {
      for (int i = 0; i < NUM_LEDS; i++) {
        strip.setPixelColor(i, 0);
      }
    }
    strip.show();
    effectCounter++;
  }
}

// Effect 6: Running Lights
void effect6() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(50);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      int brightness = sin8((i * 256 / NUM_LEDS) + effectCounter) / 2;
      strip.setPixelColor(i, strip.Color(
        (currentColor >> 16 & 0xFF) * brightness / 255,
        (currentColor >> 8 & 0xFF) * brightness / 255,
        (currentColor & 0xFF) * brightness / 255
      ));
    }
    strip.show();
    effectCounter++;
    if (effectCounter >= 256) effectCounter = 0;
  }
}

// Effect 7: Meteor
void effect7() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(30);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      strip.setPixelColor(i, strip.Color(
        (currentColor >> 16 & 0xFF) * 0.7,
        (currentColor >> 8 & 0xFF) * 0.7,
        (currentColor & 0xFF) * 0.7
      ));
    }
    for (int i = 0; i < 5; i++) {
      int pos = (meteorCounter - i + NUM_LEDS) % NUM_LEDS;
      int brightness = 255 - (i * 50);
      if (brightness > 0) {
        strip.setPixelColor(pos, strip.Color(
          (currentColor >> 16 & 0xFF) * brightness / 255,
          (currentColor >> 8 & 0xFF) * brightness / 255,
          (currentColor & 0xFF) * brightness / 255
        ));
      }
    }
    strip.show();
    meteorCounter++;
    if (meteorCounter >= NUM_LEDS) meteorCounter = 0;
  }
}

// Effect 8: Twinkle
void effect8() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(100);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    if (random(10) == 0) {
      int led = random(NUM_LEDS);
      strip.setPixelColor(led, currentColor);
    } else {
      for (int i = 0; i < NUM_LEDS; i++) {
        uint32_t color = strip.getPixelColor(i);
        int r = (color >> 16) & 0xFF;
        int g = (color >> 8) & 0xFF;
        int b = color & 0xFF;
        r = r * 0.9;
        g = g * 0.9;
        b = b * 0.9;
        strip.setPixelColor(i, strip.Color(r, g, b));
      }
    }
    strip.show();
  }
}

// Effect 9: Cycling Wipe
void effect9() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(50);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      if (i == effectPosition) {
        strip.setPixelColor(i, currentColor);
      } else {
        strip.setPixelColor(i, Wheel((i * 256 / NUM_LEDS) & 255));
      }
    }
    strip.show();
    effectPosition++;
    if (effectPosition >= NUM_LEDS) effectPosition = 0;
  }
}

// Effect 10: Fire
void effect10() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(50);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      int flicker = random(150, 255);
      strip.setPixelColor(i, strip.Color(flicker, flicker * 0.4, flicker * 0.1));
    }
    strip.show();
  }
}

// Effect 11: Confetti
void effect11() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(50);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      uint32_t color = strip.getPixelColor(i);
      int r = (color >> 16) & 0xFF;
      int g = (color >> 8) & 0xFF;
      int b = color & 0xFF;
      strip.setPixelColor(i, strip.Color(r * 0.8, g * 0.8, b * 0.8));
    }
    if (random(5) == 0) {
      strip.setPixelColor(random(NUM_LEDS), Wheel(random(256)));
    }
    strip.show();
  }
}

// Effect 12: Police
void effect12() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(100);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    if (effectCounter % 4 < 2) {
      for (int i = 0; i < NUM_LEDS; i++) {
        strip.setPixelColor(i, (i % 2 == 0) ? strip.Color(255, 0, 0) : 0);
      }
    } else {
      for (int i = 0; i < NUM_LEDS; i++) {
        strip.setPixelColor(i, (i % 2 == 1) ? strip.Color(0, 0, 255) : 0);
      }
    }
    strip.show();
    effectCounter++;
  }
}

// Effect 13: BPM
void effect13() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(60);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    int beat = sin8(beatCounter);
    for (int i = 0; i < NUM_LEDS; i++) {
      strip.setPixelColor(i, Wheel(beat + (i * 256 / NUM_LEDS)));
    }
    strip.show();
    beatCounter++;
    if (beatCounter >= 256) beatCounter = 0;
  }
}

// Effect 14: Strobe
void effect14() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(50);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    if (effectCounter % 2 == 0) {
      for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, strip.Color(255, 255, 255));
    } else {
      for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, 0);
    }
    strip.show();
    effectCounter++;
  }
}

// Effect 15: Waves
void effect15() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(30);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      int wave = sin8((i * 16) + waveCounter);
      strip.setPixelColor(i, strip.Color(wave, wave/2, 255-wave));
    }
    strip.show();
    waveCounter++;
    if (waveCounter >= 256) waveCounter = 0;
  }
}

// Effect 16: Comet
void effect16() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(40);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, 0);
    for (int i = 0; i < 4; i++) {
      int pos = (cometCounter - i + NUM_LEDS) % NUM_LEDS;
      int brightness = 255 - (i * 60);
      if (brightness > 0) {
        strip.setPixelColor(pos, Wheel((effectCounter + i * 20) % 256));
      }
    }
    strip.show();
    cometCounter++;
    effectCounter += 5;
    if (cometCounter >= NUM_LEDS) cometCounter = 0;
  }
}

// Effect 17: Checkerboard
void effect17() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(500);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      if (((i / 4) + (i % 4) + effectCounter) % 2 == 0) {
        strip.setPixelColor(i, currentColor);
      } else {
        strip.setPixelColor(i, 0);
      }
    }
    strip.show();
    effectCounter++;
  }
}

// Effect 18: Split Color
void effect18() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(50);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      if (i < NUM_LEDS/2) {
        strip.setPixelColor(i, currentColor);
      } else {
        strip.setPixelColor(i, Wheel(effectCounter % 256));
      }
    }
    strip.show();
    effectCounter++;
    if (effectCounter >= 256) effectCounter = 0;
  }
}

// Effect 19: Rainbow Fast
void effect19() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(10);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      strip.setPixelColor(i, Wheel((i + effectCounter) & 255));
    }
    strip.show();
    effectCounter += 3;
    if (effectCounter >= 256) effectCounter = 0;
  }
}

// Effect 20: Reverse Wipe
void effect20() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(50);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    strip.setPixelColor(NUM_LEDS - 1 - effectPosition, currentColor);
    strip.show();
    effectPosition++;
    if (effectPosition >= NUM_LEDS) {
      effectPosition = 0;
      for (int j = 0; j < NUM_LEDS; j++) strip.setPixelColor(j, 0);
    }
  }
}

// Effect 21: Theater Rainbow
void effect21() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(100);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      if ((i + effectPosition) % 3 == 0) {
        strip.setPixelColor(i, Wheel((i * 256 / NUM_LEDS) & 255));
      } else {
        strip.setPixelColor(i, 0);
      }
    }
    strip.show();
    effectPosition++;
    if (effectPosition >= 3) effectPosition = 0;
  }
}

// Effect 22: Twinkle Random
void effect22() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(80);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      uint32_t color = strip.getPixelColor(i);
      int r = (color >> 16) & 0xFF;
      int g = (color >> 8) & 0xFF;
      int b = color & 0xFF;
      strip.setPixelColor(i, strip.Color(r * 0.85, g * 0.85, b * 0.85));
    }
    if (random(3) == 0) {
      strip.setPixelColor(random(NUM_LEDS), Wheel(random(256)));
    }
    strip.show();
  }
}

// Effect 23: Pulse
void effect23() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(20);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    int pulse = sin8(pulseCounter);
    for (int i = 0; i < NUM_LEDS; i++) {
      strip.setPixelColor(i, strip.Color(
        (currentColor >> 16 & 0xFF) * pulse / 255,
        (currentColor >> 8 & 0xFF) * pulse / 255,
        (currentColor & 0xFF) * pulse / 255
      ));
    }
    strip.show();
    pulseCounter++;
    if (pulseCounter >= 256) pulseCounter = 0;
  }
}

// Effect 24: Sparkle
void effect24() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(50);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, strip.Color(10, 10, 10));
    for (int i = 0; i < 3; i++) strip.setPixelColor(random(NUM_LEDS), strip.Color(255, 255, 255));
    strip.show();
  }
}

// Effect 25: Bounce
void effect25() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(50);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, 0);
    int pos = abs((effectPosition % (NUM_LEDS * 2 - 2)) - (NUM_LEDS - 1));
    strip.setPixelColor(pos, currentColor);
    strip.show();
    effectPosition++;
    if (effectPosition >= NUM_LEDS * 2 - 2) effectPosition = 0;
  }
}

// Effect 26: Fade In Out
void effect26() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(20);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    int brightness = sin8(effectCounter);
    for (int i = 0; i < NUM_LEDS; i++) {
      strip.setPixelColor(i, strip.Color(
        (currentColor >> 16 & 0xFF) * brightness / 255,
        (currentColor >> 8 & 0xFF) * brightness / 255,
        (currentColor & 0xFF) * brightness / 255
      ));
    }
    strip.show();
    effectCounter++;
    if (effectCounter >= 256) effectCounter = 0;
  }
}

// Effect 27: Dual Chase
void effect27() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(80);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      if ((i + effectPosition) % 4 == 0) {
        strip.setPixelColor(i, currentColor);
      } else if ((i + effectPosition) % 4 == 2) {
        strip.setPixelColor(i, Wheel(effectCounter % 256));
      } else {
        strip.setPixelColor(i, 0);
      }
    }
    strip.show();
    effectPosition++;
    effectCounter += 10;
    if (effectPosition >= 4) effectPosition = 0;
  }
}

// Effect 28: Rainbow Wave
void effect28() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(30);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      int wave = sin8((i * 32) + effectCounter);
      strip.setPixelColor(i, Wheel(wave));
    }
    strip.show();
    effectCounter++;
    if (effectCounter >= 256) effectCounter = 0;
  }
}

// Effect 29: Meteor Rainbow
void effect29() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(30);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      uint32_t color = strip.getPixelColor(i);
      int r = (color >> 16) & 0xFF;
      int g = (color >> 8) & 0xFF;
      int b = color & 0xFF;
      strip.setPixelColor(i, strip.Color(r * 0.8, g * 0.8, b * 0.8));
    }
    for (int i = 0; i < 5; i++) {
      int pos = (effectPosition - i + NUM_LEDS) % NUM_LEDS;
      int brightness = 255 - (i * 50);
      if (brightness > 0) {
        strip.setPixelColor(pos, Wheel((effectCounter + i * 30) % 256));
      }
    }
    strip.show();
    effectPosition++;
    effectCounter += 5;
    if (effectPosition >= NUM_LEDS) effectPosition = 0;
  }
}

// Effects 30-59 (mapped to existing effects)
void effect30() { effect23(); }
void effect31() { effect29(); }
void effect32() { effect14(); }
void effect33() { effect5(); }
void effect34() { effect16(); }
void effect35() { effect7(); }
void effect36() { effect25(); }
void effect37() { effect23(); }
void effect38() { effect24(); }
void effect39() { effect15(); }
void effect40() { effect4(); }
void effect41() { effect22(); }
void effect42() { effect17(); }
void effect43() { effect28(); }
void effect44() { effect29(); }
void effect45() { effect23(); }
void effect46() { effect24(); }
void effect47() { effect33(); }
void effect48() { effect15(); }
void effect49() { effect27(); }
void effect50() { effect22(); }
void effect51() { effect23(); }
void effect52() { effect25(); }
void effect53() { effect24(); }
void effect54() { effect28(); }
void effect55() { effect17(); }
void effect56() { effect24(); }
void effect57() { effect17(); }
void effect58() { effect7(); }
void effect59() { effect23(); }

// Effect 60: Music Visualizer
void effect60() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(100);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      int height = random(0, 4);
      uint32_t color = Wheel((i * 256 / NUM_LEDS + hueCounter) % 256);
      if (i % 4 >= (3 - height)) {
        strip.setPixelColor(i, color);
      } else {
        strip.setPixelColor(i, 0);
      }
    }
    strip.show();
    hueCounter += 5;
  }
}

// Effect 61: Rainbow Fire
void effect61() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(50);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      int flicker = random(150, 255);
      int wave = sin8((i * 32) + effectCounter);
      strip.setPixelColor(i, strip.Color(flicker, wave * 0.6, (255 - wave) * 0.3));
    }
    strip.show();
    effectCounter++;
  }
}

// Effect 62: Color Dance
void effect62() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(80);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      int wave1 = sin8((i * 16) + effectCounter);
      int wave2 = sin8((i * 16) + effectCounter + 85);
      int wave3 = sin8((i * 16) + effectCounter + 170);
      strip.setPixelColor(i, strip.Color(wave1, wave2, wave3));
    }
    strip.show();
    effectCounter += 3;
  }
}

// Effect 63: Matrix Rain
void effect63() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(70);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      uint32_t color = strip.getPixelColor(i);
      strip.setPixelColor(i, strip.Color(
        ((color >> 16) & 0xFF) * 0.3,
        ((color >> 8) & 0xFF) * 0.8,
        (color & 0xFF) * 0.3
      ));
    }
    if (random(5) == 0) {
      strip.setPixelColor(random(4), strip.Color(0, 255, 0));
    }
    strip.show();
  }
}

// Effect 64: Galaxy Spin
void effect64() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(60);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    int spiral[16] = {0, 1, 2, 3, 7, 11, 15, 14, 13, 12, 8, 4, 5, 6, 10, 9};
    for (int i = 0; i < NUM_LEDS; i++) {
      strip.setPixelColor(spiral[i], Wheel((i * 256 / NUM_LEDS + hueCounter) % 256));
    }
    strip.show();
    hueCounter += 2;
  }
}

// Effect 65: Energy Pulse
void effect65() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(40);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    int pulse = sin8(pulseCounter * 3);
    for (int i = 0; i < NUM_LEDS; i++) {
      int distance = abs(i - NUM_LEDS/2);
      int brightness = max(0, pulse - distance * 20);
      strip.setPixelColor(i, strip.Color(0, brightness, brightness * 0.7));
    }
    strip.show();
    pulseCounter++;
  }
}

// Effect 66: Water Ripple
void effect66() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(60);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      int wave = sin8((i * 12) + waveCounter);
      strip.setPixelColor(i, strip.Color(0, wave * 0.3, wave));
    }
    strip.show();
    waveCounter += 2;
  }
}

// Effect 67: Heart Beat
void effect67() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(30);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    int beat = sin8(beatCounter);
    for (int i = 0; i < NUM_LEDS; i++) {
      strip.setPixelColor(i, strip.Color(beat, 0, 0));
    }
    strip.show();
    beatCounter += 8;
    if (beatCounter >= 768) beatCounter = 0;
  }
}

// Effect 68: Christmas Lights
void effect68() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(200);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      if ((i + effectCounter) % 2 == 0) {
        strip.setPixelColor(i, strip.Color(255, 0, 0));
      } else {
        strip.setPixelColor(i, strip.Color(0, 255, 0));
      }
    }
    strip.show();
    effectCounter++;
  }
}

// Effect 69: Fireworks
void effect69() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(150);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      uint32_t color = strip.getPixelColor(i);
      strip.setPixelColor(i, strip.Color(
        ((color >> 16) & 0xFF) * 0.7,
        ((color >> 8) & 0xFF) * 0.7,
        (color & 0xFF) * 0.7
      ));
    }
    if (random(10) == 0) {
      int center = random(NUM_LEDS);
      for (int i = 0; i < 3; i++) {
        strip.setPixelColor((center + i) % NUM_LEDS, Wheel(random(256)));
      }
    }
    strip.show();
  }
}

// Effect 70: Plasma Ball
void effect70() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(50);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      int x = i % 4;
      int y = i / 4;
      int plasma = sin8(x * 32 + effectCounter) + sin8(y * 32 + effectCounter) + sin8((x + y) * 16 + effectCounter);
      strip.setPixelColor(i, Wheel(plasma));
    }
    strip.show();
    effectCounter++;
  }
}

// Effect 71: Lava Lamp
void effect71() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(80);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      int x = i % 4;
      int lava = sin8(x * 64 + effectCounter * 2);
      strip.setPixelColor(i, strip.Color(255, lava * 0.4, lava * 0.1));
    }
    strip.show();
    effectCounter++;
  }
}

// Effect 72: Aurora Borealis
void effect72() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(70);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      int wave1 = sin8((i * 8) + effectCounter);
      int wave2 = sin8((i * 8) + effectCounter + 64);
      strip.setPixelColor(i, strip.Color(wave1 * 0.2, wave1, wave2));
    }
    strip.show();
    effectCounter++;
  }
}

// Effect 73: Ocean Waves
void effect73() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(60);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      int wave = sin8((i * 20) + waveCounter);
      strip.setPixelColor(i, strip.Color(0, wave * 0.3, wave));
    }
    strip.show();
    waveCounter += 2;
  }
}

// Effect 74: Desert Sunset
void effect74() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(50);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      int y = i / 4;
      int sunset = sin8(y * 64 + effectCounter);
      strip.setPixelColor(i, strip.Color(255, sunset * 0.6, 0));
    }
    strip.show();
    effectCounter++;
  }
}

// Effect 75: Northern Lights
void effect75() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(90);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      int wave = sin8((i * 12) + effectCounter + random(-10, 10));
      strip.setPixelColor(i, strip.Color(wave * 0.1, wave, wave * 0.5));
    }
    strip.show();
    effectCounter++;
  }
}

// Effect 76: Rainbow Tornado
void effect76() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(40);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    int spiral[16] = {0, 1, 2, 3, 7, 11, 15, 14, 13, 12, 8, 4, 5, 6, 10, 9};
    for (int i = 0; i < NUM_LEDS; i++) {
      int pos = spiral[(i + effectPosition) % 16];
      strip.setPixelColor(pos, Wheel((i * 256 / NUM_LEDS + effectCounter) % 256));
    }
    strip.show();
    effectPosition++;
    effectCounter += 5;
  }
}

// Effect 77: Color Tornado
void effect77() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(50);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    int spiral[16] = {0, 1, 2, 3, 7, 11, 15, 14, 13, 12, 8, 4, 5, 6, 10, 9};
    for (int i = 0; i < NUM_LEDS; i++) {
      strip.setPixelColor(spiral[(i + effectPosition) % 16], currentColor);
    }
    strip.show();
    effectPosition++;
  }
}

// Effect 78: Sparkle Storm
void effect78() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(20);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      uint32_t color = strip.getPixelColor(i);
      strip.setPixelColor(i, strip.Color(
        ((color >> 16) & 0xFF) * 0.4,
        ((color >> 8) & 0xFF) * 0.4,
        (color & 0xFF) * 0.4
      ));
    }
    for (int i = 0; i < 12; i++) {
      if (random(3) == 0) {
        strip.setPixelColor(random(NUM_LEDS), Wheel(random(256)));
      }
    }
    strip.show();
  }
}

// Effect 79: Rainbow Explosion
void effect79() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(100);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, 0);
    for (int i = 0; i < 4; i++) {
      int pos1 = (8 + i) % NUM_LEDS;
      int pos2 = (7 - i) % NUM_LEDS;
      if (255 - (i * 60) > 0) {
        strip.setPixelColor(pos1, Wheel((effectCounter + i * 40) % 256));
        strip.setPixelColor(pos2, Wheel((effectCounter + i * 40 + 128) % 256));
      }
    }
    strip.show();
    effectCounter += 10;
  }
}

// White Effects 80-84
void effect80() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(30);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    int intensity = sin8(effectCounter);
    for (int i = 0; i < NUM_LEDS; i++) {
      strip.setPixelColor(i, strip.Color(
        map(intensity, 0, 255, 100, 255),
        map(intensity, 0, 255, 80, 200),
        map(intensity, 0, 255, 60, 150)
      ));
    }
    strip.show();
    effectCounter++;
    if (effectCounter >= 256) effectCounter = 0;
  }
}

void effect81() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(40);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    int intensity = sin8(pulseCounter * 2);
    for (int i = 0; i < NUM_LEDS; i++) {
      strip.setPixelColor(i, strip.Color(
        map(intensity, 0, 255, 80, 200),
        map(intensity, 0, 255, 100, 220),
        map(intensity, 0, 255, 120, 255)
      ));
    }
    strip.show();
    pulseCounter++;
    if (pulseCounter >= 128) pulseCounter = 0;
  }
}

void effect82() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(100);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    if (effectCounter % 2 == 0) {
      for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, strip.Color(255, 255, 255));
    } else {
      for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, 0);
    }
    strip.show();
    effectCounter++;
  }
}

void effect83() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(50);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      int wave = sin8((i * 16) + waveCounter);
      strip.setPixelColor(i, strip.Color(wave, wave * 0.9, wave * 0.8));
    }
    strip.show();
    waveCounter++;
    if (waveCounter >= 256) waveCounter = 0;
  }
}

void effect84() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(random(50, 150));
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    int flicker = random(150, 255);
    for (int i = 0; i < NUM_LEDS; i++) {
      int variation = random(-30, 30);
      strip.setPixelColor(i, strip.Color(
        constrain(255 + variation, 180, 255),
        constrain(flicker * 0.6 + variation, 60, 200),
        constrain(flicker * 0.2 + variation, 20, 100)
      ));
    }
    strip.show();
  }
}

// Extra Effects 85-99
void effect85() { effect1(); }
void effect86() { effect63(); }
void effect87() { effect1(); }
void effect88() { effect63(); }
void effect89() { effect1(); }
void effect90() { effect17(); }
void effect91() { effect62(); }
void effect92() { effect23(); }
void effect93() { effect1(); }
void effect94() { effect24(); }
void effect95() { effect28(); }
void effect96() { effect15(); }
void effect97() { effect63(); }
void effect98() { effect65(); }
void effect99() { effect22(); }

// ========== PREMIUM EFFECTS (100-105) WITH DEEP COLORS ==========

// Effect 100: Daylight
void effect100() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(40);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    int brightness = 200 + sin8(effectCounter) / 5;
    for (int i = 0; i < NUM_LEDS; i++) {
      strip.setPixelColor(i, strip.Color(brightness, brightness - 20, brightness - 60));
    }
    strip.show();
    effectCounter++;
    if (effectCounter >= 256) effectCounter = 0;
  }
}

// Effect 101: Night Light
void effect101() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(50);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    int breath = sin8(effectCounter * 2);
    int blue = map(breath, 0, 255, 80, 180);
    int white = map(breath, 0, 255, 60, 140);
    for (int i = 0; i < NUM_LEDS; i++) {
      strip.setPixelColor(i, strip.Color(white, white + 20, blue));
    }
    strip.show();
    effectCounter++;
    if (effectCounter >= 256) effectCounter = 0;
  }
}

// Effect 102: Star Light
void effect102() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(30);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      strip.setPixelColor(i, strip.Color(30, 30, 40));
    }
    for (int i = 0; i < 4; i++) {
      if (random(10) < 3) {
        int led = random(NUM_LEDS);
        int brightness = random(150, 255);
        strip.setPixelColor(led, strip.Color(brightness, brightness, brightness));
      }
    }
    strip.show();
  }
}

// Effect 103: Blue Candle Light
void effect103() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(random(40, 100));
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      int flicker = random(100, 200);
      int variation = random(-20, 20);
      strip.setPixelColor(i, strip.Color(
        constrain(flicker * 0.1 + variation, 0, 30),
        constrain(flicker * 0.2 + variation, 20, 60),
        constrain(flicker * 0.8 + variation, 100, 255)
      ));
    }
    strip.show();
  }
}

// Effect 104: Red Candle Light
void effect104() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(random(40, 100));
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      int flicker = random(140, 240);
      int variation = random(-25, 25);
      strip.setPixelColor(i, strip.Color(
        constrain(flicker + variation, 150, 255),
        constrain(flicker * 0.1 + variation, 0, 30),
        constrain(flicker * 0.05 + variation, 0, 15)
      ));
    }
    strip.show();
  }
}

// Effect 105: Green Candle Light
void effect105() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(random(40, 100));
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      int flicker = random(130, 230);
      int variation = random(-20, 20);
      strip.setPixelColor(i, strip.Color(
        constrain(flicker * 0.05 + variation, 0, 15),
        constrain(flicker + variation, 150, 255),
        constrain(flicker * 0.05 + variation, 0, 15)
      ));
    }
    strip.show();
  }
}

// ========== NEW RAINBOW MODE EFFECTS (106-110) ==========

// Effect 106: CLASS Rainbow (Classic smooth rainbow)
void effect106() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(20);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      strip.setPixelColor(i, Wheel((i * 16 + effectCounter) & 255));
    }
    strip.show();
    effectCounter++;
    if (effectCounter >= 256) effectCounter = 0;
  }
}

// Effect 107: ROSE Rainbow (Pink/Red/Magenta tones)
void effect107() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(25);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      int hue = (i * 16 + effectCounter) & 255;
      // Rose tones: focus on red/pink/magenta (hue 0-50 and 200-255)
      if (hue < 128) {
        hue = map(hue, 0, 127, 0, 30);
      } else {
        hue = map(hue, 128, 255, 220, 255);
      }
      strip.setPixelColor(i, Wheel(hue));
    }
    strip.show();
    effectCounter += 2;
    if (effectCounter >= 256) effectCounter = 0;
  }
}

// Effect 108: FAST Rainbow (Quick cycling rainbow)
void effect108() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(5);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      strip.setPixelColor(i, Wheel((i * 20 + effectCounter * 2) & 255));
    }
    strip.show();
    effectCounter += 5;
    if (effectCounter >= 256) effectCounter = 0;
  }
}

// Effect 109: WAVE Rainbow (Flowing wave pattern)
void effect109() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(30);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    for (int i = 0; i < NUM_LEDS; i++) {
      int wave = sin8((i * 20) + waveCounter);
      strip.setPixelColor(i, Wheel(wave));
    }
    strip.show();
    waveCounter += 3;
    if (waveCounter >= 256) waveCounter = 0;
  }
}

// Effect 110: PULSE Rainbow (Breathing/pulsing rainbow)
void effect110() {
  if (!isPoweredOn) return;
  int delayTime = getEffectDelay(20);
  if (millis() - lastEffectUpdate > delayTime) {
    lastEffectUpdate = millis();
    int pulse = sin8(pulseCounter);
    for (int i = 0; i < NUM_LEDS; i++) {
      int colorVal = (i * 16 + effectCounter) & 255;
      uint32_t color = Wheel(colorVal);
      int r = ((color >> 16) & 0xFF) * pulse / 255;
      int g = ((color >> 8) & 0xFF) * pulse / 255;
      int b = (color & 0xFF) * pulse / 255;
      strip.setPixelColor(i, strip.Color(r, g, b));
    }
    strip.show();
    effectCounter++;
    pulseCounter += 2;
    if (effectCounter >= 256) effectCounter = 0;
    if (pulseCounter >= 256) pulseCounter = 0;
  }
}

// ========== SETUP ==========
void setup() {
  Serial.begin(115200);
  Serial.println("\n\n======================================");
  Serial.println("   LED MATRIX CONTROLLER V2.0");
  Serial.println("======================================");
  
  stackCanary = STACK_CANARY;
  systemStartTime = millis();
  
  strip.begin();
  strip.show();
  strip.setBrightness(currentBrightness);
  
  // Quick initialization test
  for (int i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, strip.Color(135, 206, 235));
  }
  strip.show();
  delay(100);
  
  // WiFi Setup
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.setOutputPower(15.5);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, gateway, subnet);
  wifi_set_phy_mode(PHY_MODE_11N);
  wifi_set_channel(6);
  
  bool apStarted = WiFi.softAP(ssid, password);
  
  if (!apStarted) {
    Serial.println("CRITICAL: Failed to start AP! Retrying...");
    delay(1000);
    ESP.restart();
  }
  
  Serial.println("======================================");
  Serial.println("Access Point Started Successfully");
  Serial.println("======================================");
  Serial.print("SSID: ");
  Serial.println(ssid);
  Serial.print("Password: ");
  Serial.println(password);
  Serial.print("IP Address: ");
  Serial.println(WiFi.softAPIP());
  Serial.println("======================================");
  
  // Web Server Routes
  webServer.on("/", handleRoot);
  webServer.on("/ping", handlePing);
  webServer.on("/handshake", handleHandshake);
  webServer.on("/color", handleColor);
  webServer.on("/rgb", handleRGB);
  webServer.on("/rgbw", handleRGBW);         // NEW: RGBW handler for white density
  webServer.on("/speed", handleSpeed);
  webServer.on("/rainbow", handleRainbow);
  webServer.on("/brightness", handleBrightness);
  webServer.on("/toggle", handleToggle);
  webServer.on("/effect", handleEffect);
  webServer.on("/status", handleStatus);
  webServer.on("/reset", handleReset);
  webServer.onNotFound(handleNotFound);
  
  webServer.begin();
  
  Serial.println("HTTP Server Started");
  Serial.println("Waiting for app connection...");
  Serial.println("======================================");
  
  ESP.wdtEnable(8000);
  wdtTicker.attach(2, resetWatchdog);
  
  Serial.println("System ready! Instant response mode enabled.");
  Serial.println("RGB Sliders, Speed Control, and Rainbow Modes active.");
  Serial.println("NEW: 5 Rainbow Mode Effects (106-110) added!");
  Serial.println("======================================");
}

// ========== MAIN LOOP ==========
void loop() {
  feedWatchdog();
  
  static unsigned long lastStackCheck = 0;
  if (millis() - lastStackCheck > 10000) {
    lastStackCheck = millis();
    checkStack();
  }
  
  checkMemory();
  
  webServer.handleClient();
  
  if (isPoweredOn && isEffectRunning) {
    switch(currentEffect) {
      case 0: effect0(); break;
      case 1: effect1(); break;
      case 2: effect2(); break;
      case 3: effect3(); break;
      case 4: effect4(); break;
      case 5: effect5(); break;
      case 6: effect6(); break;
      case 7: effect7(); break;
      case 8: effect8(); break;
      case 9: effect9(); break;
      case 10: effect10(); break;
      case 11: effect11(); break;
      case 12: effect12(); break;
      case 13: effect13(); break;
      case 14: effect14(); break;
      case 15: effect15(); break;
      case 16: effect16(); break;
      case 17: effect17(); break;
      case 18: effect18(); break;
      case 19: effect19(); break;
      case 20: effect20(); break;
      case 21: effect21(); break;
      case 22: effect22(); break;
      case 23: effect23(); break;
      case 24: effect24(); break;
      case 25: effect25(); break;
      case 26: effect26(); break;
      case 27: effect27(); break;
      case 28: effect28(); break;
      case 29: effect29(); break;
      case 30: effect30(); break;
      case 31: effect31(); break;
      case 32: effect32(); break;
      case 33: effect33(); break;
      case 34: effect34(); break;
      case 35: effect35(); break;
      case 36: effect36(); break;
      case 37: effect37(); break;
      case 38: effect38(); break;
      case 39: effect39(); break;
      case 40: effect40(); break;
      case 41: effect41(); break;
      case 42: effect42(); break;
      case 43: effect43(); break;
      case 44: effect44(); break;
      case 45: effect45(); break;
      case 46: effect46(); break;
      case 47: effect47(); break;
      case 48: effect48(); break;
      case 49: effect49(); break;
      case 50: effect50(); break;
      case 51: effect51(); break;
      case 52: effect52(); break;
      case 53: effect53(); break;
      case 54: effect54(); break;
      case 55: effect55(); break;
      case 56: effect56(); break;
      case 57: effect57(); break;
      case 58: effect58(); break;
      case 59: effect59(); break;
      case 60: effect60(); break;
      case 61: effect61(); break;
      case 62: effect62(); break;
      case 63: effect63(); break;
      case 64: effect64(); break;
      case 65: effect65(); break;
      case 66: effect66(); break;
      case 67: effect67(); break;
      case 68: effect68(); break;
      case 69: effect69(); break;
      case 70: effect70(); break;
      case 71: effect71(); break;
      case 72: effect72(); break;
      case 73: effect73(); break;
      case 74: effect74(); break;
      case 75: effect75(); break;
      case 76: effect76(); break;
      case 77: effect77(); break;
      case 78: effect78(); break;
      case 79: effect79(); break;
      case 80: effect80(); break;
      case 81: effect81(); break;
      case 82: effect82(); break;
      case 83: effect83(); break;
      case 84: effect84(); break;
      case 85: effect85(); break;
      case 86: effect86(); break;
      case 87: effect87(); break;
      case 88: effect88(); break;
      case 89: effect89(); break;
      case 90: effect90(); break;
      case 91: effect91(); break;
      case 92: effect92(); break;
      case 93: effect93(); break;
      case 94: effect94(); break;
      case 95: effect95(); break;
      case 96: effect96(); break;
      case 97: effect97(); break;
      case 98: effect98(); break;
      case 99: effect99(); break;
      case 100: effect100(); break;
      case 101: effect101(); break;
      case 102: effect102(); break;
      case 103: effect103(); break;
      case 104: effect104(); break;
      case 105: effect105(); break;
      // NEW RAINBOW MODE EFFECTS 106-110
      case 106: effect106(); break;
      case 107: effect107(); break;
      case 108: effect108(); break;
      case 109: effect109(); break;
      case 110: effect110(); break;
      default: effect0(); break;
    }
  } else if (!isPoweredOn) {
    for (int i = 0; i < NUM_LEDS; i++) {
      strip.setPixelColor(i, 0);
    }
    strip.show();
    delay(50);
  } else if (!isEffectRunning && isPoweredOn) {
    effect0();
    delay(50);
  }
  
  delay(1);
}