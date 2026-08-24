#include "packing.hpp"
#include "config.hpp"
#include "geometry.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace spatial
{
std::vector<placement_t> pack_cell(const std::vector<wayfire_toplevel_view>& views,
    wf::geometry_t area, double monitor_h)
{
    std::vector<placement_t> out;
    const int n = (int) views.size();
    if (n == 0) { return out; }

    /* Cap so a preview never upscales past ~the cell. */
    constexpr double MAX_PREVIEW = 0.95;
    /* Small windows read as postage stamps, so scale them up a touch: a
     * zero-height window gets SMALL_BOOST_MAX, a full-height one none. */
    constexpr double SMALL_BOOST_MAX = 1.5;
    constexpr double SMALL_BOOST_MIN = 1.0;
    /* Row-count score: preview scale dominates. Cell fill only breaks ties
     * between row counts with near-equal scale. */
    constexpr double SCALE_WEIGHT = 1.0;
    constexpr double SPACE_WEIGHT = 0.1;

    const double spacing = SPACING;
    monitor_h = std::max(1.0, monitor_h);

    struct win_t
    {
        wayfire_toplevel_view view;
        double bw = 0, bh = 0, boost = 0, cx = 0, cy = 0;
    };
    std::vector<win_t> ws;
    ws.reserve(n);
    for (auto& v : views)
    {
        auto vg = v->get_geometry();
        const double bw = std::max(1.0, vg.width), bh = std::max(1.0, vg.height);
        const double ratio = std::clamp(bh / monitor_h, 0.0, 1.0);
        const double boost = geom::lerp(SMALL_BOOST_MAX, SMALL_BOOST_MIN, ratio);
        ws.push_back({v, bw, bh, boost, vg.x + bw / 2.0, vg.y + bh / 2.0});
    }

    std::sort(ws.begin(), ws.end(),
        [] (const win_t& a, const win_t& b) { return a.cy < b.cy; });

    double total_w = 0;
    for (const auto& w : ws) { total_w += w.bw * w.boost; }

    struct row_t { int start, count; double width, height; };
    std::vector<row_t> best;
    double best_scale = 0, best_score = -1;

    for (int num_rows = 1; num_rows <= n; num_rows++)
    {
        const double ideal = total_w / num_rows;
        std::vector<row_t> rows;
        int idx = 0;
        for (int r = 0; (r < num_rows) && (idx < n); r++)
        {
            row_t row{idx, 0, 0, 0};
            for (; idx < n; idx++)
            {
                const double w = ws[idx].bw * ws[idx].boost;
                const double h = ws[idx].bh * ws[idx].boost;
                bool keep;
                if (row.width + w <= ideal) { keep = true; }
                else
                {
                    const double old_r = row.width / ideal;
                    const double new_r = (row.width + w) / ideal;
                    keep = std::abs(1 - new_r) < std::abs(1 - old_r);
                }

                if (keep || (r == num_rows - 1))
                {
                    row.count++;
                    row.width += w;
                    row.height = std::max(row.height, h);
                } else { break; }
            }

            rows.push_back(row);
        }

        double grid_w = 0, grid_h = 0;
        int max_cols = 0;
        for (const auto& row : rows)
        {
            grid_w  = std::max(grid_w, row.width);
            grid_h += row.height;
            max_cols = std::max(max_cols, row.count);
        }

        const double hspace = (max_cols - 1) * spacing;
        const double vspace = ((int) rows.size() - 1) * spacing;
        const double scale  = std::min({std::max(1.0, area.width - hspace) / grid_w,
            std::max(1.0, area.height - vspace) / grid_h, MAX_PREVIEW});
        const double used_w = grid_w * scale + hspace;
        const double used_h = grid_h * scale + vspace;
        const double space  = (used_w * used_h) / (area.width * area.height);
        const double score  = scale * SCALE_WEIGHT + space * SPACE_WEIGHT;

        if (score > best_score) { best = rows; best_scale = scale; best_score = score; }
    }

    double grid_h = 0;
    for (const auto& row : best) { grid_h += row.height; }

    const double total_h = grid_h * best_scale + ((int) best.size() - 1) * spacing;
    double row_y = area.y + std::max(0.0, (area.height - total_h) / 2.0);

    for (const auto& row : best)
    {
        const double row_h = row.height * best_scale;
        const double row_w = row.width * best_scale + (row.count - 1) * spacing;
        double x = area.x + std::max(0.0, (area.width - row_w) / 2.0);

        std::vector<int> order;
        for (int i = row.start; i < row.start + row.count; i++) { order.push_back(i); }
        std::sort(order.begin(), order.end(),
            [&] (int a, int b) { return ws[a].cx < ws[b].cx; });

        for (int i : order)
        {
            auto& w = ws[i];
            const double final_scale = std::min(best_scale * w.boost, MAX_PREVIEW);
            const double cell_w  = w.bw * w.boost * best_scale;
            const double clone_w = w.bw * final_scale;
            const double clone_h = w.bh * final_scale;
            const double clone_x = x + (cell_w - clone_w) / 2.0;
            const double clone_y = (best.size() == 1)
                ? row_y + (row_h - clone_h) / 2.0
                : row_y + row_h - clone_h;

            out.push_back({w.view, {clone_x, clone_y, clone_w, clone_h}});

            x += cell_w + spacing;
        }

        row_y += row_h + spacing;
    }

    return out;
}
}
