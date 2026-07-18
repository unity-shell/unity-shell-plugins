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
inline constexpr float DIM_INACTIVE = 0.6f;

/**
 * Returns the configured wall gap tint for workspace spread rendering.
 */
inline wf::color_t wall_gap_color()
{
    static wf::option_wrapper_t<wf::color_t> opt{"spatial/gap-color"};
    return opt;
}
}
