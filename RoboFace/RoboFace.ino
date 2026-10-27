// RoboFace: cartoon eyes + mouth for the Waveshare ESP32-C6-LCD-1.47
//  - eyes blink at random (sometimes a double blink) and glance around
//  - every so often it smiles (eyes squint happily)
//  - onboard RGB LED (seen through the case side slots) cycles through colours
//
// Board: "ESP32C6 Dev Module" (arduino-esp32 3.x)
// Libraries: Adafruit GFX, Adafruit ST7735 and ST7789

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

// ---- Waveshare ESP32-C6-LCD-1.47 pins (Waveshare wiki / schematic) ----
#define PIN_MOSI 6
#define PIN_SCLK 7
#define PIN_CS   14
#define PIN_DC   15
#define PIN_RST  21
#define PIN_BL   22
#define PIN_RGB  8

// 0 = USB end down, 2 = upside down (USB end up, the way it sits in the dock)
#define ROTATION 0

#define BACKLIGHT_PCT  50      // screen backlight, 0..100 % *perceived* (gamma-corrected)
#define LED_BRIGHTNESS 60      // side RGB LED, 0..255 (60 = ~24 %)
#define LED_CYCLE_MS   6000    // one full trip round the colour wheel

Adafruit_ST7789 tft(&SPI, PIN_CS, PIN_DC, PIN_RST);
GFXcanvas16 *fb;

const uint16_t BG  = 0x0000;
uint16_t EYE;                  // set in setup (soft cyan)
uint16_t PUPIL, HILITE;        // set in setup
#define PUPIL_R 14

// ---------------- animation state ----------------
uint32_t blinkStart = 0, nextBlink = 0;
bool     blinking = false, pendingDouble = false;

uint32_t smileStart = 0, smileEnd = 0, nextSmile = 0;

float gazeX = 0, gazeY = 0, gazeTX = 0, gazeTY = 0;
uint32_t nextGaze = 0;

static float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float easeInOut(float t) { return t * t * (3 - 2 * t); }

// eye openness 0..1 (1 = fully open)
float eyeOpen(uint32_t now) {
  if (!blinking) return 1;
  uint32_t t = now - blinkStart;
  const uint32_t CLOSE = 90, HOLD = 50, OPEN = 120;
  if (t < CLOSE) return 1 - easeInOut(t / (float)CLOSE);
  if (t < CLOSE + HOLD) return 0;
  if (t < CLOSE + HOLD + OPEN) return easeInOut((t - CLOSE - HOLD) / (float)OPEN);
  blinking = false;
  if (pendingDouble) {               // second blink of a double
    pendingDouble = false;
    blinking = true;
    blinkStart = now + 60;
  }
  return 1;
}

// smile amount 0..1 with 300 ms ease in/out
float smileAmt(uint32_t now) {
  if (now < smileStart || now > smileEnd + 300) return 0;
  if (now < smileStart + 300) return easeInOut((now - smileStart) / 300.0f);
  if (now <= smileEnd) return 1;
  return 1 - easeInOut((now - smileEnd) / 300.0f);
}

void drawMouth(int cx, int my, float s) {
  // crescent = big circle minus the same circle shifted up; small radius = smile, huge = flat line
  float R     = lerpf(600, 46, s);
  float th    = lerpf(9, 14, s);
  float halfw = lerpf(26, 42, s);
  int c1y = (int)(my + 4 - R);
  fb->fillCircle(cx, c1y, (int)R, EYE);
  fb->fillCircle(cx, (int)(c1y - th), (int)R, BG);
  // trim to mouth width
  fb->fillRect(0, my - 70, cx - (int)halfw, 110, BG);
  fb->fillRect(cx + (int)halfw + 1, my - 70, fb->width(), 110, BG);
  // rounded ends on the flat mouth
  float sag = R - sqrtf(R * R - halfw * halfw);
  int capR = (int)((1 - s) * th / 2);
  if (capR > 1) {
    int ey = (int)(my + 4 - sag - th / 2);
    fb->fillCircle(cx - (int)halfw, ey, capR, EYE);
    fb->fillCircle(cx + (int)halfw, ey, capR, EYE);
  }
}

void drawEye(int cx, int cy, float open, float s, float px, float py) {
  const int W = 54, H = 80;
  int h = max(6, (int)(H * open));
  int r = min(h / 2, 20);
  fb->fillRoundRect(cx - W / 2, cy - h / 2, W, h, r, EYE);
  // pupil + highlight, kept inside the (possibly half-closed) eye
  int pr = min(PUPIL_R, h / 2 - 3);
  if (pr >= 3) {
    int maxX = W / 2 - pr - 4;
    int ppx = cx + (int)constrain(px, -maxX, maxX);
    int ppy = constrain(cy + (int)py, cy - h / 2 + pr + 2, cy + h / 2 - pr - 2);
    fb->fillCircle(ppx, ppy, pr, PUPIL);
    if (pr > 6) fb->fillCircle(ppx - pr / 3, ppy - pr / 3, max(2, pr / 4), HILITE);
  }
  if (s > 0.01f && h > 10) {
    // happy squint: bite the bottom of the eye with a background circle
    int R = 44;
    int bite = (int)(H * 0.5f * s);
    fb->fillCircle(cx, cy + h / 2 + R - bite, R, BG);
  }
}

void drawFace(uint32_t now) {
  int W = fb->width(), H = fb->height();
  float s = smileAmt(now);
  float open = eyeOpen(now);
  fb->fillScreen(BG);
  drawMouth(W / 2 + (int)(gazeX * 0.4f), (int)(H * 0.72f), s);   // mouth first: its mask circle is huge
  int cy = (int)(H * 0.38f + gazeY);
  int dx = 40;
  // pupils travel further than the eyes for a livelier look
  float px = gazeX * 0.9f, py = gazeY * 1.6f;
  drawEye(W / 2 - dx + (int)gazeX, cy, open, s, px, py);
  drawEye(W / 2 + dx + (int)gazeX, cy, open, s, px, py);
  tft.drawRGBBitmap(0, 0, fb->getBuffer(), W, H);
}

// ---------------- RGB LED ----------------
void hsvToRgb(float h, uint8_t v, uint8_t &r, uint8_t &g, uint8_t &b) {
  float hh = fmodf(h, 1.0f) * 6;
  int i = (int)hh;
  float f = hh - i;
  uint8_t p = 0, q = (uint8_t)(v * (1 - f)), t = (uint8_t)(v * f);
  switch (i) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
  }
}

void updateLed(uint32_t now) {
  static uint32_t last = 0;
  if (now - last < 20) return;
  last = now;
  uint8_t r, g, b;
  hsvToRgb((now % LED_CYCLE_MS) / (float)LED_CYCLE_MS, LED_BRIGHTNESS, r, g, b);
  rgbLedWrite(PIN_RGB, r, g, b);
}

// ---------------- setup / loop ----------------
void setup() {
  Serial.begin(115200);

  SPI.begin(PIN_SCLK, -1, PIN_MOSI, PIN_CS);
  tft.init(172, 320);
  tft.setSPISpeed(40000000);
  tft.setRotation(ROTATION);

  // backlight PWM (set after the display is up; 20 kHz so there's no visible/audible flicker)
  bool ok = ledcAttach(PIN_BL, 20000, 8);
  // eyes see brightness ~ duty^(1/2.2), so 50 % perceived is ~22 % duty
  ledcWrite(PIN_BL, (uint32_t)(powf(BACKLIGHT_PCT / 100.0f, 2.2f) * 255 + 0.5f));
  Serial.printf("RoboFace: backlight attach=%d duty=%u/255 freq=%u Hz\n",
                ok, (unsigned)ledcRead(PIN_BL), (unsigned)ledcReadFreq(PIN_BL));
  tft.fillScreen(ST77XX_BLACK);

  EYE = tft.color565(90, 220, 255);
  PUPIL = tft.color565(10, 30, 50);
  HILITE = tft.color565(255, 255, 255);
  fb = new GFXcanvas16(tft.width(), tft.height());

  randomSeed(esp_random());
  uint32_t now = millis();
  nextBlink = now + 1500;
  nextSmile = now + random(5000, 9000);
  nextGaze  = now + 2000;
}

void loop() {
  uint32_t now = millis();

  if (!blinking && now >= nextBlink) {
    blinking = true;
    blinkStart = now;
    pendingDouble = random(100) < 20;
    nextBlink = now + random(2000, 6000);
  }
  if (now >= nextSmile) {
    smileStart = now;
    smileEnd = now + random(2000, 4000);
    nextSmile = smileEnd + random(8000, 20000);
  }
  if (now >= nextGaze) {
    bool centre = random(100) < 40;
    gazeTX = centre ? 0 : random(-12, 13);
    gazeTY = centre ? 0 : random(-8, 9);
    nextGaze = now + random(1500, 4500);
  }
  gazeX += (gazeTX - gazeX) * 0.25f;
  gazeY += (gazeTY - gazeY) * 0.25f;

  drawFace(now);
  updateLed(now);

  static uint32_t lastReport = 0;               // status over USB serial every 5 s
  if (now - lastReport > 5000) {
    lastReport = now;
    Serial.printf("RoboFace: backlight duty=%u/255 freq=%u Hz\n",
                  (unsigned)ledcRead(PIN_BL), (unsigned)ledcReadFreq(PIN_BL));
  }
}
