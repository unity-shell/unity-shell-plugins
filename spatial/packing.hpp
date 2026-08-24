#pragma once

#include <vector>

#include <wayfire/geometry.hpp>
#include <wayfire/toplevel-view.hpp>

namespace spatial
{
/** A window and the slot rect it should occupy in the spread. */
struct placement_t
{
    wayfire_toplevel_view view;
    wf::geometry_t        target;
};

/*
 * Row-packed preview layout for one workspace cell. Windows keep their relative
 * sizes, small windows get a scale boost, and the row count with the best
 * preview scale wins. Never upscales past MAX_PREVIEW. Returns each view's slot
 * target within @area. The caller animates the slots. @monitor_h is the output
 * height, used for the small-window boost.
 */
std::vector<placement_t> pack_cell(const std::vector<wayfire_toplevel_view>& views,
    wf::geometry_t area, double monitor_h);
}
