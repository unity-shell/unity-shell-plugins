#pragma once

#include <algorithm>
#include <cmath>

#include <wayfire/geometry.hpp>

namespace wf { class output_t; }

namespace spatial
{
struct frame_ctx
{
    wf::point_t      cur_ws{0, 0};
    wf::dimensions_t grid{1, 1};
    wf::geometry_t   workarea{0, 0, 1, 1};
    wf::dimensions_t output{1, 1};
    wf::pointf_t     cursor{0, 0};
};

frame_ctx make_frame_ctx(wf::output_t *output);

namespace coords
{
inline double lerp(double a, double b, double t) { return a + (b - a) * t; }

inline wf::geometry_t lerp_rect(wf::geometry_t a, wf::geometry_t b, double t)
{
    return {(int) lerp(a.x, b.x, t), (int) lerp(a.y, b.y, t),
        (int) lerp(a.width, b.width, t), (int) lerp(a.height, b.height, t)};
}

inline wf::geometry_t card_rect(const frame_ctx& c, int gap)
{
    const double s = std::min(
        (double) (c.workarea.width  - 2 * gap) / std::max(1, c.output.width),
        (double) (c.workarea.height - 2 * gap) / std::max(1, c.output.height));
    const int cw = (int) (c.output.width * s), ch = (int) (c.output.height * s);
    return {c.workarea.x + (c.workarea.width - cw) / 2,
        c.workarea.y + (c.workarea.height - ch) / 2, cw, ch};
}

inline wf::geometry_t cell_rect(const frame_ctx& c, int i, int j, int gap)
{
    const double s = std::min(
        (double) (c.workarea.width  - (c.grid.width  + 1) * gap) /
        std::max(1, c.grid.width  * c.output.width),
        (double) (c.workarea.height - (c.grid.height + 1) * gap) /
        std::max(1, c.grid.height * c.output.height));
    const int cw = (int) (c.output.width * s), ch = (int) (c.output.height * s);
    const int gw = c.grid.width * cw + (c.grid.width + 1) * gap;
    const int gh = c.grid.height * ch + (c.grid.height + 1) * gap;
    const int ox = c.workarea.x + (c.workarea.width - gw) / 2 + gap;
    const int oy = c.workarea.y + (c.workarea.height - gh) / 2 + gap;
    return {ox + i * (cw + gap), oy + j * (ch + gap), cw, ch};
}

inline wf::point_t cell_at(const frame_ctx& c, wf::pointf_t p, int gap)
{
    auto r0 = cell_rect(c, 0, 0, gap);
    const int i = (int) std::floor((p.x - r0.x) / (double) (r0.width + gap));
    const int j = (int) std::floor((p.y - r0.y) / (double) (r0.height + gap));
    return {std::clamp(i, 0, c.grid.width - 1), std::clamp(j, 0, c.grid.height - 1)};
}

inline wf::geometry_t cell_on_screen(const frame_ctx& c, int i, int j, double g, int gap)
{
    const double ep = std::clamp(g, 0.0, 1.0);
    const double op = std::clamp(g - 1.0, 0.0, 1.0);

    wf::geometry_t full{0, 0, c.output.width, c.output.height};
    wf::geometry_t card = lerp_rect(full, card_rect(c, gap), ep);

    auto cc = cell_rect(c, c.cur_ws.x, c.cur_ws.y, gap);
    const double s0 = (double) card.width / std::max(1, cc.width);
    const double s  = lerp(s0, 1.0, op);
    const double tx = (card.x - cc.x * s0) * (1.0 - op);
    const double ty = (card.y - cc.y * s0) * (1.0 - op);

    auto gc = cell_rect(c, i, j, gap);
    return {(int) std::lround(gc.x * s + tx), (int) std::lround(gc.y * s + ty),
        (int) std::lround(gc.width * s), (int) std::lround(gc.height * s)};
}

inline wf::geometry_t pane_on_screen(const frame_ctx& c, int i, int j, double g, int gap,
    wf::point_t dir, double amount)
{
    auto base = cell_on_screen(c, c.cur_ws.x, c.cur_ws.y, g, gap);
    const int sx = base.width + gap, sy = base.height + gap;
    const double panx = -dir.x * amount * sx, pany = -dir.y * amount * sy;
    return {(int) std::lround(base.x + (i - c.cur_ws.x) * sx + panx),
        (int) std::lround(base.y + (j - c.cur_ws.y) * sy + pany), base.width, base.height};
}
}
}
