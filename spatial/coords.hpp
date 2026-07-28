#pragma once

#include <algorithm>
#include <cmath>

#include <wayfire/geometry.hpp>

#include "config.hpp"

namespace wf { class output_t; }

namespace spatial
{
/**
 * Snapshot of output/workspace geometry used while placing spread content.
 */
struct frame_ctx
{
    wf::point_t      cur_ws{0, 0};
    wf::dimensions_t grid{1, 1};
    wf::geometry_t   workarea{0, 0, 1, 1};
    wf::dimensions_t output{1, 1};
    wf::pointf_t     cursor{0, 0};
};

/**
 * Builds a frame context from the current output/workspace state.
 */
frame_ctx make_frame_ctx(wf::output_t *output);

namespace coords
{
/**
 * Linear interpolation helper for scalar values.
 */
inline double lerp(double a, double b, double t) { return a + (b - a) * t; }

/**
 * Linear interpolation helper for geometry rectangles.
 */
inline wf::geometry_t lerp_rect(wf::geometry_t a, wf::geometry_t b, double t)
{
    return {lerp(a.x, b.x, t), lerp(a.y, b.y, t),
        lerp(a.width, b.width, t), lerp(a.height, b.height, t)};
}

/* The wall's inter-cell gutter: twice the base gap (see WALL_GAP). */
inline constexpr int wall_gutter() { return 2 * WALL_GAP; }

/* The apps-spread card: the workarea inset by a uniform WALL_GAP on every side,
 * so the margin is exactly WALL_GAP regardless of screen size / aspect (the
 * wallpaper is fit to this rect rather than letterboxed). */
inline wf::geometry_t card_rect(const frame_ctx& c)
{
    return {c.workarea.x + WALL_GAP, c.workarea.y + WALL_GAP,
        c.workarea.width - 2 * WALL_GAP, c.workarea.height - 2 * WALL_GAP};
}

inline wf::geometry_t cell_rect(const frame_ctx& c, int i, int j)
{
    const int gap = wall_gutter();
    const double s = std::min(
        (c.workarea.width  - (c.grid.width  + 1) * gap) /
        std::max(1, c.grid.width  * c.output.width),
        (c.workarea.height - (c.grid.height + 1) * gap) /
        std::max(1, c.grid.height * c.output.height));
    const double cw = c.output.width * s, ch = c.output.height * s;
    const double gw = c.grid.width * cw + (c.grid.width + 1) * gap;
    const double gh = c.grid.height * ch + (c.grid.height + 1) * gap;
    const double ox = c.workarea.x + (c.workarea.width - gw) / 2 + gap;
    const double oy = c.workarea.y + (c.workarea.height - gh) / 2 + gap;
    return {ox + i * (cw + gap), oy + j * (ch + gap), cw, ch};
}

inline wf::point_t cell_at(const frame_ctx& c, wf::pointf_t p)
{
    const int gap = wall_gutter();
    auto r0 = cell_rect(c, 0, 0);
    const int i = (int) std::floor((p.x - r0.x) / (r0.width + gap));
    const int j = (int) std::floor((p.y - r0.y) / (r0.height + gap));
    return {std::clamp(i, 0, c.grid.width - 1), std::clamp(j, 0, c.grid.height - 1)};
}

/**
 * On-screen rect of grid cell (i, j) at the axis value g in [0, 2]. The single
 * zoom shared by the windows and the backdrop cards, so they can never diverge:
 *   g in (0, 1]  the current cell grows from the full output to the spread card;
 *   g in (1, 2]  the whole grid zooms out from that card to the full wall.
 *
 * The card (workarea aspect, uniform 12px margins) and the wall cells (output
 * aspect, 24px gutter) differ in shape, so the zoom uses independent x/y scales
 * to map one to the other -- the current cell is exactly the card at g==1 and
 * exactly its wall rect at g==2, squishing subtly in between.
 */
inline wf::geometry_t cell_on_screen(const frame_ctx& c, int i, int j, double g)
{
    const double ep = std::clamp(g, 0.0, 1.0);         /* expose / card-grow phase */
    const double op = std::clamp(g - 1.0, 0.0, 1.0);   /* zoom-out-to-wall phase */

    wf::geometry_t full = wf::construct_box({0, 0}, c.output);
    wf::geometry_t card = lerp_rect(full, card_rect(c), ep);

    auto cc = cell_rect(c, c.cur_ws.x, c.cur_ws.y);
    const double sx = lerp(card.width  / std::max(1.0, cc.width),  1.0, op);
    const double sy = lerp(card.height / std::max(1.0, cc.height), 1.0, op);
    const double tx = lerp(card.x - cc.x * (card.width  / std::max(1.0, cc.width)),  0.0, op);
    const double ty = lerp(card.y - cc.y * (card.height / std::max(1.0, cc.height)), 0.0, op);

    auto gc = cell_rect(c, i, j);
    return {gc.x * sx + tx, gc.y * sy + ty, gc.width * sx, gc.height * sy};
}

/**
 * Cell rect during a 4-finger slide: fixed-size panes (the current cell's size
 * at g) spaced by a constant gap and panned by @amount toward @dir. Keeps the
 * inter-workspace gap constant, unlike the grid zoom which scales it.
 */
inline wf::geometry_t pane_on_screen(const frame_ctx& c, int i, int j, double g,
    wf::point_t dir, double amount)
{
    const int gap = wall_gutter();
    auto base = cell_on_screen(c, c.cur_ws.x, c.cur_ws.y, g);
    const double sx = base.width + gap, sy = base.height + gap;
    const double panx = -dir.x * amount * sx, pany = -dir.y * amount * sy;
    return {base.x + (i - c.cur_ws.x) * sx + panx,
        base.y + (j - c.cur_ws.y) * sy + pany, base.width, base.height};
}

/**
 * On-screen rect of cell (i, j): the panning pane layout during a 4-finger
 * slide, or the plain grid zoom otherwise.
 */
inline wf::geometry_t cell_or_pane(const frame_ctx& c, int i, int j, double g,
    wf::point_t pan_dir, double pan_amount)
{
    const bool sliding = pan_dir.x || pan_dir.y || (pan_amount != 0.0);
    return sliding ? pane_on_screen(c, i, j, g, pan_dir, pan_amount)
                   : cell_on_screen(c, i, j, g);
}
}
}
