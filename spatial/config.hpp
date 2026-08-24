#pragma once

#include <wayfire/config/types.hpp>
#include <wayfire/option-wrapper.hpp>

namespace spatial
{
inline constexpr const char *PLUGIN_NAME = "spatial";

inline constexpr double SWIPE_DISTANCE = 300.0;
inline constexpr double FLING_VELOCITY = 0.6;
inline constexpr double STAGE_HYSTERESIS = 0.15;

/* High so finger drift during a 3-finger swipe does not toggle the wall. */
inline constexpr double PINCH_THRESHOLD = 0.25;

inline constexpr int SPACING = 20;
inline constexpr int OUTER_MARGIN = 36;
inline constexpr int WALL_GAP = 12;
inline constexpr int CARD_CORNER_RADIUS = 9;
inline constexpr float FOCUS_RING_GAP   = 4.0f;
inline constexpr float FOCUS_RING_WIDTH = 3.0f;

inline wf::color_t backdrop_color()
{
    static wf::option_wrapper_t<wf::color_t> opt{"spatial/backdrop-color"};
    return opt;
}

/* Shell can point this at the desktop accent. */
inline wf::color_t active_cell_color()
{
    static wf::option_wrapper_t<wf::color_t> opt{"spatial/active-cell-color"};
    return opt;
}
}
