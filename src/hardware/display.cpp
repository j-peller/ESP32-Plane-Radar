#include "hardware/display.h"

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

  s_bg.setColorDepth(16);
  // Allocate sprite buffer from general internal RAM (much less fragmented than DMA memory)
  void* buf = heap_caps_malloc(config::kDisplayWidth * config::kDisplayHeight * 2, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
  if (buf) {
    s_bg.setBuffer(buf, config::kDisplayWidth, config::kDisplayHeight, 16);
    s_bg_ready = true;
  } else {
    // Fallback to standard createSprite if heap_caps_malloc fails
    if (s_bg.createSprite(config::kDisplayWidth, config::kDisplayHeight)) {
      s_bg_ready = true;
    } else {
      Serial.println("display: s_bg sprite creation failed");
    }
  }
}

