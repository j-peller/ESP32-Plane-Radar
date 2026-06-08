#include "ui/radar_display.h"

#include <lgfx/v1/lgfx_fonts.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "config.h"
#include "hardware/display.h"
#include "hardware/display_font.h"
#include "services/adsb_client.h"
#include "services/radar_location.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"

namespace fonts = lgfx::v1::fonts;

namespace ui {
namespace radar {

uint16_t kColorBackground = 0x0000;
uint16_t kColorGrid = 0x0320;
uint16_t kColorLabel = 0xFFFF;
uint16_t kColorCenter = 0xFFFF;
uint16_t kColorAircraft = 0x001F;
uint16_t kColorTrackVector = 0xFFFF;
uint16_t kColorTagType = 0x5DFF;
uint16_t kColorTagAltitude = 0xFFE0;

}  // namespace radar

namespace {

int s_pulse_radius = -1;

uint16_t fadeColor(uint16_t color, float intensity) {
  if (intensity <= 0.0f) return 0;
  if (intensity >= 1.0f) return color;
  
  uint8_t r = (color >> 11) & 0x1F;
  uint8_t g = (color >> 5) & 0x3F;
  uint8_t b = color & 0x1F;
  
  r = static_cast<uint8_t>(r * intensity);
  g = static_cast<uint8_t>(g * intensity);
  b = static_cast<uint8_t>(b * intensity);
  
  return (r << 11) | (g << 5) | b;
}

bool s_label_metrics_ready = false;
bool s_cardinal_use_vlw = false;
bool s_scale_use_vlw = false;
float s_cardinal_vlw_size = 0.56f;
float s_scale_vlw_size = 0.50f;
float s_tag_vlw_size = 0.56f;
const lgfx::GFXfont* s_cardinal_gfx = &fonts::FreeSansBold12pt7b;
const lgfx::GFXfont* s_scale_gfx = &fonts::FreeSansBold9pt7b;
const lgfx::GFXfont* s_tag_gfx = &fonts::FreeSansBold12pt7b;

bool s_tag_label_metrics_ready = false;
bool s_tag_use_vlw = false;

int s_scale_label_max_w = 0;
int s_scale_label_h = 0;

lgfx::LovyanGFX* s_draw = &tft;

class DrawScope {
 public:
  explicit DrawScope(lgfx::LovyanGFX& gfx) : prev_(s_draw) { s_draw = &gfx; }
  ~DrawScope() { s_draw = prev_; }

 private:
  lgfx::LovyanGFX* prev_;
};

int absDiff(int a, int b) { return std::abs(a - b); }

int measureGfxHeight(const lgfx::GFXfont& font) {
  tft.setFont(&font);
  tft.setTextSize(1);
  return tft.fontHeight();
}

int measureVlwHeight(float size) {
  tft.setTextSize(size);
  return tft.fontHeight();
}

float findVlwSizeForHeight(int target_px) {
  float lo = 0.25f;
  float hi = 1.2f;
  for (int i = 0; i < 16; ++i) {
    const float mid = (lo + hi) * 0.5f;
    if (measureVlwHeight(mid) < target_px) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  return hi;
}

void applyScaleStyle();

const lgfx::GFXfont* pickGfxFontClosest(
    int target_px, const lgfx::GFXfont* const* candidates, size_t count) {
  const lgfx::GFXfont* best = candidates[0];
  int best_diff = absDiff(measureGfxHeight(*best), target_px);

  for (size_t i = 1; i < count; ++i) {
    const int diff = absDiff(measureGfxHeight(*candidates[i]), target_px);
    if (diff < best_diff) {
      best_diff = diff;
      best = candidates[i];
    }
  }
  return best;
}

void initLabelMetrics() {
  if (s_label_metrics_ready) {
    return;
  }

  const int cardinal_target = radar::kCardinalLabelHeightPx;

  if (displayFontIsSmooth()) {
    s_cardinal_use_vlw = true;
    s_cardinal_vlw_size = findVlwSizeForHeight(cardinal_target);
    const int cardinal_h = measureVlwHeight(s_cardinal_vlw_size);
    const int scale_target = cardinal_h - radar::kScaleBelowCardinalPx;
    s_scale_use_vlw = true;
    s_scale_vlw_size = findVlwSizeForHeight(scale_target);
  } else {
    const lgfx::GFXfont* cardinal_candidates[] = {&fonts::FreeSansBold12pt7b,
                                                  &fonts::FreeSansBold9pt7b};
    s_cardinal_gfx =
        pickGfxFontClosest(cardinal_target, cardinal_candidates, 2);
    s_cardinal_use_vlw = false;

    const int cardinal_h = measureGfxHeight(*s_cardinal_gfx);
    const int scale_target = cardinal_h - radar::kScaleBelowCardinalPx;
    const lgfx::GFXfont* scale_candidates[] = {&fonts::FreeSansBold9pt7b,
                                               &fonts::FreeSansBold12pt7b};
    s_scale_gfx = pickGfxFontClosest(scale_target, scale_candidates, 2);
    s_scale_use_vlw = false;
  }

  applyScaleStyle();
  s_scale_label_h = tft.fontHeight();
  s_scale_label_max_w = 0;
  char label[12];
  for (size_t i = 0; i < radar::kRangePresetCount; ++i) {
    for (bool miles : {false, true}) {
      radar::formatRing3Label(label, sizeof(label), radar::kRangePresets[i].ring3_km,
                              miles);
      const int w = tft.textWidth(label);
      if (w > s_scale_label_max_w) {
        s_scale_label_max_w = w;
      }
    }
  }

  s_label_metrics_ready = true;
}

void initTagLabelMetrics() {
  if (s_tag_label_metrics_ready) {
    return;
  }

  const int target = radar::kAircraftTagLabelHeightPx;
  if (displayFontIsSmooth()) {
    s_tag_use_vlw = true;
    s_tag_vlw_size = findVlwSizeForHeight(target);
  } else {
    const lgfx::GFXfont* tag_candidates[] = {&fonts::FreeSansBold12pt7b,
                                               &fonts::FreeSansBold9pt7b};
    s_tag_gfx = pickGfxFontClosest(target, tag_candidates, 2);
    s_tag_use_vlw = false;
  }

  s_tag_label_metrics_ready = true;
}

void initPalette() {
  if (radar::isRetroTheme()) {
    radar::kColorBackground = tft.color565(0, 8, 2);
    radar::kColorGrid = tft.color565(0, 100, 30);
    radar::kColorLabel = tft.color565(0, 220, 60);
    radar::kColorCenter = tft.color565(120, 255, 150);
    radar::kColorAircraft = tft.color565(150, 255, 150);
    radar::kColorTrackVector = tft.color565(0, 180, 50);
    radar::kColorTagType = tft.color565(0, 150, 40);
    radar::kColorTagAltitude = tft.color565(0, 130, 30);
  } else {
    radar::kColorBackground = tft.color565(radar::kBgR, radar::kBgG, radar::kBgB);
    radar::kColorGrid = tft.color565(radar::kGridR, radar::kGridG, radar::kGridB);
    radar::kColorLabel = tft.color565(255, 255, 255);
    radar::kColorCenter = tft.color565(255, 255, 255);
    // GC9A01 BGR panel: swap R/B in color565 so logical red renders red on screen.
    if (config::kDisplayRgbOrder) {
      radar::kColorAircraft =
          tft.color565(radar::kAircraftB, radar::kAircraftG, radar::kAircraftR);
    } else {
      radar::kColorAircraft =
          tft.color565(radar::kAircraftR, radar::kAircraftG, radar::kAircraftB);
    }
    radar::kColorTrackVector =
        tft.color565(radar::kTrackR, radar::kTrackG, radar::kTrackB);
    radar::kColorTagType =
        tft.color565(radar::kTagTypeR, radar::kTagTypeG, radar::kTagTypeB);
    radar::kColorTagAltitude =
        tft.color565(radar::kTagAltR, radar::kTagAltG, radar::kTagAltB);
  }
}

constexpr float kKmPerDeg = 111.0f;

void offsetKmFromCenter(float lat, float lon, float* dx_km, float* dy_km,
                        float* dist_km) {
  *dx_km =
      static_cast<float>(lon - services::location::lon()) * kKmPerDeg;
  *dy_km =
      static_cast<float>(lat - services::location::lat()) * kKmPerDeg;
  *dist_km = sqrtf((*dx_km) * (*dx_km) + (*dy_km) * (*dy_km));
}

float innerRingMaxKm() {
  const float outer_km = radar::rangeCurrent().outer_km;
  return outer_km * (static_cast<float>(radar::kGridOuterRadius -
                                       radar::kAircraftInsideRingInsetPx) /
                     static_cast<float>(radar::kGridOuterRadius));
}

/** Flat lat/lon as x/y: 1° ≈ 111 km, north = screen up. */
void latLonToScreen(float lat, float lon, int* out_x, int* out_y) {
  const float outer_km = radar::rangeCurrent().outer_km;
  const float px_per_km = static_cast<float>(radar::kGridOuterRadius) / outer_km;

  float dx_km = 0.0f;
  float dy_km = 0.0f;
  float dist_km = 0.0f;
  offsetKmFromCenter(lat, lon, &dx_km, &dy_km, &dist_km);

  *out_x = radar::kCenterX + static_cast<int>(lroundf(dx_km * px_per_km));
  *out_y = radar::kCenterY - static_cast<int>(lroundf(dy_km * px_per_km));
}

bool isInsideOuterRingKm(float dist_km) { return dist_km <= innerRingMaxKm(); }

int distSqFromCenter(int x, int y) {
  const int dx = x - radar::kCenterX;
  const int dy = y - radar::kCenterY;
  return dx * dx + dy * dy;
}

bool isInsideOuterRing(int x, int y) {
  const int max_r = radar::kGridOuterRadius - radar::kAircraftInsideRingInsetPx;
  return distSqFromCenter(x, y) <= max_r * max_r;
}

/** Rim dot from true bearing; always on screen edge (even if target is 50+ km away). */
bool beyondRingEdgeDotFromLatLon(float lat, float lon, int* out_x, int* out_y) {
  float dx_km = 0.0f;
  float dy_km = 0.0f;
  float dist_km = 0.0f;
  offsetKmFromCenter(lat, lon, &dx_km, &dy_km, &dist_km);
  if (dist_km < 0.01f) {
    return false;
  }
  if (isInsideOuterRingKm(dist_km)) {
    return false;
  }

  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int rim_r = radar::kCenterX - radar::kBeyondRingScreenMarginPx;
  const float angle_rad = atan2f(dx_km, dy_km);

  *out_x = cx + static_cast<int>(lroundf(sinf(angle_rad) * rim_r));
  *out_y = cy - static_cast<int>(lroundf(cosf(angle_rad) * rim_r));
  return true;
}

void drawBeyondRingDot(int x, int y, float intensity) {
  s_draw->fillSmoothCircle(x, y, radar::kBeyondRingDotRadiusPx, fadeColor(radar::kColorAircraft, intensity));
}

void clipPointToOuterRing(int x0, int y0, int* x1, int* y1) {
  const int max_r = radar::kGridOuterRadius;
  const int max_r_sq = max_r * max_r;
  if (distSqFromCenter(*x1, *y1) <= max_r_sq) {
    return;
  }

  const int dx = *x1 - x0;
  const int dy = *y1 - y0;
  float t = 1.0f;
  for (int step = 0; step < 20; ++step) {
    const int px = x0 + static_cast<int>(lroundf(dx * t));
    const int py = y0 + static_cast<int>(lroundf(dy * t));
    if (distSqFromCenter(px, py) <= max_r_sq) {
      *x1 = px;
      *y1 = py;
      return;
    }
    t -= 0.05f;
    if (t <= 0.0f) {
      *x1 = x0;
      *y1 = y0;
      return;
    }
  }
}

int speedLineLengthPx(float gs_knots) {
  if (gs_knots <= 0.0f) {
    return 0;
  }

  // Fixed screen scale: 60 s horizon at gs, not tied to current range zoom.
  constexpr float kKmPerKnotPerHorizon =
      1.852f * radar::kAircraftTrackHorizonSec / 3600.0f;
  const float px =
      gs_knots * kKmPerKnotPerHorizon * radar::kGridOuterRadius /
      radar::kAircraftTrackRefOuterKm * radar::kAircraftTrackLengthScale;

  const int len = static_cast<int>(px + 0.5f);
  if (len < radar::kAircraftSpeedLineMinPx) {
    return radar::kAircraftSpeedLineMinPx;
  }
  return len;
}

void noseTip(int cx, int cy, float heading_deg, int* tip_x, int* tip_y) {
  constexpr float kDegToRad = 0.01745329252f;
  const float rad = heading_deg * kDegToRad;
  *tip_x = cx + static_cast<int>(lroundf(sinf(rad) * radar::kAircraftNoseLenPx));
  *tip_y = cy - static_cast<int>(lroundf(cosf(rad) * radar::kAircraftNoseLenPx));
}

void drawHeadingTriangle(int cx, int cy, float heading_deg, uint16_t color) {
  constexpr float kDegToRad = 0.01745329252f;
  const float rad = heading_deg * kDegToRad;
  const float sin_h = sinf(rad);
  const float cos_h = cosf(rad);

  int tip_x = 0;
  int tip_y = 0;
  noseTip(cx, cy, heading_deg, &tip_x, &tip_y);

  const int base_x =
      cx - static_cast<int>(lroundf(sin_h * static_cast<float>(radar::kAircraftTailLenPx)));
  const int base_y =
      cy + static_cast<int>(lroundf(cos_h * static_cast<float>(radar::kAircraftTailLenPx)));

  const int wing_x = static_cast<int>(lroundf(cos_h * radar::kAircraftTailHalfPx));
  const int wing_y = static_cast<int>(lroundf(sin_h * radar::kAircraftTailHalfPx));

  s_draw->fillTriangle(tip_x, tip_y, base_x + wing_x, base_y + wing_y,
                       base_x - wing_x, base_y - wing_y, color);
}

void drawSpeedVector(int cx, int cy, float heading_deg, float track_deg,
                     float gs_knots, uint16_t color) {
  const int len = speedLineLengthPx(gs_knots);
  if (len <= 0) {
    return;
  }

  int tip_x = 0;
  int tip_y = 0;
  noseTip(cx, cy, heading_deg, &tip_x, &tip_y);

  constexpr float kDegToRad = 0.01745329252f;
  const float rad = track_deg * kDegToRad;
  int ex = tip_x + static_cast<int>(lroundf(sinf(rad) * len));
  int ey = tip_y - static_cast<int>(lroundf(cosf(rad) * len));
  clipPointToOuterRing(tip_x, tip_y, &ex, &ey);
  if (ex == tip_x && ey == tip_y) {
    return;
  }
  s_draw->drawWideLine(tip_x, tip_y, ex, ey, radar::kAircraftTrackLineHalfWidth,
                       color);
}

void applyTagStyleToTft() {
  if (s_tag_use_vlw) {
    displayFontSetSmoothSize(*s_draw, s_tag_vlw_size);
  } else {
    displayFontSetBitmap(*s_draw, s_tag_gfx);
  }
}

int measureTagBlockWidth(const services::adsb::Aircraft& plane) {
  applyTagStyleToTft();
  int max_w = 0;
  if (plane.callsign[0] != '\0') {
    const int w = s_draw->textWidth(plane.callsign);
    if (w > max_w) {
      max_w = w;
    }
  }
  if (plane.type[0] != '\0') {
    const int w = s_draw->textWidth(plane.type);
    if (w > max_w) {
      max_w = w;
    }
  }
  if (plane.alt[0] != '\0') {
    const int w = s_draw->textWidth(plane.alt);
    if (w > max_w) {
      max_w = w;
    }
  }
  return max_w;
}

void drawAircraftTag(int x, int y, const services::adsb::Aircraft& plane, float intensity) {
  initTagLabelMetrics();
  applyTagStyleToTft();

  const int line_h = s_draw->fontHeight();
  const int block_w = measureTagBlockWidth(plane);
  const int block_h = line_h * 3;
  int ly = y - block_h / 2;

  const int symbol_half =
      radar::kAircraftNoseLenPx + radar::kAircraftTailHalfPx;
  // West (left): tag toward center on the right; east (right): tag on the left.
  const bool tag_on_right = x < radar::kCenterX;
  int anchor_x = 0;
  if (tag_on_right) {
    anchor_x = x + symbol_half + radar::kAircraftLabelGapPx;
    anchor_x = std::min(anchor_x, radar::kSize - block_w - 1);
    s_draw->setTextDatum(textdatum_t::top_left);
  } else {
    anchor_x = x - symbol_half - radar::kAircraftLabelGapPx;
    anchor_x = std::max(anchor_x, block_w + 1);
    s_draw->setTextDatum(textdatum_t::top_right);
  }
  ly = std::max(1, std::min(ly, radar::kSize - block_h - 1));

  if (plane.callsign[0] != '\0') {
    s_draw->setTextColor(fadeColor(radar::kColorLabel, intensity), radar::kColorBackground);
    s_draw->drawString(plane.callsign, anchor_x, ly);
  }
  ly += line_h;

  if (plane.type[0] != '\0') {
    s_draw->setTextColor(fadeColor(radar::kColorTagType, intensity), radar::kColorBackground);
    s_draw->drawString(plane.type, anchor_x, ly);
  }
  ly += line_h;

  if (plane.alt[0] != '\0') {
    s_draw->setTextColor(fadeColor(radar::kColorTagAltitude, intensity), radar::kColorBackground);
    s_draw->drawString(plane.alt, anchor_x, ly);
  }
}

struct AircraftDrawItem {
  size_t index = 0;
  int x = 0;
  int y = 0;
  int dist_sq = 0;
  float intensity = 0.0f;
};

struct BeyondDotDrawItem {
  int x = 0;
  int y = 0;
  int dist_sq = 0;
  float intensity = 0.0f;
};

void sortDrawItemsFarFirst(AircraftDrawItem* items, size_t count) {
  for (size_t i = 1; i < count; ++i) {
    const AircraftDrawItem key = items[i];
    size_t j = i;
    while (j > 0 && items[j - 1].dist_sq < key.dist_sq) {
      items[j] = items[j - 1];
      --j;
    }
    items[j] = key;
  }
}

void sortBeyondDotsFarFirst(BeyondDotDrawItem* items, size_t count) {
  for (size_t i = 1; i < count; ++i) {
    const BeyondDotDrawItem key = items[i];
    size_t j = i;
    while (j > 0 && items[j - 1].dist_sq < key.dist_sq) {
      items[j] = items[j - 1];
      --j;
    }
    items[j] = key;
  }
}



uint16_t mixGlowColor(uint16_t base_color, float glow_factor) {
  if (glow_factor <= 0.0f) return base_color;
  if (glow_factor >= 1.0f) return 0xFFFF; // Pure white
  uint8_t r = (base_color >> 11) & 0x1F;
  uint8_t g = (base_color >> 5) & 0x3F;
  uint8_t b = base_color & 0x1F;
  r = r + static_cast<uint8_t>((31 - r) * glow_factor);
  g = g + static_cast<uint8_t>((63 - g) * glow_factor);
  b = b + static_cast<uint8_t>((31 - b) * glow_factor);
  return (r << 11) | (g << 5) | b;
}

void drawAircraft() {
  initLabelMetrics();

  const size_t n_new = services::adsb::aircraftCount();
  const services::adsb::Aircraft* planes_new = services::adsb::aircraftList();

  const size_t n_old = services::adsb::aircraftCountOld();
  const services::adsb::Aircraft* planes_old = services::adsb::aircraftListOld();

  // Temporary list of targets to draw this frame
  struct RadarTarget {
    const services::adsb::Aircraft* plane;
    int x = 0;
    int y = 0;
    int dist_sq = 0;
    float glow_factor = 0.0f;
    bool is_rim_dot = false;
  };

  RadarTarget targets[128];
  size_t target_count = 0;

  if (s_pulse_radius < 0) {
    // Pulse is inactive: draw all new aircraft at their new positions, no glow
    for (size_t i = 0; i < n_new; ++i) {
      if (target_count >= 128) break;
      
      float dx_km = 0.0f, dy_km = 0.0f, dist_km = 0.0f;
      offsetKmFromCenter(planes_new[i].lat, planes_new[i].lon, &dx_km, &dy_km, &dist_km);
      
      int x = 0, y = 0;
      bool rim = false;
      if (isInsideOuterRingKm(dist_km)) {
        latLonToScreen(planes_new[i].lat, planes_new[i].lon, &x, &y);
      } else {
        if (!beyondRingEdgeDotFromLatLon(planes_new[i].lat, planes_new[i].lon, &x, &y)) {
          continue;
        }
        rim = true;
      }
      
      targets[target_count].plane = &planes_new[i];
      targets[target_count].x = x;
      targets[target_count].y = y;
      targets[target_count].dist_sq = distSqFromCenter(x, y);
      targets[target_count].glow_factor = 0.0f;
      targets[target_count].is_rim_dot = rim;
      ++target_count;
    }
  } else {
    // Pulse is active: synchronize positions
    // 1. Process new aircraft list
    for (size_t i = 0; i < n_new; ++i) {
      if (target_count >= 128) break;

      float dx_km = 0.0f, dy_km = 0.0f, dist_km = 0.0f;
      offsetKmFromCenter(planes_new[i].lat, planes_new[i].lon, &dx_km, &dy_km, &dist_km);
      
      int x_new = 0, y_new = 0;
      bool rim_new = false;
      if (isInsideOuterRingKm(dist_km)) {
        latLonToScreen(planes_new[i].lat, planes_new[i].lon, &x_new, &y_new);
      } else {
        if (!beyondRingEdgeDotFromLatLon(planes_new[i].lat, planes_new[i].lon, &x_new, &y_new)) {
          continue;
        }
        rim_new = true;
      }
      
      int d_sq_new = distSqFromCenter(x_new, y_new);
      int r_new = static_cast<int>(lroundf(sqrtf(d_sq_new)));

      if (s_pulse_radius >= r_new) {
        // Swept: draw at new position with glow if fresh
        float glow = 0.0f;
        int diff = s_pulse_radius - r_new;
        if (diff >= 0 && diff < 15) {
          glow = 1.0f - (static_cast<float>(diff) / 15.0f);
        }
        
        targets[target_count].plane = &planes_new[i];
        targets[target_count].x = x_new;
        targets[target_count].y = y_new;
        targets[target_count].dist_sq = d_sq_new;
        targets[target_count].glow_factor = glow;
        targets[target_count].is_rim_dot = rim_new;
        ++target_count;
      } else {
        // Not swept yet: find in old list
        int old_idx = -1;
        for (size_t j = 0; j < n_old; ++j) {
          if (strcmp(planes_new[i].callsign, planes_old[j].callsign) == 0) {
            old_idx = j;
            break;
          }
        }
        if (old_idx >= 0) {
          float dx_old = 0.0f, dy_old = 0.0f, dist_old = 0.0f;
          offsetKmFromCenter(planes_old[old_idx].lat, planes_old[old_idx].lon, &dx_old, &dy_old, &dist_old);
          int x_old = 0, y_old = 0;
          bool rim_old = false;
          if (isInsideOuterRingKm(dist_old)) {
            latLonToScreen(planes_old[old_idx].lat, planes_old[old_idx].lon, &x_old, &y_old);
          } else {
            if (!beyondRingEdgeDotFromLatLon(planes_old[old_idx].lat, planes_old[old_idx].lon, &x_old, &y_old)) {
              continue;
            }
            rim_old = true;
          }
          targets[target_count].plane = &planes_old[old_idx];
          targets[target_count].x = x_old;
          targets[target_count].y = y_old;
          targets[target_count].dist_sq = distSqFromCenter(x_old, y_old);
          targets[target_count].glow_factor = 0.0f;
          targets[target_count].is_rim_dot = rim_old;
          ++target_count;
        }
        // If not found (brand new target), do not draw yet (will be revealed when swept)
      }
    }

    // 2. Process old list for vanished targets
    for (size_t i = 0; i < n_old; ++i) {
      if (target_count >= 128) break;

      bool still_exists = false;
      for (size_t j = 0; j < n_new; ++j) {
        if (strcmp(planes_old[i].callsign, planes_new[j].callsign) == 0) {
          still_exists = true;
          break;
        }
      }

      if (!still_exists) {
        float dx_old = 0.0f, dy_old = 0.0f, dist_old = 0.0f;
        offsetKmFromCenter(planes_old[i].lat, planes_old[i].lon, &dx_old, &dy_old, &dist_old);
        int x_old = 0, y_old = 0;
        bool rim_old = false;
        if (isInsideOuterRingKm(dist_old)) {
          latLonToScreen(planes_old[i].lat, planes_old[i].lon, &x_old, &y_old);
        } else {
          if (!beyondRingEdgeDotFromLatLon(planes_old[i].lat, planes_old[i].lon, &x_old, &y_old)) {
            continue;
          }
          rim_old = true;
        }
        
        int d_sq_old = distSqFromCenter(x_old, y_old);
        int r_old = static_cast<int>(lroundf(sqrtf(d_sq_old)));

        if (s_pulse_radius < r_old) {
          // Pulse hasn't reached it yet: keep drawing at old position
          targets[target_count].plane = &planes_old[i];
          targets[target_count].x = x_old;
          targets[target_count].y = y_old;
          targets[target_count].dist_sq = d_sq_old;
          targets[target_count].glow_factor = 0.0f;
          targets[target_count].is_rim_dot = rim_old;
          ++target_count;
        }
        // If swept, it's ignored (erased).
      }
    }
  }

  // --- DRAWING ---
  
  // Sort targets: far targets first so close targets and their labels draw on top
  for (size_t i = 1; i < target_count; ++i) {
    const RadarTarget key = targets[i];
    size_t j = i;
    while (j > 0 && targets[j - 1].dist_sq < key.dist_sq) {
      targets[j] = targets[j - 1];
      --j;
    }
    targets[j] = key;
  }

  // Phase 1: Draw symbols & speed vectors
  for (size_t i = 0; i < target_count; ++i) {
    const RadarTarget& t = targets[i];
    if (t.is_rim_dot) {
      if (t.glow_factor > 0.0f) {
        uint16_t col = mixGlowColor(radar::kColorAircraft, t.glow_factor);
        s_draw->fillSmoothCircle(t.x, t.y, radar::kBeyondRingDotRadiusPx, col);
      } else {
        s_draw->fillSmoothCircle(t.x, t.y, radar::kBeyondRingDotRadiusPx, radar::kColorAircraft);
      }
    } else {
      uint16_t col_track = mixGlowColor(radar::kColorTrackVector, t.glow_factor);
      uint16_t col_ac = mixGlowColor(radar::kColorAircraft, t.glow_factor);
      
      drawSpeedVector(t.x, t.y, t.plane->nose_deg, t.plane->track_deg,
                      t.plane->gs_knots, col_track);
      drawHeadingTriangle(t.x, t.y, t.plane->nose_deg, col_ac);
    }
  }

  // Phase 2: Draw tags on top
  for (size_t i = 0; i < target_count; ++i) {
    const RadarTarget& t = targets[i];
    if (!t.is_rim_dot) {
      initTagLabelMetrics();
      applyTagStyleToTft();

      const int line_h = s_draw->fontHeight();
      const int block_w = measureTagBlockWidth(*t.plane);
      const int block_h = line_h * 3;
      int ly = t.y - block_h / 2;

      const int symbol_half = radar::kAircraftNoseLenPx + radar::kAircraftTailHalfPx;
      const bool tag_on_right = t.x < radar::kCenterX;
      int anchor_x = 0;
      if (tag_on_right) {
        anchor_x = t.x + symbol_half + radar::kAircraftLabelGapPx;
        anchor_x = std::min(anchor_x, radar::kSize - block_w - 1);
        s_draw->setTextDatum(textdatum_t::top_left);
      } else {
        anchor_x = t.x - symbol_half - radar::kAircraftLabelGapPx;
        anchor_x = std::max(anchor_x, block_w + 1);
        s_draw->setTextDatum(textdatum_t::top_right);
      }
      ly = std::max(1, std::min(ly, radar::kSize - block_h - 1));

      uint16_t color_label = mixGlowColor(radar::kColorLabel, t.glow_factor);
      uint16_t color_type = mixGlowColor(radar::kColorTagType, t.glow_factor);
      uint16_t color_alt = mixGlowColor(radar::kColorTagAltitude, t.glow_factor);

      if (t.plane->callsign[0] != '\0') {
        s_draw->setTextColor(color_label, radar::kColorBackground);
        s_draw->drawString(t.plane->callsign, anchor_x, ly);
      }
      ly += line_h;

      if (t.plane->type[0] != '\0') {
        s_draw->setTextColor(color_type, radar::kColorBackground);
        s_draw->drawString(t.plane->type, anchor_x, ly);
      }
      ly += line_h;

      if (t.plane->alt[0] != '\0') {
        s_draw->setTextColor(color_alt, radar::kColorBackground);
        s_draw->drawString(t.plane->alt, anchor_x, ly);
      }
    }
  }
}

void applyCardinalStyle() {
  if (s_cardinal_use_vlw) {
    displayFontSetSmoothSize(*s_draw, s_cardinal_vlw_size);
  } else {
    displayFontSetBitmap(*s_draw, s_cardinal_gfx);
  }
}

void applyScaleStyle() {
  if (s_scale_use_vlw) {
    displayFontSetSmoothSize(*s_draw, s_scale_vlw_size);
  } else {
    displayFontSetBitmap(*s_draw, s_scale_gfx);
  }
}

void drawCardinalLabel(const char* text, int x, int y, textdatum_t datum) {
  applyCardinalStyle();
  s_draw->setTextDatum(datum);
  s_draw->setTextColor(radar::kColorLabel, radar::kColorBackground);
  s_draw->drawString(text, x, y);
}

void drawScaleLabelWithBackground(const char* text, int x, int y) {
  applyScaleStyle();
  s_draw->setTextDatum(textdatum_t::middle_right);

  const int tw = s_draw->textWidth(text);
  const int th = s_draw->fontHeight();
  constexpr int kPadX = 3;
  constexpr int kPadY = 2;

  const int left = x - tw - kPadX;
  const int top = y - th / 2 - kPadY;

  s_draw->fillRect(left, top, tw + kPadX * 2, th + kPadY * 2,
                   radar::kColorBackground);
  s_draw->setTextColor(radar::kColorGrid, radar::kColorBackground);
  s_draw->drawString(text, x, y);
}

void drawGridRing(int cx, int cy, int r, uint16_t color) {
  if (r <= 0) {
    return;
  }
  const int thickness =
      std::max(1, static_cast<int>(radar::kGridStrokeHalfWidth * 2.0f));
  for (int i = 0; i < thickness && r - i > 0; ++i) {
    s_draw->drawCircle(cx, cy, r - i, color);
  }
}

void drawRings(int cx, int cy, int outer_radius) {
  for (int i = 1; i <= radar::kRingCount; ++i) {
    const int r = (outer_radius * i) / radar::kRingCount;
    drawGridRing(cx, cy, r, radar::kColorGrid);
  }
}

void drawCrosshairs(int cx, int cy, int radius, uint16_t color) {
  s_draw->drawWideLine(cx, cy - radius, cx, cy + radius,
                       radar::kGridStrokeHalfWidth, color);
  s_draw->drawWideLine(cx - radius, cy, cx + radius, cy,
                       radar::kGridStrokeHalfWidth, color);
}

void drawCenterDot(int cx, int cy) {
  s_draw->fillSmoothCircle(cx, cy, radar::kCenterDotRadius, radar::kColorCenter);
}

void drawCardinalLabels() {
  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int edge = radar::kSize - 1;

  drawCardinalLabel("N", cx, radar::kCardinalNorthOffsetY, textdatum_t::top_center);
  drawCardinalLabel("S", cx, edge + radar::kCardinalSouthOffsetY,
                    textdatum_t::bottom_center);
  drawCardinalLabel("W", 0, cy, textdatum_t::middle_left);
  drawCardinalLabel("E", edge, cy, textdatum_t::middle_right);
}

int scaleLabelAnchorX(int cx, int outer_radius) {
  return cx + outer_radius - radar::kScaleGapFromOuterRing;
}

void drawScaleLabel(int cx, int cy, int outer_radius) {
  char scale_label[12];
  radar::formatCurrentRing3Label(scale_label, sizeof(scale_label));
  drawScaleLabelWithBackground(scale_label,
                               scaleLabelAnchorX(cx, outer_radius), cy);
}

template <typename Gfx>
void drawStaticGrid(Gfx& gfx) {
  initLabelMetrics();
  const DrawScope scope(gfx);
  displayFontEnsureLoaded(gfx);
  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int grid_r = radar::kGridOuterRadius;

  gfx.fillScreen(radar::kColorBackground);
  drawRings(cx, cy, grid_r);
  drawCrosshairs(cx, cy, grid_r, radar::kColorGrid);
  drawCenterDot(cx, cy);
  drawCardinalLabels();
  drawScaleLabel(cx, cy, grid_r);
  gfx.setTextDatum(textdatum_t::top_left);
}

void blitBackgroundAndAircraft() {
  if (s_bg_ready) {
    DrawScope scope(s_bg); // Redirect all drawing calls to s_bg

    // 1. Redraw static grid onto s_bg to clear previous frame
    drawStaticGrid(s_bg);

    // 2. Draw the pulse ring if active
    if (s_pulse_radius >= 0) {
      uint16_t pulse_color = radar::isRetroTheme() ? 
                             s_bg.color565(0, 255, 100) : 
                             s_bg.color565(0, 200, 255);
      s_bg.drawCircle(radar::kCenterX, radar::kCenterY, s_pulse_radius, pulse_color);
      if (s_pulse_radius > 0) {
        s_bg.drawCircle(radar::kCenterX, radar::kCenterY, s_pulse_radius - 1, pulse_color);
      }
    }

    // 3. Draw aircraft onto s_bg
    drawAircraft();

    // 4. Draw center dot onto s_bg
    drawCenterDot(radar::kCenterX, radar::kCenterY);

#ifdef ENABLE_VIRTUAL_DISPLAY
    displayStreamVirtual();
#endif

    // 6. Push the fully composited frame buffer to the physical screen
    tft.startWrite();
    s_bg.pushSprite(0, 0);
    tft.endWrite();
  }
  tft.setTextDatum(textdatum_t::top_left);
}

}  // namespace

void radarDisplayDraw() {
  initPalette();
  initLabelMetrics();
  s_pulse_radius = -1;

  if (s_bg_ready) {
    blitBackgroundAndAircraft();
    return;
  }

  const DrawScope scope(tft);
  initLabelMetrics();
  drawStaticGrid(tft);
  drawAircraft();
  tft.setTextDatum(textdatum_t::top_left);
}

void radarDisplayRefreshAircraft() {
  initPalette();

  if (s_bg_ready) {
    blitBackgroundAndAircraft();
    return;
  }

  radarDisplayDraw();
}

void radarDisplayRefreshRange() {
  initPalette();
  initLabelMetrics();
  radarDisplayDraw();
}

void radarDisplayRefreshWithPulse(int pulse_radius) {
  s_pulse_radius = pulse_radius;
  radarDisplayRefreshAircraft();
}

}  // namespace ui
