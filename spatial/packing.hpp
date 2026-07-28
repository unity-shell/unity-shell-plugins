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

/**
 * Row-packed preview layout for one workspace cell: windows keep their relative
 * sizes, small windows get a gentle scale boost, and the row count that
 * maximises preview scale (then space) wins. Never upscales past MAX_PREVIEW.
 *
 * Pure geometry -- returns each view's slot target within @area; the caller
 * animates the slots. @monitor_h is the output height (for the small-window
 * boost). Views are read top-to-bottom / left-to-right by their real position.
 */
std::vector<placement_t> pack_cell(const std::vector<wayfire_toplevel_view>& views,
    wf::geometry_t area, double monitor_h);
}
