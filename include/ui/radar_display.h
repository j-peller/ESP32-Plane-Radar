#pragma once

namespace ui {

/** Draw the static sonar/radar grid (black disc, green overlay, labels). */
void radarDisplayDraw();

/** Redraw aircraft only (blits cached grid; no full-screen clear). */
void radarDisplayRefreshAircraft();

/** Redraw only the range label (no full-screen clear). Use after rangeNext(). */
void radarDisplayRefreshRange();

/** Redraw background, sweep line, and aircraft at the given sweep angle. */
void radarDisplayRefreshWithSweep(float sweep_angle);

}  // namespace ui
