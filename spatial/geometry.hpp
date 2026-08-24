#pragma once

#include <algorithm>
#include <cmath>

#include <wayfire/geometry.hpp>

#include "config.hpp"

namespace wf { class output_t; }

namespace spatial
{
/* Per-frame layout inputs. */
struct world
{
    wf::point_t      cur_ws{0, 0};
    wf::dimensions_t grid{1, 1};
    wf::geometry_t   workarea{0, 0, 1, 1};
    wf::dimensions_t output{1, 1};
    wf::pointf_t     cursor{0, 0};
};

world make_world(wf::output_t *output);

/* Reserve edge pixels for shell surfaces in the spread's layout rect only, not
 * the work area, so no window is resized. Set by the shell over IPC. */
void set_inset(int left, int right, int top, int bottom);

namespace geom
{
inline double lerp(double from, double to, double t) { return from + (to - from) * t; }

inline wf::geometry_t lerp_rect(wf::geometry_t from, wf::geometry_t to, double t)
{
    return {lerp(from.x, to.x, t), lerp(from.y, to.y, t),
        lerp(from.width, to.width, t), lerp(from.height, to.height, t)};
}

/* 2x the base gap. */
inline constexpr int wall_gutter() { return 2 * WALL_GAP; }

/* Workarea inset by WALL_GAP on every side (uniform at any size or aspect). */
inline wf::geometry_t card_rect(const world& ctx)
{
    return {ctx.workarea.x + WALL_GAP, ctx.workarea.y + WALL_GAP,
        ctx.workarea.width - 2 * WALL_GAP, ctx.workarea.height - 2 * WALL_GAP};
}

/* Wall rect of a cell: the grid scaled to fit the workarea. */
inline wf::geometry_t cell_rect(const world& ctx, int col, int row)
{
    const int gutter = wall_gutter();
    const double scale = std::min(
        (ctx.workarea.width  - (ctx.grid.width  + 1) * gutter) /
        std::max(1, ctx.grid.width  * ctx.output.width),
        (ctx.workarea.height - (ctx.grid.height + 1) * gutter) /
        std::max(1, ctx.grid.height * ctx.output.height));
    const double cell_w = ctx.output.width * scale, cell_h = ctx.output.height * scale;
    const double grid_w = ctx.grid.width * cell_w + (ctx.grid.width + 1) * gutter;
    const double grid_h = ctx.grid.height * cell_h + (ctx.grid.height + 1) * gutter;
    const double origin_x = ctx.workarea.x + (ctx.workarea.width - grid_w) / 2 + gutter;
    const double origin_y = ctx.workarea.y + (ctx.workarea.height - grid_h) / 2 + gutter;
    return {origin_x + col * (cell_w + gutter), origin_y + row * (cell_h + gutter), cell_w, cell_h};
}

/* Clamped, so a click in a gutter maps to the nearest cell. */
inline wf::point_t cell_at(const world& ctx, wf::pointf_t at)
{
    const int gutter = wall_gutter();
    auto origin = cell_rect(ctx, 0, 0);
    const int col = (int) std::floor((at.x - origin.x) / (origin.width + gutter));
    const int row = (int) std::floor((at.y - origin.y) / (origin.height + gutter));
    return {std::clamp(col, 0, ctx.grid.width - 1), std::clamp(row, 0, ctx.grid.height - 1)};
}

/*
 * Shared zoom for windows and cards. g in (0,1] grows the current cell to the
 * spread card. g in (1,2] zooms out to the full wall. The card and the cell
 * differ in aspect, so separate x/y scales morph one to the other.
 */
inline wf::geometry_t cell_on_screen(const world& ctx, int col, int row, double g)
{
    const double expose   = std::clamp(g, 0.0, 1.0);
    const double zoom_out = std::clamp(g - 1.0, 0.0, 1.0);

    wf::geometry_t full_output = wf::construct_box({0, 0}, ctx.output);
    wf::geometry_t card = lerp_rect(full_output, card_rect(ctx), expose);

    auto cur_cell = cell_rect(ctx, ctx.cur_ws.x, ctx.cur_ws.y);
    const double ratio_w = card.width  / std::max(1.0, cur_cell.width);
    const double ratio_h = card.height / std::max(1.0, cur_cell.height);
    const double scale_x = lerp(ratio_w, 1.0, zoom_out);
    const double scale_y = lerp(ratio_h, 1.0, zoom_out);
    const double trans_x = lerp(card.x - cur_cell.x * ratio_w, 0.0, zoom_out);
    const double trans_y = lerp(card.y - cur_cell.y * ratio_h, 0.0, zoom_out);

    auto cell = cell_rect(ctx, col, row);
    return {cell.x * scale_x + trans_x, cell.y * scale_y + trans_y,
        cell.width * scale_x, cell.height * scale_y};
}

/* Slide layout: fixed panes at a constant gap, panned by @amount. Unlike the
 * zoom, the gap does not scale. */
inline wf::geometry_t pane_on_screen(const world& ctx, int col, int row, double g,
    wf::point_t dir, double amount)
{
    const int gutter = wall_gutter();
    auto base = cell_on_screen(ctx, ctx.cur_ws.x, ctx.cur_ws.y, g);
    const double step_x = base.width + gutter, step_y = base.height + gutter;
    const double pan_x = -dir.x * amount * step_x, pan_y = -dir.y * amount * step_y;
    return {base.x + (col - ctx.cur_ws.x) * step_x + pan_x,
        base.y + (row - ctx.cur_ws.y) * step_y + pan_y, base.width, base.height};
}

/* Pane layout while sliding, else the grid zoom. */
inline wf::geometry_t cell_or_pane(const world& ctx, int col, int row, double g,
    wf::point_t pan_dir, double pan_amount)
{
    const bool sliding = pan_dir.x || pan_dir.y || (pan_amount != 0.0);
    return sliding ? pane_on_screen(ctx, col, row, g, pan_dir, pan_amount)
                   : cell_on_screen(ctx, col, row, g);
}
}
}
