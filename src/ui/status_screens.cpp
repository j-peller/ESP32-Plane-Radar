#include "ui/status_screens.h"

#include <lgfx/v1/lgfx_fonts.hpp>
#include <qrcode.h>

#include <cmath>
#include <cstdio>
#include <cstddef>
#include <cstring>

#include "config.h"
#include "hardware/display.h"
#include "hardware/display_font.h"

namespace fonts = lgfx::v1::fonts;

namespace {

constexpr int kLineGap = 6;
const int kCenterX = config::kDisplayWidth / 2;
const int kCenterY = config::kDisplayHeight / 2;

constexpr int kSpinnerDotCount = 10;
constexpr int kSpinnerRadius = 113;
constexpr int kSpinnerDotRadius = 2;
constexpr int kSpinnerEraseRadius = 4;
constexpr float kSpinnerStepDeg = 6.0f;

struct SpinnerDot {
  int x = 0;
  int y = 0;
  bool drawn = false;
};

char s_connecting_ssid[33];
char s_ssid_line[33];
constexpr int kConnectingTextMaxWidthPx = 220;
float s_spinner_angle_deg = -90.0f;
SpinnerDot s_spinner_dots[kSpinnerDotCount];
bool s_connecting_text_drawn = false;

constexpr auto& kGfxTitle = fonts::FreeSans18pt7b;
constexpr auto& kGfxBody = fonts::FreeSans12pt7b;
constexpr auto& kGfxDetail = fonts::Font2;
constexpr auto& kPortalGfxTitle = fonts::FreeSansBold18pt7b;
constexpr auto& kPortalGfxBody = fonts::FreeSansBold12pt7b;
constexpr auto& kPortalGfxEmphasis = fonts::FreeSansBold18pt7b;
constexpr auto& kConnectingGfxDetail = fonts::FreeSans9pt7b;

struct TextLine {
  const char* text;
  float vlw_size;
  const lgfx::GFXfont* gfx_font;
};

// Helper to direct drawing calls to s_bg (double-buffer) if ready, or fallback to tft
lgfx::LovyanGFX* screen_gfx() {
  return s_bg_ready ? (lgfx::LovyanGFX*)&s_bg : (lgfx::LovyanGFX*)&tft;
}

// Commits the sprite buffer to the physical display and streams it to the virtual PC display
void screen_commit() {
  if (s_bg_ready) {
#ifdef ENABLE_VIRTUAL_DISPLAY
    if (config::kVirtualDisplayEnabled) {
      static unsigned long last_serial_send = 0;
      if (last_serial_send == 0 || millis() - last_serial_send >= 200) {
        last_serial_send = millis();
        Serial.write((const uint8_t*)"\xAA\xBB\xCC\xDD\xA5\x5A\xA5\x5A\x11\x22\x33\x44\x55\x66\x77\x88", 16);
        Serial.write((const uint8_t*)s_bg.getBuffer(), config::kDisplayWidth * config::kDisplayHeight * 2);
      }
    }
#endif
    s_bg.pushSprite(0, 0);
  }
}

int lineHeightGfx(const lgfx::GFXfont* font) {
  screen_gfx()->setFont(font);
  screen_gfx()->setTextSize(1);
  return screen_gfx()->fontHeight();
}

int lineHeightVlw(float size) {
  displayFontSetSmoothSize(*screen_gfx(), size);
  return screen_gfx()->fontHeight();
}

void applyLineStyle(const TextLine& line) {
  if (displayFontIsSmooth()) {
    displayFontSetSmoothSize(*screen_gfx(), line.vlw_size);
  } else {
    displayFontSetBitmap(*screen_gfx(), line.gfx_font);
  }
}

void drawTextBlock(uint16_t bg, uint16_t fg, const TextLine* lines, size_t count) {
  screen_gfx()->fillScreen(bg);
  screen_gfx()->setTextColor(fg, bg);
  screen_gfx()->setTextDatum(textdatum_t::middle_center);

  int total_h = 0;
  for (size_t i = 0; i < count; ++i) {
    if (displayFontIsSmooth()) {
      total_h += lineHeightVlw(lines[i].vlw_size);
    } else {
      total_h += lineHeightGfx(lines[i].gfx_font);
    }
    if (i + 1 < count) {
      total_h += kLineGap;
    }
  }

  int y = (config::kDisplayHeight - total_h) / 2;
  for (size_t i = 0; i < count; ++i) {
    applyLineStyle(lines[i]);
    const int h =
        displayFontIsSmooth() ? lineHeightVlw(lines[i].vlw_size)
                              : lineHeightGfx(lines[i].gfx_font);
    screen_gfx()->drawString(lines[i].text, kCenterX, y + h / 2);
    y += h + kLineGap;
  }
  
  screen_commit();
}

constexpr float kConnectingDetailVlw = 0.92f;

void applyConnectingDetailStyle() {
  if (displayFontIsSmooth()) {
    displayFontSetSmoothSize(*screen_gfx(), kConnectingDetailVlw);
  } else {
    displayFontSetBitmap(*screen_gfx(), &kConnectingGfxDetail);
  }
}

/** SSID on one line; truncate with … if wider than kConnectingTextMaxWidthPx. */
void fitSsidLine() {
  strncpy(s_ssid_line, s_connecting_ssid, sizeof(s_ssid_line) - 1);
  s_ssid_line[sizeof(s_ssid_line) - 1] = '\0';
  applyConnectingDetailStyle();
  if (screen_gfx()->textWidth(s_ssid_line) <= kConnectingTextMaxWidthPx) {
    return;
  }
  const size_t len = strlen(s_connecting_ssid);
  for (size_t n = len; n > 0; --n) {
    snprintf(s_ssid_line, sizeof(s_ssid_line), "%.*s…", static_cast<int>(n),
             s_connecting_ssid);
    if (screen_gfx()->textWidth(s_ssid_line) <= kConnectingTextMaxWidthPx) {
      return;
    }
  }
  strncpy(s_ssid_line, "…", sizeof(s_ssid_line) - 1);
  s_ssid_line[sizeof(s_ssid_line) - 1] = '\0';
}

void drawConnectingText() {
  screen_gfx()->fillScreen(config::kColorBlack);

  screen_gfx()->setTextDatum(textdatum_t::middle_center);
  screen_gfx()->setTextColor(config::kTextOnBlack, config::kColorBlack);

  applyConnectingDetailStyle();
  const int detail_h = screen_gfx()->fontHeight();
  const int total_h = detail_h * 2 + kLineGap;
  const int block_top = (config::kDisplayHeight - total_h) / 2;
  constexpr int kPanelPadY = 8;
  screen_gfx()->fillRect(kCenterX - kConnectingTextMaxWidthPx / 2, block_top - kPanelPadY,
                         kConnectingTextMaxWidthPx, total_h + kPanelPadY * 2, config::kColorBlack);

  int y = block_top;
  screen_gfx()->drawString("Connecting to", kCenterX, y + detail_h / 2);
  y += detail_h + kLineGap;
  screen_gfx()->drawString(s_ssid_line, kCenterX, y + detail_h / 2);

  s_connecting_text_drawn = true;
  screen_commit();
}

void eraseSpinnerDots() {
  for (int i = 0; i < kSpinnerDotCount; ++i) {
    if (!s_spinner_dots[i].drawn) {
      continue;
    }
    screen_gfx()->fillCircle(s_spinner_dots[i].x, s_spinner_dots[i].y, kSpinnerEraseRadius,
                             config::kColorBlack);
    s_spinner_dots[i].drawn = false;
  }
}

void drawSpinnerDots() {
  constexpr float kDegToRad = 0.01745329252f;
  const float head_rad = s_spinner_angle_deg * kDegToRad;

  for (int i = 0; i < kSpinnerDotCount; ++i) {
    const float a = head_rad - static_cast<float>(i) * (6.283185307f / kSpinnerDotCount);
    const int x = kCenterX + static_cast<int>(std::lround(std::cos(a) * kSpinnerRadius));
    const int y = kCenterY + static_cast<int>(std::lround(std::sin(a) * kSpinnerRadius));

    const int fade = 255 - i * 22;
    const uint16_t color = screen_gfx()->color565(0, fade, 0);
    screen_gfx()->fillSmoothCircle(x, y, kSpinnerDotRadius, color);

    s_spinner_dots[i].x = x;
    s_spinner_dots[i].y = y;
    s_spinner_dots[i].drawn = true;
  }
  
  screen_commit();
}

// Generates a QR Code and draws it centered inside the circle viewport
void drawQrCode(const char* qrText) {
  QRCode qrcode;
  uint8_t qrcodeBytes[qrcode_getBufferSize(3)];
  qrcode_initText(&qrcode, qrcodeBytes, 3, ECC_LOW, qrText);

  const int scale = 4;
  const int qrSize = qrcode.size * scale; // 116 pixels
  
  const int qr_x = kCenterX - qrSize / 2;
  const int qr_y = kCenterY - qrSize / 2;
  
  const int pad = 8;
  // Draw white background / quiet zone
  screen_gfx()->fillRect(qr_x - pad, qr_y - pad, qrSize + pad * 2, qrSize + pad * 2, 0xFFFF);
  
  // Draw black modules
  for (uint8_t y = 0; y < qrcode.size; y++) {
    for (uint8_t x = 0; x < qrcode.size; x++) {
      if (qrcode_getModule(&qrcode, x, y)) {
        screen_gfx()->fillRect(qr_x + x * scale, qr_y + y * scale, scale, scale, 0x0000);
      }
    }
  }
}

}  // namespace

void statusScreenConnectingBegin(const char* ssid) {
  const char* name = (ssid != nullptr && ssid[0] != '\0') ? ssid : "network";
  strncpy(s_connecting_ssid, name, sizeof(s_connecting_ssid) - 1);
  s_connecting_ssid[sizeof(s_connecting_ssid) - 1] = '\0';
  fitSsidLine();
  s_spinner_angle_deg = -90.0f;
  for (auto& dot : s_spinner_dots) {
    dot.drawn = false;
  }
  s_connecting_text_drawn = false;
  drawConnectingText();
  drawSpinnerDots();
}

void statusScreenConnectingTick() {
  if (!s_connecting_text_drawn) {
    drawConnectingText();
  }
  eraseSpinnerDots();
  s_spinner_angle_deg += kSpinnerStepDeg;
  if (s_spinner_angle_deg >= 270.0f) {
    s_spinner_angle_deg -= 360.0f;
  }
  drawSpinnerDots();
}

void statusScreenPortal() {
  // 1. Clear screen with yellow background
  screen_gfx()->fillScreen(config::kColorYellow);
  screen_gfx()->setTextColor(config::kTextOnYellow, config::kColorYellow);
  screen_gfx()->setTextDatum(textdatum_t::middle_center);

  // 2. Draw Title Text above the QR code
  if (displayFontIsSmooth()) {
    displayFontSetSmoothSize(*screen_gfx(), 1.15f);
  } else {
    displayFontSetBitmap(*screen_gfx(), &kPortalGfxTitle);
  }
  screen_gfx()->drawString("Wi-Fi Setup", kCenterX, 28);

  // 3. Draw QR code in the center (SSID link format)
  char qr_text[128];
  snprintf(qr_text, sizeof(qr_text), "WIFI:S:%s;T:nopass;;", config::kPortalApName);
  drawQrCode(qr_text);

  // 4. Draw SSID instructions below the QR code
  if (displayFontIsSmooth()) {
    displayFontSetSmoothSize(*screen_gfx(), 1.0f);
  } else {
    displayFontSetBitmap(*screen_gfx(), &kPortalGfxBody);
  }
  screen_gfx()->drawString("Scan to connect AP:", kCenterX, 202);
  
  if (displayFontIsSmooth()) {
    displayFontSetSmoothSize(*screen_gfx(), 1.12f);
  } else {
    displayFontSetBitmap(*screen_gfx(), &kPortalGfxEmphasis);
  }
  screen_gfx()->drawString(config::kPortalApName, kCenterX, 222);

  // 5. Commit frame
  screen_commit();
}

void statusScreenConnectFailed() {
  const TextLine lines[] = {
      {"Could not connect", 1.15f, &kGfxTitle},
      {"Check Wi-Fi password", 1.0f, &kGfxBody},
      {"and signal strength.", 1.0f, &kGfxBody},
      {"Hold BOOT 3 sec", 1.0f, &kGfxBody},
      {"to reset Wi-Fi", 1.0f, &kGfxBody},
  };
  drawTextBlock(config::kColorYellow, config::kTextOnYellow, lines,
                sizeof(lines) / sizeof(lines[0]));
}

void statusScreenWifiReset() {
  const TextLine lines[] = {
      {"Wi-Fi reset", 1.15f, &kPortalGfxTitle},
      {"Restarting...", 1.05f, &kPortalGfxBody},
  };
  drawTextBlock(config::kColorYellow, config::kTextOnYellow, lines,
                sizeof(lines) / sizeof(lines[0]));
}
