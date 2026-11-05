// Display-only smoke test for the 240x240 ST7789.
// No WiFi, no include/config.h needed -- just confirms the wiring and the
// TFT_SCLK/MOSI/DC/RST/CS/BL pins set in platformio.ini are correct.
// Build/flash with: pio run -e display_test -t upload -t monitor
#include <Arduino.h>
#include <TFT_eSPI.h>

static TFT_eSPI tft;

static void colorBars() {
  // If a color shows up as its complementary color, flip TFT_RGB_ORDER in
  // platformio.ini. If everything looks washed out/negative, remove
  // TFT_INVERSION_ON.
  static const uint16_t colors[] = {TFT_RED,    TFT_GREEN,   TFT_BLUE, TFT_YELLOW,
                                     TFT_CYAN,   TFT_MAGENTA, TFT_WHITE};
  const int n = sizeof(colors) / sizeof(colors[0]);
  int barHeight = tft.height() / n;
  for (int i = 0; i < n; i++) {
    tft.fillRect(0, i * barHeight, tft.width(), barHeight, colors[i]);
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("display_test: starting");

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  tft.init();
  tft.setRotation(0);
  Serial.printf("display_test: TFT_eSPI reports %dx%d\n", tft.width(), tft.height());
  Serial.println("display_test: cycling color bars / white / text every 2s");
}

void loop() {
  static uint32_t last = 0;
  static int step = 0;
  if (millis() - last < 2000) return;
  last = millis();

  switch (step) {
    case 0:
      Serial.println("step 0: color bars (check orientation + colors)");
      colorBars();
      break;
    case 1:
      Serial.println("step 1: solid white (check for dead/stuck pixels)");
      tft.fillScreen(TFT_WHITE);
      break;
    case 2:
      Serial.println("step 2: text + border (check DC/font rendering)");
      tft.fillScreen(TFT_BLACK);
      tft.drawRect(0, 0, tft.width(), tft.height(), TFT_WHITE);
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.drawString("Display OK", tft.width() / 2, tft.height() / 2 - 10, 4);
      tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
      tft.drawString(String(tft.width()) + "x" + String(tft.height()), tft.width() / 2,
                      tft.height() / 2 + 20, 2);
      break;
  }
  step = (step + 1) % 3;
}
