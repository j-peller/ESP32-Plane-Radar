#pragma once

namespace ui {

/** Draw the static sonar/radar grid (black disc, green overlay, labels). */
void radarDisplayDraw();

/** Redraw aircraft only (blits cached grid; no full-screen clear). */
void radarDisplayRefreshAircraft();

/** Redraw only the range label (no full-screen clear). Use after rangeNext(). */
void radarDisplayRefreshRange();

/** Redraw background, pulse ring, and aircraft at the given pulse radius. */
void radarDisplayRefreshWithPulse(int pulse_radius);

}  // namespace ui
