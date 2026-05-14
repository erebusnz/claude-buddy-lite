#include <Arduino.h>
#include <FastLED.h>
#include <NimBLEDevice.h> // NimBLE-Arduino
#include <ArduinoJson.h>

// --- Onboard WS2812 RGB LED ---
#define LED_PIN     48
#define NUM_LEDS    1
#define BRIGHTNESS  96

// --- Buttons (wired pin -> button -> +3V3, internal pulldown) ---
#define BTN_ACCEPT_PIN  13
#define BTN_REJECT_PIN  12
#define BTN_DEBOUNCE_MS 30

// --- Nordic UART Service (Hardware Buddy BLE Protocol) ---
#define NUS_SERVICE_UUID "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_RX_UUID      "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  // desktop -> device
#define NUS_TX_UUID      "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  // device -> desktop

#define DEVICE_NAME           "Claude-Buddy"
#define HEARTBEAT_TIMEOUT_MS  30000

// ESP32S3-Super Mini
//
// Arduino IDE setup:
//   Board:        ESP32S3 Dev Module
//   Core:         Espressif ESP32 Arduino core v3.x or later
//   Libraries:    FastLED, NimBLE-Arduino, ArduinoJson
//   Upload speed: 921600, USB CDC On Boot: Enabled

enum Status { IDLE, RUNNING, WAITING };

CRGB leds[NUM_LEDS];
volatile Status gStatus = IDLE;
volatile uint32_t gLastHeartbeatMs = 0;
volatile bool gConnected = false;

String rxBuffer;
NimBLECharacteristic *txChar = nullptr;

// Active prompt id from the latest heartbeat snapshot's "prompt" object.
// Empty string = no pending permission prompt.
char gPromptId[64] = {0};

struct Button {
  uint8_t pin;
  const char *decision;  // "once" = approve, "deny" = reject
  bool lastReading;
  bool stableState;
  uint32_t lastChangeMs;
};

Button gButtons[] = {
  {BTN_ACCEPT_PIN, "once", false, false, 0},
  {BTN_REJECT_PIN, "deny", false, false, 0},
};

void sendPermission(const char *decision) {
  if (!gConnected || !txChar) return;
  if (gPromptId[0] == 0) {
    Serial.printf("[btn] %s ignored (no active prompt)\n", decision);
    return;
  }
  JsonDocument doc;
  doc["cmd"]      = "permission";
  doc["id"]       = (const char *)gPromptId;
  doc["decision"] = decision;
  String out;
  serializeJson(doc, out);
  out += '\n';
  txChar->setValue((uint8_t *)out.c_str(), out.length());
  txChar->notify();
  Serial.printf("[btn] permission id=%s decision=%s\n", gPromptId, decision);
  // Clear locally so a second press doesn't double-send before the next
  // heartbeat arrives. The desktop will drop `prompt` from the next snapshot.
  gPromptId[0] = 0;
}

void pollButtons() {
  uint32_t now = millis();
  for (auto &b : gButtons) {
    bool reading = digitalRead(b.pin) == HIGH;
    if (reading != b.lastReading) {
      b.lastReading = reading;
      b.lastChangeMs = now;
    } else if (reading != b.stableState && (now - b.lastChangeMs) >= BTN_DEBOUNCE_MS) {
      b.stableState = reading;
      if (reading) sendPermission(b.decision);
    }
  }
}

// ---------- JSON line handler ----------

void handleJsonLine(const String &line) {
  JsonDocument doc;
  if (deserializeJson(doc, line)) return;

  // Heartbeat snapshot has both running and waiting integer fields.
  if (doc["running"].is<int>() && doc["waiting"].is<int>()) {
    int running = doc["running"] | 0;
    int waiting = doc["waiting"] | 0;

    if (waiting > 0)      gStatus = WAITING;
    else if (running > 0) gStatus = RUNNING;
    else                  gStatus = IDLE;

    gLastHeartbeatMs = millis();

    // Track the active permission prompt id so the buttons can respond to it.
    const char *id = doc["prompt"]["id"].as<const char *>();
    if (id && *id) {
      strncpy(gPromptId, id, sizeof(gPromptId) - 1);
      gPromptId[sizeof(gPromptId) - 1] = 0;
    } else {
      gPromptId[0] = 0;
    }

    Serial.printf("[heartbeat] running=%d waiting=%d prompt=%s -> %s\n",
                  running, waiting,
                  gPromptId[0] ? gPromptId : "-",
                  gStatus == WAITING ? "WAITING" :
                  gStatus == RUNNING ? "RUNNING" : "IDLE");
  }

  // Other message types (time sync, owner, commands) ignored for now.
  // Extend here to ack {"cmd":"status"}, {"cmd":"name"}, etc.
}

// ---------- BLE callbacks ----------

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *, NimBLEConnInfo &) override {
    gConnected = true;
    gLastHeartbeatMs = millis();
    Serial.println("[BLE] connected");
  }
  void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int reason) override {
    gConnected = false;
    gStatus = IDLE;
    rxBuffer = "";
    gPromptId[0] = 0;
    Serial.printf("[BLE] disconnected (reason=0x%02x), re-advertising\n", reason);
    NimBLEDevice::startAdvertising();
  }
};

class RxCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &) override {
    std::string data = c->getValue();
    for (char ch : data) {
      if (ch == '\n') {
        handleJsonLine(rxBuffer);
        rxBuffer = "";
      } else if (rxBuffer.length() < 4096) {
        rxBuffer += ch;
      } else {
        rxBuffer = "";  // overflow guard
      }
    }
  }
};

void setupBLE() {
  NimBLEDevice::init(DEVICE_NAME);
  NimBLEDevice::setMTU(247);

  NimBLEServer *server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  NimBLEService *svc = server->createService(NUS_SERVICE_UUID);

  auto *rxChar = svc->createCharacteristic(
      NUS_RX_UUID,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  rxChar->setCallbacks(new RxCallbacks());

  txChar = svc->createCharacteristic(NUS_TX_UUID, NIMBLE_PROPERTY::NOTIFY);

  svc->start();

  // 128-bit service UUID + name won't fit in a single 31-byte adv packet,
  // so the UUID goes in the adv data and the name goes in the scan response.
  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();

  NimBLEAdvertisementData advData;
  advData.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  advData.addServiceUUID(NUS_SERVICE_UUID);
  adv->setAdvertisementData(advData);

  NimBLEAdvertisementData scanData;
  scanData.setName(DEVICE_NAME);
  adv->setScanResponseData(scanData);

  adv->start();
  Serial.println("[BLE] advertising as " DEVICE_NAME);
}

// ---------- LED animation ----------

void updateLed() {
  uint32_t now = millis();

  if (gConnected && (now - gLastHeartbeatMs > HEARTBEAT_TIMEOUT_MS)) {
    gStatus = IDLE;
  }

  CRGB out = CRGB::Black;

  switch (gStatus) {
    case IDLE: {
      // Steady dim white when paired; slow dim blue pulse when advertising.
      if (gConnected) {
        out = CRGB(16, 16, 16);
      } else {
        uint8_t b = sin8(now / 16) >> 3;
        out = CRGB(0, 0, b);
      }
      break;
    }

    case RUNNING: {
      // Slow pulse (~2s) blending blue <-> green.
      uint8_t pulse = sin8(now / 8);
      uint8_t mix   = sin8(now / 32);
      out = blend(CRGB::Blue, CRGB::Green, mix).nscale8(pulse);
      break;
    }

    case WAITING: {
      // Fast pulse (~0.5s) blending red <-> yellow.
      uint8_t pulse = sin8(now / 2);
      uint8_t mix   = sin8(now / 8);
      out = blend(CRGB::Red, CRGB::Yellow, mix).nscale8(pulse);
      break;
    }
  }

  leds[0] = out;
  FastLED.show();
}

// ---------- Arduino entrypoints ----------

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[boot] Claude-Buddy starting");

  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);

  for (auto &b : gButtons) pinMode(b.pin, INPUT_PULLDOWN);

  setupBLE();
}

void loop() {
  pollButtons();
  updateLed();
  delay(20);
}
