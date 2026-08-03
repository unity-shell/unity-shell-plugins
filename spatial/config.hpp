#pragma once

#include <wayfire/config/types.hpp>
#include <wayfire/option-wrapper.hpp>

namespace spatial
{
/**
 * Shared plugin identifier and motion/layout tuning constants.
 */
inline constexpr const char *PLUGIN_NAME = "spatial";

inline constexpr double SWIPE_DISTANCE = 300.0;
inline constexpr double FLING_VELOCITY = 0.6;
inline constexpr double STAGE_HYSTERESIS = 0.15;

/* Minimum pinch scale change to accept as a deliberate pinch. Kept high so the
 * incidental finger drift libinput may report as a pinch during a 3-finger
 * swipe does not trip the workspaces-spread toggle. */
inline constexpr double PINCH_THRESHOLD = 0.25;

inline constexpr int SPACING = 20;
inline constexpr int OUTER_MARGIN = 36;
inline constexpr int WALL_GAP = 12;
inline constexpr int CARD_CORNER_RADIUS = 9;
/* Focus ring on the current workspace card, set off from its edge by a gap. */
inline constexpr float FOCUS_RING_GAP   = 4.0f;
inline constexpr float FOCUS_RING_WIDTH = 3.0f;

/**
 * Backdrop color filling the gaps between and around the workspace cells.
 */
inline wf::color_t backdrop_color()
{
    static wf::option_wrapper_t<wf::color_t> opt{"spatial/backdrop-color"};
    return opt;
}

/**
 * Highlight color for the current workspace cell's ring (default white; the
 * shell can map the desktop accent onto spatial/active-cell-color).
 */
inline wf::color_t active_cell_color()
{
    static wf::option_wrapper_t<wf::color_t> opt{"spatial/active-cell-color"};
    return opt;
}
}
