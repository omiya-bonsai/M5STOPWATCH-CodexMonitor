#include <M5Unified.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>

#include "config.h"
#include "JetBrainsMonoBold20.h"
#include "JetBrainsMonoBold14.h"

// ============================================================
// Codex usage data
// ============================================================

struct CodexUsage {
  bool valid = false;

  int fiveHourRemaining = 0;
  int weeklyRemaining = 0;

  time_t updatedAt = 0;
  time_t fiveHourResetAt = 0;
  time_t weeklyResetAt = 0;

  String plan;
};

CodexUsage codex;

// ============================================================
// Codex realtime status
// ============================================================

enum class CodexStatus {
  IDLE,
  WORKING,
  DONE,
  UNKNOWN
};

CodexStatus codexStatus = CodexStatus::IDLE;

bool codexStatusValid = false;

// ============================================================
// Network
// ============================================================

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

unsigned long lastWifiAttempt = 0;
unsigned long lastMqttAttempt = 0;

bool lastStaleState = true;

// ============================================================
// MQTT topics
// ============================================================

constexpr const char* MQTT_TOPIC_STATUS =
  "home/codex/status";

// ============================================================
// Display geometry
// ============================================================

constexpr int SCREEN_CX = 233;
constexpr int SCREEN_CY = 233;

// ------------------------------------------------------------
// Outer ring: 5 hour
// ------------------------------------------------------------

constexpr int FIVE_RING_RADIUS = 205;
constexpr int FIVE_RING_WIDTH = 30;

// ------------------------------------------------------------
// Inner ring: weekly
// ------------------------------------------------------------

constexpr int WEEK_RING_RADIUS = 163;
constexpr int WEEK_RING_WIDTH = 28;

// ------------------------------------------------------------
// Center status
// ------------------------------------------------------------

constexpr int STATUS_RADIUS = 68;
constexpr int STATUS_BORDER_WIDTH = 4;

// ============================================================
// Color palette
// ============================================================

constexpr uint16_t COLOR_BG = 0x0000;

// Very dark inactive track
constexpr uint16_t COLOR_TRACK = 0x18C3;

// Center border
constexpr uint16_t COLOR_BORDER = 0x2945;

// ------------------------------------------------------------
// Usage rings
// ------------------------------------------------------------

// Pikachu Yellow
constexpr uint16_t COLOR_FIVE = 0xFEA5;

// CIO Purple
constexpr uint16_t COLOR_WEEK = 0x8AFF;

// ------------------------------------------------------------
// Codex status
// ------------------------------------------------------------

// Omarchy Cyan - idle
constexpr uint16_t COLOR_STATUS_IDLE = 0x2C73;

// Working - red
constexpr uint16_t COLOR_STATUS_WORKING = 0xF800;

// Done - blue
constexpr uint16_t COLOR_STATUS_DONE = 0x249F;

// Unknown
constexpr uint16_t COLOR_STATUS_UNKNOWN = 0x4208;

// ------------------------------------------------------------
// Text
// ------------------------------------------------------------

constexpr uint16_t COLOR_TEXT = 0xFFFF;
constexpr uint16_t COLOR_DIM = 0x8410;

// Warning / stale
constexpr uint16_t COLOR_WARNING = 0xFD20;

// ============================================================
// Vibration
// ============================================================

constexpr uint8_t DONE_VIBRATION_STRENGTH = 180;
constexpr uint32_t DONE_VIBRATION_MS = 250;

// ============================================================
// Font helpers
// ============================================================

void setFontLarge() {
  M5.Display.setFont(
    &JetBrainsMonoNLNerdFontMono_Bold20pt7b);
}

void setFontSmall() {
  M5.Display.setFont(
    &JetBrainsMonoNLNerdFontMono_Bold14pt7b);
}

// ============================================================
// Utility
// ============================================================

int clampPercent(int value) {
  if (value < 0) {
    return 0;
  }

  if (value > 100) {
    return 100;
  }

  return value;
}

// ============================================================
// Time validity
// ============================================================

bool timeIsValid() {
  time_t now = time(nullptr);

  return now > 1700000000;
}

// ============================================================
// Stale check
// ============================================================

bool codexIsStale() {
  if (!codex.valid) {
    return true;
  }

  if (!timeIsValid()) {
    return false;
  }

  time_t now = time(nullptr);

  if (now < codex.updatedAt) {
    return false;
  }

  return (now - codex.updatedAt)
         > CODEX_STALE_SECONDS;
}

// ============================================================
// Time formatting
// ============================================================

String formatTime(time_t timestamp) {
  if (timestamp <= 0) {
    return "--:--";
  }

  struct tm timeInfo;

  localtime_r(
    &timestamp,
    &timeInfo);

  char buffer[16];

  strftime(
    buffer,
    sizeof(buffer),
    "%H:%M",
    &timeInfo);

  return String(buffer);
}

// ============================================================
// Date formatting
// ============================================================

String formatDate(time_t timestamp) {
  if (timestamp <= 0) {
    return "--/--";
  }

  struct tm timeInfo;

  localtime_r(
    &timestamp,
    &timeInfo);

  char buffer[16];

  strftime(
    buffer,
    sizeof(buffer),
    "%m/%d",
    &timeInfo);

  return String(buffer);
}

// ============================================================
// Thick arc
// ============================================================

void drawThickArc(
  int radius,
  int width,
  float startAngle,
  float endAngle,
  uint16_t color) {

  int halfWidth = width / 2;

  for (
    int r = radius - halfWidth;
    r <= radius + halfWidth;
    r++) {

    M5.Display.drawArc(
      SCREEN_CX,
      SCREEN_CY,
      r,
      r,
      startAngle,
      endAngle,
      color);
  }
}

// ============================================================
// Full ring
// ============================================================

void drawFullRing(
  int radius,
  int width,
  uint16_t color) {

  drawThickArc(
    radius,
    width,
    0,
    360,
    color);
}

// ============================================================
// Percentage ring
//
// Start:
//   270 degrees = 12 o'clock
//
// Direction:
//   clockwise
// ============================================================

void drawPercentRing(
  int radius,
  int width,
  int percent,
  uint16_t color) {

  percent = clampPercent(percent);

  if (percent <= 0) {
    return;
  }

  if (percent >= 100) {
    drawFullRing(
      radius,
      width,
      color);

    return;
  }

  constexpr float startAngle = 270.0f;

  float sweep =
    360.0f
    * ((float)percent / 100.0f);

  float endAngle =
    startAngle + sweep;

  if (endAngle <= 360.0f) {
    drawThickArc(
      radius,
      width,
      startAngle,
      endAngle,
      color);

  } else {
    drawThickArc(
      radius,
      width,
      startAngle,
      360.0f,
      color);

    drawThickArc(
      radius,
      width,
      0.0f,
      endAngle - 360.0f,
      color);
  }
}

// ============================================================
// Status helpers
// ============================================================

const char* codexStatusName(
  CodexStatus status) {

  switch (status) {
    case CodexStatus::IDLE:
      return "IDLE";

    case CodexStatus::WORKING:
      return "WORKING";

    case CodexStatus::DONE:
      return "DONE";

    default:
      return "UNKNOWN";
  }
}

// ============================================================
// Vibration
// ============================================================

void vibrateDone() {
  Serial.println(
    "VIBRATION: DONE");

  M5.Power.setVibration(
    DONE_VIBRATION_STRENGTH);

  delay(
    DONE_VIBRATION_MS);

  M5.Power.setVibration(0);
}

// ============================================================
// Center status color
// ============================================================

uint16_t getCenterStatusColor() {
  if (codexIsStale()) {
    return COLOR_WARNING;
  }

  if (!codexStatusValid) {
    return COLOR_STATUS_IDLE;
  }

  switch (codexStatus) {
    case CodexStatus::IDLE:
      return COLOR_STATUS_IDLE;

    case CodexStatus::WORKING:
      return COLOR_STATUS_WORKING;

    case CodexStatus::DONE:
      return COLOR_STATUS_DONE;

    default:
      return COLOR_STATUS_UNKNOWN;
  }
}

// ============================================================
// Draw center status
// ============================================================

void drawCenterStatus() {
  bool stale = codexIsStale();

  uint16_t centerColor =
    getCenterStatusColor();

  // ----------------------------------------------------------
  // Border
  // ----------------------------------------------------------

  M5.Display.fillCircle(
    SCREEN_CX,
    SCREEN_CY,
    STATUS_RADIUS,
    COLOR_BORDER);

  // ----------------------------------------------------------
  // Status face
  // ----------------------------------------------------------

  M5.Display.fillCircle(
    SCREEN_CX,
    SCREEN_CY,
    STATUS_RADIUS - STATUS_BORDER_WIDTH,
    centerColor);

  M5.Display.setTextDatum(
    middle_center);

  M5.Display.setTextColor(
    COLOR_BG,
    centerColor);

  // ----------------------------------------------------------
  // Usage data stale
  // ----------------------------------------------------------

  if (stale) {
    setFontLarge();

    M5.Display.drawString(
      "STALE",
      SCREEN_CX,
      SCREEN_CY - 18);

    setFontSmall();

    M5.Display.drawString(
      "CODEX",
      SCREEN_CX,
      SCREEN_CY + 24);

    return;
  }

  // ----------------------------------------------------------
  // No realtime status yet
  // ----------------------------------------------------------

  if (!codexStatusValid) {
    setFontLarge();

    M5.Display.drawString(
      "CODEX",
      SCREEN_CX,
      SCREEN_CY);

    return;
  }

  // ----------------------------------------------------------
  // Realtime status
  // ----------------------------------------------------------

  setFontLarge();

  switch (codexStatus) {
    case CodexStatus::IDLE:

      M5.Display.drawString(
        "CODEX",
        SCREEN_CX,
        SCREEN_CY);

      break;

    case CodexStatus::WORKING:

      M5.Display.drawString(
        "WORK",
        SCREEN_CX,
        SCREEN_CY);

      break;

    case CodexStatus::DONE:

      M5.Display.drawString(
        "DONE",
        SCREEN_CX,
        SCREEN_CY);

      break;

    default:

      setFontSmall();

      M5.Display.drawString(
        "UNKNOWN",
        SCREEN_CX,
        SCREEN_CY);

      break;
  }
}

// ============================================================
// 5H information
// ============================================================

void drawFiveHourInformation() {
  M5.Display.setTextDatum(
    middle_center);

  // ----------------------------------------------------------
  // Label
  // ----------------------------------------------------------

  setFontSmall();

  M5.Display.setTextColor(
    COLOR_DIM,
    COLOR_BG);

  M5.Display.drawString(
    "5H",
    SCREEN_CX,
    86);

  // ----------------------------------------------------------
  // Percentage
  // ----------------------------------------------------------

  setFontLarge();

  M5.Display.setTextColor(
    COLOR_FIVE,
    COLOR_BG);

  M5.Display.drawString(
    String(codex.fiveHourRemaining) + "%",
    SCREEN_CX,
    116);

  // ----------------------------------------------------------
  // Reset time
  // ----------------------------------------------------------

  setFontSmall();

  M5.Display.setTextColor(
    COLOR_DIM,
    COLOR_BG);

  M5.Display.drawString(
    formatTime(
      codex.fiveHourResetAt),
    SCREEN_CX,
    146);
}

// ============================================================
// WEEK information
// ============================================================

void drawWeeklyInformation() {
  M5.Display.setTextDatum(
    middle_center);

  // ----------------------------------------------------------
  // Label
  // ----------------------------------------------------------

  setFontSmall();

  M5.Display.setTextColor(
    COLOR_DIM,
    COLOR_BG);

  M5.Display.drawString(
    "WEEK",
    SCREEN_CX,
    316);

  // ----------------------------------------------------------
  // Percentage
  // ----------------------------------------------------------

  setFontLarge();

  M5.Display.setTextColor(
    COLOR_WEEK,
    COLOR_BG);

  M5.Display.drawString(
    String(codex.weeklyRemaining) + "%",
    SCREEN_CX,
    346);

  // ----------------------------------------------------------
  // Reset date
  // ----------------------------------------------------------

  setFontSmall();

  M5.Display.setTextColor(
    COLOR_DIM,
    COLOR_BG);

  M5.Display.drawString(
    formatDate(
      codex.weeklyResetAt),
    SCREEN_CX,
    376);
}

// ============================================================
// Full usage screen
// ============================================================

void drawUsageScreen() {
  bool stale =
    codexIsStale();

  M5.Display.fillScreen(
    COLOR_BG);

  // ----------------------------------------------------------
  // Ring tracks
  // ----------------------------------------------------------

  drawFullRing(
    FIVE_RING_RADIUS,
    FIVE_RING_WIDTH,
    COLOR_TRACK);

  drawFullRing(
    WEEK_RING_RADIUS,
    WEEK_RING_WIDTH,
    COLOR_TRACK);

  // ----------------------------------------------------------
  // Active rings
  // ----------------------------------------------------------

  if (codex.valid) {
    drawPercentRing(
      FIVE_RING_RADIUS,
      FIVE_RING_WIDTH,
      codex.fiveHourRemaining,
      COLOR_FIVE);

    drawPercentRing(
      WEEK_RING_RADIUS,
      WEEK_RING_WIDTH,
      codex.weeklyRemaining,
      COLOR_WEEK);
  }

  // ----------------------------------------------------------
  // Information
  // ----------------------------------------------------------

  if (codex.valid) {
    drawFiveHourInformation();
    drawWeeklyInformation();
  }

  // ----------------------------------------------------------
  // Center
  // ----------------------------------------------------------

  drawCenterStatus();

  lastStaleState =
    stale;
}

// ============================================================
// Redraw center only
// ============================================================

void redrawCenterStatus() {
  drawCenterStatus();
}

// ============================================================
// Waiting screen
// ============================================================

void drawWaitingScreen() {
  M5.Display.fillScreen(
    COLOR_BG);

  // ----------------------------------------------------------
  // Ring tracks
  // ----------------------------------------------------------

  drawFullRing(
    FIVE_RING_RADIUS,
    FIVE_RING_WIDTH,
    COLOR_TRACK);

  drawFullRing(
    WEEK_RING_RADIUS,
    WEEK_RING_WIDTH,
    COLOR_TRACK);

  // ----------------------------------------------------------
  // Center
  // ----------------------------------------------------------

  M5.Display.fillCircle(
    SCREEN_CX,
    SCREEN_CY,
    STATUS_RADIUS,
    COLOR_BORDER);

  M5.Display.fillCircle(
    SCREEN_CX,
    SCREEN_CY,
    STATUS_RADIUS - STATUS_BORDER_WIDTH,
    COLOR_STATUS_IDLE);

  M5.Display.setTextDatum(
    middle_center);

  M5.Display.setTextColor(
    COLOR_BG,
    COLOR_STATUS_IDLE);

  setFontLarge();

  M5.Display.drawString(
    "CODEX",
    SCREEN_CX,
    SCREEN_CY - 18);

  setFontSmall();

  M5.Display.drawString(
    "WAITING",
    SCREEN_CX,
    SCREEN_CY + 24);
}

// ============================================================
// Parse usage MQTT
// ============================================================

void handleUsageMessage(
  byte* payload,
  unsigned int length) {

  JsonDocument doc;

  DeserializationError error =
    deserializeJson(
      doc,
      payload,
      length);

  if (error) {
    Serial.printf(
      "MQTT usage: JSON error: %s\n",
      error.c_str());

    return;
  }

  codex.updatedAt =
    doc["updated_at"]
    | 0;

  codex.fiveHourRemaining =
    clampPercent(
      doc["five_hour"]["remaining_percent"]
      | 0);

  codex.fiveHourResetAt =
    doc["five_hour"]["reset_at"]
    | 0;

  codex.weeklyRemaining =
    clampPercent(
      doc["weekly"]["remaining_percent"]
      | 0);

  codex.weeklyResetAt =
    doc["weekly"]["reset_at"]
    | 0;

  codex.plan =
    doc["plan"]
    | "";

  codex.valid = true;

  Serial.printf(
    "CODEX usage: 5H=%d%% WEEK=%d%% plan=%s\n",
    codex.fiveHourRemaining,
    codex.weeklyRemaining,
    codex.plan.c_str());

  drawUsageScreen();
}

// ============================================================
// Parse realtime status MQTT
// ============================================================

void handleStatusMessage(
  byte* payload,
  unsigned int length) {

  JsonDocument doc;

  DeserializationError error =
    deserializeJson(
      doc,
      payload,
      length);

  if (error) {
    Serial.printf(
      "MQTT status: JSON error: %s\n",
      error.c_str());

    return;
  }

  const char* status =
    doc["status"]
    | "";

  const char* event =
    doc["event"]
    | "";

  CodexStatus newStatus =
    CodexStatus::UNKNOWN;

  if (
    strcmp(
      status,
      "idle")
    == 0) {

    newStatus =
      CodexStatus::IDLE;

  } else if (
    strcmp(
      status,
      "working")
    == 0) {

    newStatus =
      CodexStatus::WORKING;

  } else if (
    strcmp(
      status,
      "done")
    == 0) {

    newStatus =
      CodexStatus::DONE;
  }

  // ----------------------------------------------------------
  // Remember previous state
  // ----------------------------------------------------------

  CodexStatus previousStatus =
    codexStatus;

  bool wasStatusValid =
    codexStatusValid;

  bool changed =
    !codexStatusValid
    || newStatus != codexStatus;

  // ----------------------------------------------------------
  // Apply new state
  // ----------------------------------------------------------

  codexStatus =
    newStatus;

  codexStatusValid =
    true;

  Serial.printf(
    "CODEX status: %s event=%s\n",
    codexStatusName(codexStatus),
    event);

  // ----------------------------------------------------------
  // Redraw only when state changed
  // ----------------------------------------------------------

  if (changed) {
    redrawCenterStatus();
  }

  // ----------------------------------------------------------
  // DONE vibration
  //
  // Conditions:
  //
  // 1. We already had a valid realtime status.
  // 2. Previous state was not DONE.
  // 3. New state is DONE.
  // 4. Event really is task_complete.
  //
  // This prevents a retained DONE message received immediately
  // after MQTT connection from causing an unwanted vibration.
  // ----------------------------------------------------------

  if (
    wasStatusValid
    && previousStatus != CodexStatus::DONE
    && newStatus == CodexStatus::DONE
    && strcmp(
         event,
         "task_complete")
         == 0) {

    vibrateDone();
  }
}

// ============================================================
// MQTT callback
// ============================================================

void mqttCallback(
  char* topic,
  byte* payload,
  unsigned int length) {

  Serial.printf(
    "MQTT: received topic=%s length=%u\n",
    topic,
    length);

  // ----------------------------------------------------------
  // Usage
  // ----------------------------------------------------------

  if (
    strcmp(
      topic,
      MQTT_TOPIC_USAGE)
    == 0) {

    handleUsageMessage(
      payload,
      length);

    return;
  }

  // ----------------------------------------------------------
  // Realtime status
  // ----------------------------------------------------------

  if (
    strcmp(
      topic,
      MQTT_TOPIC_STATUS)
    == 0) {

    handleStatusMessage(
      payload,
      length);

    return;
  }
}

// ============================================================
// MQTT connect
// ============================================================

bool connectMqtt() {
  if (
    WiFi.status()
    != WL_CONNECTED) {

    return false;
  }

  Serial.printf(
    "MQTT: connecting to %s:%d\n",
    MQTT_HOST,
    MQTT_PORT);

  bool connected = false;

  if (
    strlen(MQTT_USER) > 0) {

    connected =
      mqttClient.connect(
        MQTT_CLIENT_ID,
        MQTT_USER,
        MQTT_PASSWORD);

  } else {

    connected =
      mqttClient.connect(
        MQTT_CLIENT_ID);
  }

  if (!connected) {
    Serial.printf(
      "MQTT: failed state=%d\n",
      mqttClient.state());

    return false;
  }

  Serial.println(
    "MQTT: connected");

  // ----------------------------------------------------------
  // Usage
  // ----------------------------------------------------------

  mqttClient.subscribe(
    MQTT_TOPIC_USAGE);

  Serial.printf(
    "MQTT: subscribed %s\n",
    MQTT_TOPIC_USAGE);

  // ----------------------------------------------------------
  // Realtime status
  // ----------------------------------------------------------

  mqttClient.subscribe(
    MQTT_TOPIC_STATUS);

  Serial.printf(
    "MQTT: subscribed %s\n",
    MQTT_TOPIC_STATUS);

  return true;
}

// ============================================================
// Wi-Fi connect
// ============================================================

void connectWifi() {
  Serial.printf(
    "WiFi: connecting to %s\n",
    WIFI_SSID);

  WiFi.mode(
    WIFI_STA);

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD);
}

// ============================================================
// Maintain Wi-Fi
// ============================================================

void maintainWifi() {
  if (
    WiFi.status()
    == WL_CONNECTED) {

    return;
  }

  unsigned long now =
    millis();

  if (
    now - lastWifiAttempt
    < WIFI_RECONNECT_INTERVAL_MS) {

    return;
  }

  lastWifiAttempt =
    now;

  Serial.println(
    "WiFi: reconnecting");

  WiFi.disconnect();

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD);
}

// ============================================================
// Maintain MQTT
// ============================================================

void maintainMqtt() {
  if (
    WiFi.status()
    != WL_CONNECTED) {

    return;
  }

  if (
    mqttClient.connected()) {

    mqttClient.loop();

    return;
  }

  unsigned long now =
    millis();

  if (
    now - lastMqttAttempt
    < MQTT_RECONNECT_INTERVAL_MS) {

    return;
  }

  lastMqttAttempt =
    now;

  connectMqtt();
}

// ============================================================
// Stale display maintenance
//
// Redraw only when fresh/stale state changes.
// ============================================================

void maintainStaleDisplay() {
  if (!codex.valid) {
    return;
  }

  bool stale =
    codexIsStale();

  if (
    stale
    != lastStaleState) {

    Serial.printf(
      "CODEX: stale state changed -> %s\n",
      stale
        ? "STALE"
        : "FRESH");

    drawUsageScreen();
  }
}

// ============================================================
// Setup
// ============================================================

void setup() {
  auto cfg =
    M5.config();

  M5.begin(
    cfg);

  Serial.begin(
    SERIAL_BAUD);

  delay(300);

  Serial.println();

  Serial.println(
    "M5StopWatch Codex Monitor");

  Serial.println(
    "UI v0.10 DONE vibration");

  // ----------------------------------------------------------
  // Display
  // ----------------------------------------------------------

  M5.Display.setRotation(
    0);

  M5.Display.fillScreen(
    COLOR_BG);

  drawWaitingScreen();

  // ----------------------------------------------------------
  // Wi-Fi
  // ----------------------------------------------------------

  WiFi.mode(
    WIFI_STA);

  connectWifi();

  unsigned long wifiStart =
    millis();

  while (
    WiFi.status()
      != WL_CONNECTED
    && millis() - wifiStart
         < 15000) {

    delay(100);

    M5.update();
  }

  if (
    WiFi.status()
    == WL_CONNECTED) {

    Serial.println(
      "WiFi: connected");

    Serial.printf(
      "WiFi: IP=%s RSSI=%d dBm\n",
      WiFi.localIP()
        .toString()
        .c_str(),
      WiFi.RSSI());

    // --------------------------------------------------------
    // NTP
    // --------------------------------------------------------

    configTzTime(
      "JST-9",
      "ntp.nict.jp",
      "pool.ntp.org");

    Serial.println(
      "NTP: configured");

  } else {

    Serial.println(
      "WiFi: initial connection failed");
  }

  // ----------------------------------------------------------
  // MQTT
  // ----------------------------------------------------------

  mqttClient.setServer(
    MQTT_HOST,
    MQTT_PORT);

  mqttClient.setCallback(
    mqttCallback);

  mqttClient.setBufferSize(
    2048);

  if (
    WiFi.status()
    == WL_CONNECTED) {

    connectMqtt();
  }
}

// ============================================================
// Loop
// ============================================================

void loop() {
  M5.update();

  maintainWifi();

  maintainMqtt();

  maintainStaleDisplay();

  delay(10);
}