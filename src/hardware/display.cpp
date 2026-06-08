#include "hardware/display.h"

#include <Arduino.h>
#include "hardware/display_font.h"
#include "config.h"
#include <esp_heap_caps.h>

LGFX tft;
LGFX_Sprite s_bg(&tft);
bool s_bg_ready = false;

void displayInit() {
  tft.init();
  tft.setRotation(0);
  tft.setBrightness(255);
  tft.setTextWrap(false);
  displayFontInit();

  Serial.printf("display: Free heap before sprite: %d\n", ESP.getFreeHeap());
  Serial.printf("display: Max block before sprite: %d\n", ESP.getMaxAllocHeap());

  s_bg.setColorDepth(16);
  // Allocate from heap. On ESP32, 115200 bytes fits in heap if allocated early.
  void* buf = malloc(config::kDisplayWidth * config::kDisplayHeight * 2);
  if (buf) {
    s_bg.setBuffer(buf, config::kDisplayWidth, config::kDisplayHeight, 16);
    s_bg_ready = true;
    Serial.println("display: s_bg sprite allocated via malloc (16-bit)");
  } else {
    // Fallback to standard LovyanGFX allocator for 16-bit
    if (s_bg.createSprite(config::kDisplayWidth, config::kDisplayHeight)) {
      s_bg_ready = true;
      Serial.println("display: s_bg sprite allocated via createSprite (16-bit)");
    } else {
      Serial.println("display: 16-bit sprite allocation failed! Trying 8-bit fallback...");
      s_bg.setColorDepth(8);
      void* buf8 = malloc(config::kDisplayWidth * config::kDisplayHeight);
      if (buf8) {
        s_bg.setBuffer(buf8, config::kDisplayWidth, config::kDisplayHeight, 8);
        s_bg_ready = true;
        Serial.println("display: s_bg sprite allocated via malloc (8-bit fallback)");
      } else {
        if (s_bg.createSprite(config::kDisplayWidth, config::kDisplayHeight)) {
          s_bg_ready = true;
          Serial.println("display: s_bg sprite allocated via createSprite (8-bit fallback)");
        } else {
          Serial.println("display: 8-bit sprite allocation failed! No double-buffering available.");
        }
      }
    }
  }
}

#ifdef ENABLE_VIRTUAL_DISPLAY
void displayStreamVirtual() {
  if (!config::kVirtualDisplayEnabled || !s_bg_ready) {
    return;
  }
  static unsigned long last_serial_send = 0;
  unsigned long now = millis();
  if (last_serial_send != 0 && now - last_serial_send < 200) {
    return;
  }
  last_serial_send = now;

  Serial.write((const uint8_t*)"\xAA\xBB\xCC\xDD\xA5\x5A\xA5\x5A\x11\x22\x33\x44\x55\x66\x77\x88", 16);
  if (s_bg.getColorDepth() == 8) {
    // Unpack RGB332 to RGB565 and stream
    static uint16_t lut[256];
    static bool lut_ready = false;
    if (!lut_ready) {
      for (int i = 0; i < 256; ++i) {
        uint8_t r = (i >> 5) & 0x07;
        uint8_t g = (i >> 2) & 0x07;
        uint8_t b = i & 0x03;
        uint16_t r5 = (r * 31) / 7;
        uint16_t g6 = (g * 63) / 7;
        uint16_t b5 = (b * 31) / 3;
        lut[i] = (r5 << 11) | (g6 << 5) | b5;
      }
      lut_ready = true;
    }
    uint8_t* buf = (uint8_t*)s_bg.getBuffer();
    uint16_t line_buf[config::kDisplayWidth];
    for (int y = 0; y < config::kDisplayHeight; ++y) {
      int offset = y * config::kDisplayWidth;
      for (int x = 0; x < config::kDisplayWidth; ++x) {
        line_buf[x] = lut[buf[offset + x]];
      }
      Serial.write((const uint8_t*)line_buf, config::kDisplayWidth * 2);
    }
  } else {
    Serial.write((const uint8_t*)s_bg.getBuffer(), config::kDisplayWidth * config::kDisplayHeight * 2);
  }
}
#endif



