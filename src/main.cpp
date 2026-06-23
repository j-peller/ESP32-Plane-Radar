/**
 * Plane Radar — WiFi setup, then radar UI on the round GC9A01 display.
 */

#include <Arduino.h>
#include <WiFi.h>
#include "esp_bt.h"

#include "config.h"
#include "hardware/display.h"
#include "services/adsb_client.h"
#include "services/radar_location.h"
#include "services/wifi_setup.h"
#include "ui/radar_display.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"
#include "ui/status_screens.h"

namespace {

bool g_radar_visible = false;
unsigned long g_wifi_down_since = 0;
unsigned long g_last_reconnect_ms = 0;
unsigned long g_last_adsb_fetch_ms = 0;
bool g_pulse_active = false;
unsigned long g_pulse_start_ms = 0;

void showRadarIfConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    g_radar_visible = false;
    return;
  }
  ui::radarDisplayDraw();
  g_radar_visible = true;
}

unsigned long g_last_tap_ms = 0;
bool g_pending_tap = false;

void onThemeToggle() {
  ui::radar::toggleTheme();
  Serial.printf("Theme toggled: %s\n", ui::radar::isRetroTheme() ? "Retro Green" : "Multicolor");
  g_pulse_active = false;
  if (g_radar_visible && WiFi.status() == WL_CONNECTED) {
    ui::radarDisplayDraw();
  }
}

void onRangeTap() {
  ui::radar::rangeNext();
  char range_label[12];
  ui::radar::formatCurrentRing3Label(range_label, sizeof(range_label));
  Serial.printf("Range: %s (outer ~%.0f km)\n", range_label,
                ui::radar::rangeCurrent().outer_km);

  g_pulse_active = false;
  if (g_radar_visible && WiFi.status() == WL_CONNECTED) {
    ui::radarDisplayDraw();
  }
}

void handleBootButton() {
  bootButtonPollLongPress();
  if (bootButtonConsumeTap()) {
    unsigned long now = millis();
    if (g_pending_tap && (now - g_last_tap_ms < 400)) {
      g_pending_tap = false;
      onThemeToggle();
    } else {
      g_pending_tap = true;
      g_last_tap_ms = now;
    }
  }

  if (g_pending_tap && (millis() - g_last_tap_ms >= 400)) {
    g_pending_tap = false;
    onRangeTap();
  }
}

TaskHandle_t g_fetch_task = nullptr;
volatile bool g_fetch_completed = false;

void fetchTask(void* pvParameters) {
  while (true) {
    if (WiFi.status() == WL_CONNECTED && g_radar_visible) {
      const float fetch_km = ui::radar::fetchRadiusKm();
      bool success = services::adsb::fetchUpdate(services::location::lat(),
                                                 services::location::lon(), fetch_km);
      if (success) {
        g_fetch_completed = true;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(config::kAdsbFetchIntervalMs));
  }
}

}  // namespace

void setup() {
  // Release Bluetooth memory since this application only uses Wi-Fi.
  // This reclaims ~70KB of contiguous internal RAM, which is required for the 115KB LovyanGFX sprite.
  esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);

  Serial.begin(230400);
  delay(500);
  Serial.println();
  Serial.println("Plane Radar");
  Serial.printf("Free heap at startup: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("Max contiguous heap block: %d bytes\n", ESP.getMaxAllocHeap());

  bootButtonInit();
  displayInit();
  if (wifiShowsSetupScreenOnBoot()) {
    statusScreenPortal();
  }
  services::location::init();
  ui::radar::rangeInit();
  ui::radar::themeInit();

  if (wifiSetupConnect()) {
    showRadarIfConnected();
  }

  // Start the background fetch task continuously on Core 1
  xTaskCreatePinnedToCore(fetchTask, "ADSB_Fetch", 16384, NULL, 1, &g_fetch_task, 1);
}

void loop() {
  handleBootButton();

  if (WiFi.status() != WL_CONNECTED) {
    if (g_radar_visible) {
      Serial.println("WiFi lost — will reconnect");
      g_radar_visible = false;
    }

    if (g_wifi_down_since == 0) {
      g_wifi_down_since = millis();
    }

    const unsigned long down_ms = millis() - g_wifi_down_since;
    if (down_ms >= config::kWifiDownGraceMs &&
        millis() - g_last_reconnect_ms >= config::kWifiReconnectIntervalMs) {
      g_last_reconnect_ms = millis();
      if (wifiReconnect()) {
        g_wifi_down_since = 0;
        showRadarIfConnected();
      }
    }
  } else {
    g_wifi_down_since = 0;
    if (!g_radar_visible) {
      showRadarIfConnected();
    } else {
      // 1. Fetch ADS-B data periodically (handled by background task)

      // Check if fetch completed and trigger pulse
      if (g_fetch_completed) {
        g_fetch_completed = false;
        g_pulse_active = true;
        g_pulse_start_ms = millis();
      }

      // 2. Drive the refresh pulse animation
      if (g_pulse_active) {
        unsigned long elapsed = millis() - g_pulse_start_ms;
        constexpr unsigned long kPulseDurationMs = 800; // 800ms pulse duration
        if (elapsed >= kPulseDurationMs) {
          g_pulse_active = false;
          ui::radarDisplayRefreshWithPulse(-1); // Final static draw
        } else {
          int radius = (elapsed * ui::radar::kCenterX) / kPulseDurationMs;
          ui::radarDisplayRefreshWithPulse(radius);
        }
      }
    }
  }

  delay(10);
}
