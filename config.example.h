#pragma once

// Copy this file to config.h and fill in your local connection settings.
// Keep config.h private; it is excluded by .gitignore.

// Serial
constexpr unsigned long SERIAL_BAUD = 115200;

// Wi-Fi
constexpr char WIFI_SSID[] = "YOUR_WIFI_SSID";
constexpr char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";

// MQTT
constexpr char MQTT_HOST[] = "YOUR_MQTT_BROKER_HOST";
constexpr uint16_t MQTT_PORT = 1883;
constexpr char MQTT_USER[] = "";
constexpr char MQTT_PASSWORD[] = "";
constexpr char MQTT_CLIENT_ID[] = "YOUR_UNIQUE_CLIENT_ID";
constexpr char MQTT_TOPIC_USAGE[] = "YOUR_USAGE_TOPIC";

// Codex usage
constexpr uint32_t CODEX_STALE_SECONDS = 15 * 60;
constexpr uint32_t MQTT_RECONNECT_INTERVAL_MS = 5000;
constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS = 5000;
