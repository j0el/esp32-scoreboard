// Copy this file to include/config.h and edit. config.h is gitignored.
#pragma once

#define WIFI_SSID "your-wifi-name"
#define WIFI_PASS "your-wifi-password"

// POSIX timezone string. Full list: https://github.com/nayarsystems/posix_tz_db
#define TZ_INFO "EST5EDT,M3.2.0,M11.1.0"

// Leagues to watch (ESPN path, short label). Add or remove freely.
static const struct { const char* path; const char* label; } LEAGUES[] = {
    {"football/nfl",     "NFL"},
    {"basketball/nba",   "NBA"},
    {"baseball/mlb",     "MLB"},
    {"hockey/nhl",       "NHL"},
};

#define FINAL_HOLD_SECONDS 3600   // keep showing a final score this long
#define CARD_MS            6000   // time each game is on screen
#define POLL_LIVE_MS       20000  // refresh interval while a game is live
#define POLL_IDLE_MS       300000 // refresh interval otherwise
