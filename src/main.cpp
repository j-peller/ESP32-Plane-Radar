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

void fetchAndDrawAircraft() {
  const float fetch_km = ui::radar::fetchRadiusKm();
  if (!services::adsb::fetchUpdate(services::location::lat(),
                                   services::location::lon(), fetch_km)) {
    handleBootButton();
    return;
  }
  // Drawing is handled continuously in the main loop
  handleBootButton();
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
      // 1. Fetch ADS-B data periodically
      if (millis() - g_last_adsb_fetch_ms >= config::kAdsbFetchIntervalMs) {
        g_last_adsb_fetch_ms = millis();
        fetchAndDrawAircraft();
      }

      // 2. Continuous sweep animation when visible
      static float sweep_angle = 0.0f;
      static unsigned long last_sweep_ms = 0;
      unsigned long now = millis();
      if (last_sweep_ms == 0) {
        last_sweep_ms = now;
      }
      unsigned long elapsed = now - last_sweep_ms;
      if (elapsed >= 30) { // ~33 FPS target
        last_sweep_ms = now;

        // Speed: 360 degrees every 4.0 seconds = 0.09 degrees per millisecond
        constexpr float kDegreesPerMs = 360.0f / 4000.0f;
        sweep_angle += elapsed * kDegreesPerMs;
        if (sweep_angle >= 360.0f) {
          sweep_angle = fmod(sweep_angle, 360.0f);
        }
        ui::radarDisplayRefreshWithSweep(sweep_angle);
      }
    }
  }

  delay(10);
}
