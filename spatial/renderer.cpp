#include "renderer.hpp"
#include "config.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

#include <wayfire/core.hpp>
#include <wayfire/render.hpp>
#include <wayfire/workarea.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/scene.hpp>
#include <wayfire/scene-render.hpp>
#include <wayfire/scene-operations.hpp>
#include <wayfire/view-helpers.hpp>
#include <wayfire/config/types.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/plugins/common/geometry-animation.hpp>

namespace spatial
{
/* Scene stream node that captures background/bottom layers into a texture. */
class wallpaper_stream_t : public wf::scene::node_t
{
    class instance_t : public wf::scene::render_instance_t
    {
        wallpaper_stream_t *self;
        std::vector<wf::scene::render_instance_uptr> children;

      public:
        instance_t(wallpaper_stream_t *node, const wf::scene::damage_callback& push) :
            self(node)
        {
            for (auto& view : wf::collect_views_from_output(self->output,
                {wf::scene::layer::BACKGROUND, wf::scene::layer::BOTTOM}))
            {
                view->get_transformed_node()->gen_render_instances(children, push, self->output);
            }
        }

        void schedule_instructions(std::vector<wf::scene::render_instruction_t>& instructions,
            const wf::render_target_t& target, wf::region_t& damage) override
        {
            auto bbox = self->get_bounding_box();
            auto ours = damage & bbox;
            if (ours.empty()) { return; }

            for (auto& c : children) { c->schedule_instructions(instructions, target, ours); }
            damage ^= bbox;
            instructions.push_back({.instance = this, .target = target, .damage = ours});
        }

        void render(const wf::scene::render_instruction_t& data) override
        {
            static wf::option_wrapper_t<wf::color_t> bg{"core/background_color"};
            data.pass->clear(data.damage, bg);
        }

        void presentation_feedback(wf::output_t *o) override
        {
            for (auto& c : children) { c->presentation_feedback(o); }
        }

        void compute_visibility(wf::output_t *o, wf::region_t& visible) override
        {
            wf::scene::compute_visibility_from_list(children, o, visible, {0, 0});
        }
    };

  public:
    wf::output_t *output;

    explicit wallpaper_stream_t(wf::output_t *o) : node_t(false), output(o) {}

    wf::geometry_t get_bounding_box() override { return output->get_relative_geometry(); }

    void gen_render_instances(std::vector<wf::scene::render_instance_uptr>& instances,
        wf::scene::damage_callback push, wf::output_t *) override
    {
        instances.push_back(std::make_unique<instance_t>(this, push));
    }

    std::string stringify() const override { return "spatial-wallpaper"; }
};

class backdrop_node_t : public wf::scene::node_t
{
    class instance_t : public wf::scene::render_instance_t
    {
        std::shared_ptr<backdrop_node_t> self;
        std::vector<wf::scene::render_instance_uptr> bg;
        wf::scene::damage_callback push;
        wf::signal::connection_t<wf::scene::node_damage_signal> on_damage =
            [=] (wf::scene::node_damage_signal *ev) { push(ev->region); };

      public:
        instance_t(backdrop_node_t *n, const wf::scene::damage_callback& p) : push(p)
        {
            self = std::dynamic_pointer_cast<backdrop_node_t>(n->shared_from_this());
            self->connect(&on_damage);
            auto mark = [=] (const wf::region_t& d)
            {
                self->bg_damage |= d;
                push(self->get_bounding_box());
            };
            self->stream->gen_render_instances(bg, mark, self->output);
        }

        void schedule_instructions(std::vector<wf::scene::render_instruction_t>& instructions,
            const wf::render_target_t& target, wf::region_t& damage) override
        {
            if (!self->bg_damage.empty())
            {
                wf::render_target_t bt{self->buffer};
                bt.geometry = self->stream->get_bounding_box();
                bt.scale    = self->output->handle->scale;

                wf::render_pass_params_t p;
                p.instances        = &bg;
                p.damage           = self->bg_damage;
                p.reference_output = self->output;
                p.target           = bt;
                p.flags            = wf::RPASS_EMIT_SIGNALS;
                wf::render_pass_t::run(p);
                self->bg_damage.clear();
            }

            auto bbox = self->get_bounding_box();
            instructions.push_back({.instance = this, .target = target, .damage = damage & bbox});
            damage ^= bbox;
        }

        void render(const wf::scene::render_instruction_t& data) override
        {
            data.pass->clear(data.damage, wall_gap_color());

            auto ctx = make_frame_ctx(self->output);
            const wf::point_t hl = (self->hover.x >= 0) ? self->hover : ctx.cur_ws;
            /* Dim non-focused cells only as the wall opens (g -> 2); absent on
             * the spread and during a slide, so every pane reads full-bright. */
            const double dim = (1.0 - DIM_INACTIVE) * std::clamp(self->g - 1.0, 0.0, 1.0);
            const bool sliding = self->pan_dir.x || self->pan_dir.y || self->pan_amount != 0;
            const auto bufsz = self->buffer.get_size();

            for (int i = 0; i < ctx.grid.width; i++)
            {
                for (int j = 0; j < ctx.grid.height; j++)
                {
                    auto card = sliding
                        ? coords::pane_on_screen(ctx, i, j, self->g, WALL_GAP,
                            self->pan_dir, self->pan_amount)
                        : coords::cell_on_screen(ctx, i, j, self->g, WALL_GAP);

                    if ((card.x >= ctx.output.width) || (card.y >= ctx.output.height) ||
                        (card.x + card.width <= 0) || (card.y + card.height <= 0))
                    {
                        continue;
                    }

                    auto tex = wf::texture_t{self->buffer.get_texture()};
                    tex.filter_mode = WLR_SCALE_FILTER_BILINEAR;
                    tex.source_box  = {0.0, 0.0, (double) bufsz.width, (double) bufsz.height};
                    data.pass->add_texture(tex, data.target, card, data.damage);

                    if ((dim > 0.0) && ((i != hl.x) || (j != hl.y)))
                    {
                        data.pass->add_rect({0.0, 0.0, 0.0, dim}, data.target, card, data.damage);
                    }
                }
            }
        }

        void compute_visibility(wf::output_t *o, wf::region_t&) override
        {
            wf::region_t r = self->stream->get_bounding_box();
            for (auto& c : bg) { c->compute_visibility(o, r); }
        }
    };

  public:
    wf::output_t *output;
    double g = 0.0;
    wf::point_t  pan_dir{0, 0};
    double       pan_amount = 0.0;
    wf::point_t  hover{-1, -1};
    std::shared_ptr<wallpaper_stream_t> stream;
    wf::auxilliary_buffer_t buffer;
    wf::region_t bg_damage;

    explicit backdrop_node_t(wf::output_t *o) :
        node_t(false), output(o), stream(std::make_shared<wallpaper_stream_t>(o))
    {
        auto bbox = stream->get_bounding_box();
        buffer.allocate(wf::dimensions(bbox), o->handle->scale,
            wf::buffer_allocation_hints_t{.needs_alpha = false});
        bg_damage |= bbox;
    }

    void update(double g_, wf::point_t dir, double amount, wf::point_t hover_)
    {
        g = g_;
        pan_dir = dir;
        pan_amount = amount;
        hover = hover_;
        wf::scene::damage_node(shared_from_this(), get_bounding_box());
    }

    void gen_render_instances(std::vector<wf::scene::render_instance_uptr>& instances,
        wf::scene::damage_callback push, wf::output_t *shown_on) override
    {
        if (shown_on != output) { return; }
        instances.push_back(std::make_unique<instance_t>(this, push));
    }

    wf::geometry_t get_bounding_box() override { return output->get_layout_geometry(); }

    std::string stringify() const override { return "spatial-backdrop"; }
};

spread_t::spread_t(wf::output_t *o) : output(o) {}
spread_t::~spread_t() = default;

void spread_t::ensure_backdrop()
{
    if (backdrop) { return; }
    backdrop = std::make_shared<backdrop_node_t>(output);
    auto root = wf::get_core().scene();
    wf::scene::add_front(root->layers[(size_t) wf::scene::layer::BOTTOM], backdrop);
}

void spread_t::remove_backdrop()
{
    if (!backdrop) { return; }
    wf::scene::remove_child(backdrop);
    backdrop = nullptr;
}

std::shared_ptr<wf::scene::view_2d_transformer_t>
spread_t::ensure_transformer(wayfire_toplevel_view view)
{
    auto node = view->get_transformed_node();
    if (auto e = node->get_transformer<wf::scene::view_2d_transformer_t>(PLUGIN_NAME))
    {
        return e;
    }

    auto tr = std::make_shared<wf::scene::view_2d_transformer_t>(view);
    node->add_transformer(tr, wf::TRANSFORMER_2D + 1, PLUGIN_NAME);
    return tr;
}

void spread_t::ensure_layout(const frame_ctx& ctx, const std::vector<std::string>& filter)
{
    ensure_backdrop();
    if ((ctx.cur_ws.x != laid_out_ws.x) || (ctx.cur_ws.y != laid_out_ws.y))
    {
        layout(ctx, filter);
    }
}

void spread_t::relayout(const frame_ctx& ctx, const std::vector<std::string>& filter)
{
    layout(ctx, filter);
}

void spread_t::layout(const frame_ctx& ctx, const std::vector<std::string>& filter)
{
    ensure_backdrop();
    laid_out_ws = ctx.cur_ws;

    const int ow = ctx.output.width, oh = ctx.output.height;

    /* Partition the workspace views: surface minimized ones, gather the app-id
     * filtered-out ones, and bucket the rest by workspace cell. */
    std::map<std::pair<int, int>, std::vector<wayfire_toplevel_view>> cells;
    std::set<wayfire_toplevel_view> wanted;
    std::vector<wayfire_toplevel_view> want_hidden;

    for (auto& v : output->wset()->get_views(wf::WSET_MAPPED_ONLY))
    {
        if (!filter.empty() &&
            (std::find(filter.begin(), filter.end(), v->get_app_id()) == filter.end()))
        {
            want_hidden.push_back(v);
            continue;
        }

        /* Minimized views stay mapped but have their node disabled; enable it
         * once for the lifetime of the spread (restored in clear()). The enabled
         * state is a shared counter Wayfire also drives for workspace visibility,
         * so toggling it per relayout would drift and strand views hidden. */
        if (v->minimized &&
            std::find(shown_minimized.begin(), shown_minimized.end(), v) == shown_minimized.end())
        {
            wf::scene::set_node_enabled(v->get_root_node(), true);
            shown_minimized.push_back(v);
        }

        /* Bucket by Wayfire's own (grid-clamped) view->workspace mapping, not a
         * raw floor of the geometry centre which can land in a phantom cell. */
        const wf::point_t vws = output->wset()->get_view_main_workspace(v);
        cells[{vws.x - ctx.cur_ws.x, vws.y - ctx.cur_ws.y}].push_back(v);
        wanted.insert(v);
    }

    /* Reconcile the filter's hidden set idempotently (enable un-hidden views,
     * disable newly hidden ones), so node-enable never drifts across relayouts. */
    for (auto& v : hidden_views)
    {
        if (std::find(want_hidden.begin(), want_hidden.end(), v) == want_hidden.end())
        {
            wf::scene::set_node_enabled(v->get_root_node(), true);
        }
    }
    for (auto& v : want_hidden)
    {
        if (std::find(hidden_views.begin(), hidden_views.end(), v) == hidden_views.end())
        {
            wf::scene::set_node_enabled(v->get_root_node(), false);
        }
    }
    hidden_views = std::move(want_hidden);

    /* Drop previews for views that are no longer shown. */
    for (auto it = views.begin(); it != views.end(); )
    {
        if (!wanted.count(it->first)) { detach_family(it->second); it = views.erase(it); }
        else { ++it; }
    }

    /* Aim each shown view's slot at its freshly computed target; persistent
     * views ease from their current slot, so the arrangement never snaps. */
    for (auto& [cell, cell_views] : cells)
    {
        wf::geometry_t area = {
            cell.first * ow + ctx.workarea.x + OUTER_MARGIN,
            cell.second * oh + ctx.workarea.y + OUTER_MARGIN,
            ctx.workarea.width - OUTER_MARGIN * 2,
            ctx.workarea.height - OUTER_MARGIN * 2};
        layout_cell({cell.first, cell.second}, cell_views, area);
    }
}

/* Row-packed preview layout: windows keep their relative sizes, small windows
 * get a gentle scale boost, and the row count that maximises preview scale (then
 * space) wins. Never upscales past MAX_PREVIEW. */
void spread_t::layout_cell(wf::point_t cell,
    std::vector<wayfire_toplevel_view>& cell_views, wf::geometry_t area)
{
    const int n = (int) cell_views.size();
    if (n == 0) { return; }

    constexpr double MAX_PREVIEW = 0.95;
    const double spacing   = SPACING;
    const double monitor_h = std::max(1, output->get_relative_geometry().height);

    struct win_t
    {
        wayfire_toplevel_view view;
        double bw = 0, bh = 0, boost = 0, cx = 0, cy = 0;
    };
    std::vector<win_t> ws;
    ws.reserve(n);
    for (auto& v : cell_views)
    {
        auto vg = v->get_geometry();
        const double bw = std::max(1, vg.width), bh = std::max(1, vg.height);
        const double ratio = std::clamp(bh / monitor_h, 0.0, 1.0);
        ws.push_back({v, bw, bh, 1.5 - 0.5 * ratio, vg.x + bw / 2.0, vg.y + bh / 2.0});
    }

    std::sort(ws.begin(), ws.end(),
        [] (const win_t& a, const win_t& b) { return a.cy < b.cy; });

    double total_w = 0;
    for (const auto& w : ws) { total_w += w.bw * w.boost; }

    struct row_t { int start, count; double width, height; };
    std::vector<row_t> best;
    double best_scale = -1, best_space = 0;

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

        bool better;
        if (best_scale < 0) { better = true; }
        else if ((scale > best_scale) && (space > best_space)) { better = true; }
        else if (scale > best_scale)
        {
            better = (scale - best_scale) * 1.0 > (best_space - space) * 0.1;
        } else if (space > best_space)
        {
            better = (space - best_space) * 0.1 > (best_scale - scale) * 1.0;
        } else { better = false; }

        if (better) { best = rows; best_scale = scale; best_space = space; }
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

            aim_slot(w.view, cell,
                {(int) clone_x, (int) clone_y, (int) clone_w, (int) clone_h});

            x += cell_w + spacing;
        }

        row_y += row_h + spacing;
    }
}

/* Position every window for the current frame in two steps: interpolate its real
 * geometry toward its expose slot by ep=clamp(g,0,1) (the fan-out within its
 * workspace), then map that onto the workspace's on-screen cell at g
 * (cell_on_screen / pane_on_screen) through the view's 2D transformer. */
void spread_t::place(const frame_ctx& ctx, const render_state& state)
{
    const double ep = std::clamp(state.g, 0.0, 1.0);
    const bool sliding = state.pan_dir.x || state.pan_dir.y || state.pan_amount != 0;

    for (auto& [view, d] : views)
    {
        if (!d.slot || d.dragging) { continue; }

        auto pvg = view->get_geometry();
        wf::geometry_t region = {d.cell.x * ctx.output.width, d.cell.y * ctx.output.height,
            ctx.output.width, ctx.output.height};
        wf::geometry_t in_region = wf::interpolate(pvg, (wf::geometry_t) *d.slot, ep);

        const int i = ctx.cur_ws.x + d.cell.x, j = ctx.cur_ws.y + d.cell.y;
        auto cell = sliding
            ? coords::pane_on_screen(ctx, i, j, state.g, WALL_GAP, state.pan_dir, state.pan_amount)
            : coords::cell_on_screen(ctx, i, j, state.g, WALL_GAP);

        wf::geometry_t fin = wf::scale_box(region, cell, in_region);
        d.screen = fin;

        /* Scale and translate the whole window family (the toplevel plus its
         * dialogs) as one rigid unit about the toplevel's centre, so dialogs ride
         * on their parent preview at the right size and position. */
        const double sx = (double) fin.width  / std::max(1, pvg.width);
        const double sy = (double) fin.height / std::max(1, pvg.height);
        const double pcx = pvg.x + pvg.width / 2.0, pcy = pvg.y + pvg.height / 2.0;
        const double fcx = fin.x + fin.width / 2.0, fcy = fin.y + fin.height / 2.0;

        reconcile_family(view, d);
        for (auto& [member, tr] : d.family)
        {
            auto mvg = member->get_geometry();
            const double mcx = mvg.x + mvg.width / 2.0, mcy = mvg.y + mvg.height / 2.0;

            auto node = member->get_transformed_node();
            node->begin_transform_update();
            tr->scale_x       = (float) sx;
            tr->scale_y       = (float) sy;
            tr->translation_x = (float) ((fcx + sx * (mcx - pcx)) - mcx);
            tr->translation_y = (float) ((fcy + sy * (mcy - pcy)) - mcy);
            node->end_transform_update();
        }
    }

    if (backdrop) { backdrop->update(state.g, state.pan_dir, state.pan_amount, state.hover); }
}

void spread_t::render(const frame_ctx& ctx, const render_state& state)
{
    place(ctx, state);
}

bool spread_t::animating()
{
    for (auto& [view, d] : views)
    {
        if (d.slot && d.slot->running()) { return true; }
    }

    return false;
}

void spread_t::aim_slot(wayfire_toplevel_view view, wf::point_t cell, wf::geometry_t target)
{
    auto& d = views[view];
    d.cell     = cell;
    d.dragging = false;

    if (!d.slot)
    {
        /* A freshly shown preview starts already at its target so it just
         * appears (the g-axis still fans it out from the real window). */
        d.slot = std::make_unique<wf::geometry_animation_t>(anim_dur);
        d.slot->set_start(target);
    } else
    {
        d.slot->set_start(*d.slot);
    }

    d.slot->set_end(target);
    d.slot->start();
}

void spread_t::detach_family(view_data& d)
{
    for (auto& [member, tr] : d.family)
    {
        member->get_transformed_node()
            ->rem_transformer<wf::scene::view_2d_transformer_t>(PLUGIN_NAME);
    }

    d.family.clear();
}

void spread_t::reconcile_family(wayfire_toplevel_view parent, view_data& d)
{
    std::set<wayfire_toplevel_view> current;
    for (auto& m : parent->enumerate_views()) { current.insert(m); }

    for (auto it = d.family.begin(); it != d.family.end(); )
    {
        if (!current.count(it->first))
        {
            it->first->get_transformed_node()
                ->rem_transformer<wf::scene::view_2d_transformer_t>(PLUGIN_NAME);
            it = d.family.erase(it);
        } else { ++it; }
    }

    for (auto& m : current)
    {
        if (!d.family.count(m)) { d.family[m] = ensure_transformer(m); }
    }
}

void spread_t::clear()
{
    for (auto& [view, d] : views) { detach_family(d); }
    views.clear();

    for (auto& v : hidden_views) { wf::scene::set_node_enabled(v->get_root_node(), true); }
    hidden_views.clear();

    /* Restore the minimized views we made visible, once, when the spread ends. */
    for (auto& v : shown_minimized) { wf::scene::set_node_enabled(v->get_root_node(), false); }
    shown_minimized.clear();

    remove_backdrop();
    laid_out_ws = {-1, -1};
}

void spread_t::forget(wayfire_toplevel_view view)
{
    if (auto it = views.find(view); it != views.end())
    {
        detach_family(it->second);
        views.erase(it);
    } else
    {
        /* Not a preview of its own; it may be a dialog in some family. */
        for (auto& [parent, d] : views)
        {
            auto f = d.family.find(view);
            if (f != d.family.end())
            {
                view->get_transformed_node()
                    ->rem_transformer<wf::scene::view_2d_transformer_t>(PLUGIN_NAME);
                d.family.erase(f);
                break;
            }
        }
    }

    if (auto h = std::find(hidden_views.begin(), hidden_views.end(), view);
        h != hidden_views.end())
    {
        wf::scene::set_node_enabled(view->get_root_node(), true);
        hidden_views.erase(h);
    }

    if (auto m = std::find(shown_minimized.begin(), shown_minimized.end(), view);
        m != shown_minimized.end())
    {
        wf::scene::set_node_enabled(view->get_root_node(), false);
        shown_minimized.erase(m);
    }
}

void spread_t::release_for_drag(wayfire_toplevel_view view)
{
    auto it = views.find(view);
    if (it == views.end()) { return; }

    /* Hand the window to the drag core: drop our transformers and stop placing
     * it until the next relayout re-slots it. */
    detach_family(it->second);
    it->second.dragging = true;
}

wayfire_toplevel_view spread_t::view_at(wf::pointf_t local) const
{
    for (auto& [view, d] : views)
    {
        if (d.screen & local) { return view; }
    }

    return nullptr;
}

wf::geometry_t spread_t::thumb_of(wayfire_toplevel_view view) const
{
    auto it = views.find(view);
    return (it != views.end()) ? it->second.screen : wf::geometry_t{0, 0, 0, 0};
}
}
