// Sports scoreboard: ESP32-C3 + 240x240 ST7789.
// Cycles through live games and recently finished games (kept FINAL_HOLD_SECONDS).
// Data: ESPN's unofficial public scoreboard JSON (no API key).
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <time.h>
#include <vector>
#include "config.h"

struct Game {
  char id[16];
  char league[6];
  char away[8];
  char home[8];
  int awayScore;
  int homeScore;
  char detail[28];
  bool live;
  time_t finalSeen;  // when we first saw it final; 0 while live
};

static TFT_eSPI tft;
static std::vector<Game> games;
static const int NLEAGUES = sizeof(LEAGUES) / sizeof(LEAGUES[0]);

static bool timeSynced() { return time(nullptr) > 1600000000; }

// "2026-11-02T18:00Z" -> epoch seconds (UTC); 0 on parse failure.
static time_t parseIso(const char* s) {
  int y, mo, d, h, mi;
  if (!s || sscanf(s, "%d-%d-%dT%d:%dZ", &y, &mo, &d, &h, &mi) != 5) return 0;
  y -= mo <= 2;
  int era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = y - era * 400;
  unsigned doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long days = era * 146097L + (long)doe - 719468;
  return (time_t)(days * 86400L + h * 3600L + mi * 60L);
}

static const Game* findOld(const char* id) {
  for (const Game& g : games)
    if (!strcmp(g.id, id)) return &g;
  return nullptr;
}

static bool fetchLeague(const char* path, const char* label, std::vector<Game>& out) {
  WiFiClientSecure client;
  client.setInsecure();  // public scoreboard data; no secrets sent
  HTTPClient http;
  http.useHTTP10(true);  // avoid chunked encoding so the JSON stream parses directly
  http.setTimeout(8000);
  String url = String("https://site.api.espn.com/apis/site/v2/sports/") + path + "/scoreboard";
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  if (code != 200) {
    Serial.printf("%s: HTTP %d\n", label, code);
    http.end();
    return false;
  }

  JsonDocument filter;
  JsonObject f = filter["events"][0].to<JsonObject>();
  f["id"] = true;
  f["date"] = true;
  f["status"]["type"]["state"] = true;
  f["status"]["type"]["shortDetail"] = true;
  JsonObject c = f["competitions"][0]["competitors"][0].to<JsonObject>();
  c["homeAway"] = true;
  c["score"] = true;
  c["team"]["abbreviation"] = true;

  JsonDocument doc;
  DeserializationError err =
      deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    Serial.printf("%s: JSON %s\n", label, err.c_str());
    return false;
  }

  time_t now = time(nullptr);
  for (JsonObject ev : doc["events"].as<JsonArray>()) {
    const char* state = ev["status"]["type"]["state"] | "";
    bool live = !strcmp(state, "in");
    if (!live && strcmp(state, "post")) continue;  // skip not-yet-started games

    Game g{};
    strlcpy(g.id, ev["id"] | "", sizeof(g.id));
    strlcpy(g.league, label, sizeof(g.league));
    strlcpy(g.detail, ev["status"]["type"]["shortDetail"] | "", sizeof(g.detail));
    g.live = live;
    for (JsonObject comp : ev["competitions"][0]["competitors"].as<JsonArray>()) {
      bool isHome = !strcmp(comp["homeAway"] | "", "home");
      const char* abbr = comp["team"]["abbreviation"] | "?";
      int score = atoi(comp["score"] | "0");
      if (isHome) { strlcpy(g.home, abbr, sizeof(g.home)); g.homeScore = score; }
      else        { strlcpy(g.away, abbr, sizeof(g.away)); g.awayScore = score; }
    }

    if (!live) {
      const Game* old = findOld(g.id);
      if (old && old->finalSeen) {
        g.finalSeen = old->finalSeen;
      } else {
        // Newly seen final. If it started long ago (e.g. we just rebooted), it
        // already ended hours back, so treat the hold as expired.
        time_t start = parseIso(ev["date"] | "");
        bool longAgo = timeSynced() && start && (now - start) > 5 * 3600;
        g.finalSeen = longAgo ? now - FINAL_HOLD_SECONDS - 1 : now;
      }
    }
    out.push_back(g);
  }
  return true;
}

static bool anyLive() {
  for (const Game& g : games)
    if (g.live) return true;
  return false;
}

// Returns true if anything was removed.
static bool pruneExpired() {
  if (!timeSynced()) return false;
  time_t now = time(nullptr);
  size_t before = games.size();
  for (size_t i = games.size(); i-- > 0;)
    if (!games[i].live && games[i].finalSeen && now - games[i].finalSeen > FINAL_HOLD_SECONDS)
      games.erase(games.begin() + i);
  return games.size() != before;
}

static void refresh() {
  std::vector<Game> next;
  for (int i = 0; i < NLEAGUES; i++) {
    std::vector<Game> got;
    if (fetchLeague(LEAGUES[i].path, LEAGUES[i].label, got)) {
      next.insert(next.end(), got.begin(), got.end());
    } else {
      // keep what we had for this league rather than blanking on a failed fetch
      for (const Game& g : games)
        if (!strcmp(g.league, LEAGUES[i].label)) next.push_back(g);
    }
  }
  games.swap(next);
  pruneExpired();
  Serial.printf("refresh: %u games\n", (unsigned)games.size());
}

static void showMessage(const char* line1, const char* line2 = "") {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(line1, 120, 105, 4);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString(line2, 120, 140, 2);
}

static void drawCard(const Game& g, int idx, int total) {
  uint16_t accent = g.live ? TFT_GREEN : TFT_DARKGREY;
  tft.fillScreen(TFT_BLACK);

  tft.fillRect(0, 0, 240, 36, accent);
  tft.setTextColor(TFT_BLACK, accent);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(g.league, 10, 18, 4);
  tft.setTextDatum(MR_DATUM);
  tft.drawString(g.live ? "LIVE" : "FINAL", 230, 18, 4);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(strlen(g.away) <= 3 ? 2 : 1);
  tft.drawString(g.away, 60, 80, 4);
  tft.setTextSize(strlen(g.home) <= 3 ? 2 : 1);
  tft.drawString(g.home, 180, 80, 4);

  tft.setTextSize((g.awayScore < 10 && g.homeScore < 10) ? 2 : 1);
  tft.drawNumber(g.awayScore, 60, 150, 6);
  tft.drawNumber(g.homeScore, 180, 150, 6);
  tft.setTextSize(1);
  tft.drawFastVLine(120, 55, 125, TFT_DARKGREY);

  tft.setTextColor(g.live ? TFT_GREEN : TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString(g.detail, 120, 200, 4);

  char pg[12];
  snprintf(pg, sizeof(pg), "%d/%d", idx + 1, total);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString(pg, 120, 228, 2);
}

static void drawIdle() {
  struct tm t;
  char hm[8] = "--:--";
  if (getLocalTime(&t, 0)) strftime(hm, sizeof(hm), "%H:%M", &t);
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(hm, 120, 90, 6);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("No live games", 120, 150, 4);
}

static void ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  static uint32_t lastTry = 0;
  if (millis() - lastTry < 10000) return;
  lastTry = millis();
  WiFi.reconnect();
}

void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(0);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  showMessage("Connecting...", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(300);

  showMessage("Syncing time...");
  configTzTime(TZ_INFO, "pool.ntp.org", "time.nist.gov");
  for (int i = 0; i < 40 && !timeSynced(); i++) delay(500);
}

void loop() {
  static uint32_t lastPoll = 0, lastCard = 0;
  static bool firstRun = true;
  static int cardIdx = 0;
  static String lastSig;

  ensureWifi();

  uint32_t now = millis();
  uint32_t interval = anyLive() ? POLL_LIVE_MS : POLL_IDLE_MS;
  if (firstRun || now - lastPoll >= interval) {
    if (WiFi.status() == WL_CONNECTED) refresh();
    lastPoll = millis();
    firstRun = false;
  }
  pruneExpired();

  if (games.empty()) {
    struct tm t;
    char sig[16] = "idle";
    if (getLocalTime(&t, 0)) snprintf(sig, sizeof(sig), "idle %02d:%02d", t.tm_hour, t.tm_min);
    if (lastSig != sig) {
      lastSig = sig;
      drawIdle();
    }
  } else {
    if (now - lastCard >= CARD_MS) {
      cardIdx++;
      lastCard = now;
    }
    if (cardIdx >= (int)games.size()) cardIdx = 0;
    const Game& g = games[cardIdx];
    String sig = String(g.id) + "|" + g.awayScore + "|" + g.homeScore + "|" + g.detail + "|" +
                 g.live + "|" + games.size() + "|" + cardIdx;
    if (sig != lastSig) {
      lastSig = sig;
      drawCard(g, cardIdx, games.size());
    }
  }
  delay(200);
}
