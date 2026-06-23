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



