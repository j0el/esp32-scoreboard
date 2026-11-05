// WiFi-only smoke test. No display needed.
// Reads WIFI_SSID / WIFI_PASS from include/config.h (cp include/config.example.h
// include/config.h first, see README). Connects, prints status/IP/RSSI, then
// does one real HTTPS GET so a fully-working internet path is confirmed too.
// Build/flash with: pio run -e wifi_test -t upload -t monitor
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "config.h"

static void printStatus() {
  Serial.printf("wifi_test: connected, ip=%s rssi=%ddBm\n", WiFi.localIP().toString().c_str(),
                WiFi.RSSI());
}

static void httpCheck() {
  WiFiClientSecure client;
  client.setInsecure();  // smoke test only; no secrets sent
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(client, "https://api.open-meteo.com/v1/forecast?latitude=0&longitude=0&current=temperature_2m")) {
    Serial.println("wifi_test: http begin() failed");
    return;
  }
  int code = http.GET();
  Serial.printf("wifi_test: HTTPS GET -> status %d, %d bytes\n", code, http.getSize());
  http.end();
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.printf("wifi_test: connecting to \"%s\"\n", WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(300);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("wifi_test: FAILED to connect within 20s.");
    Serial.println("wifi_test: check WIFI_SSID/WIFI_PASS in include/config.h (2.4GHz only).");
    return;
  }
  printStatus();
  httpCheck();
}

void loop() {
  static uint32_t last = 0;
  if (millis() - last < 10000) return;
  last = millis();
  if (WiFi.status() == WL_CONNECTED) {
    printStatus();
  } else {
    Serial.println("wifi_test: not connected, retrying...");
    WiFi.reconnect();
  }
}
