#include "renderer.hpp"
#include "config.hpp"
#include "backdrop.hpp"
#include "packing.hpp"

#include <algorithm>
#include <set>
#include <utility>

#include <wayfire/core.hpp>
#include <wayfire/render.hpp>
#include <wayfire/workarea.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/scene.hpp>
#include <wayfire/scene-operations.hpp>
#include <wayfire/plugins/common/geometry-animation.hpp>

namespace spatial
{

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
        layout(ctx, filter, true);
    }
}

void spread_t::layout(const frame_ctx& ctx, const std::vector<std::string>& filter, bool animate)
{
    ensure_backdrop();
    laid_out_ws = ctx.cur_ws;

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

        /* Minimized views stay mapped but have their node disabled; surface it
         * once for the lifetime of the spread (override_node is idempotent and
         * restores in clear()). The enabled state is a shared counter Wayfire
         * also drives, so toggling per relayout would drift and strand views. */
        if (v->minimized) { override_node(v, true); }

        /* Bucket by Wayfire's own (grid-clamped) view->workspace mapping, not a
         * raw floor of the geometry centre which can land in a phantom cell. */
        const wf::point_t vws = output->wset()->get_view_main_workspace(v);
        cells[{vws.x - ctx.cur_ws.x, vws.y - ctx.cur_ws.y}].push_back(v);
        wanted.insert(v);
    }

    /* Reconcile the filter's hidden set idempotently: un-hide views we hid that
     * are no longer filtered, then hide the newly filtered ones. Surfaced
     * minimized overrides (surfaced() == true) are left for teardown. */
    for (auto it = node_overrides.begin(); it != node_overrides.end(); )
    {
        const bool we_hid = !it->second.surfaced();
        const bool still  = std::find(want_hidden.begin(), want_hidden.end(), it->first) != want_hidden.end();
        if (we_hid && !still) { it = node_overrides.erase(it); }   /* destructor re-enables */
        else { ++it; }
    }
    for (auto& v : want_hidden) { override_node(v, false); }

    /* Drop previews for views that are no longer shown. */
    for (auto it = views.begin(); it != views.end(); )
    {
        if (!wanted.count(it->first)) { detach_family(it->second); it = views.erase(it); }
        else { ++it; }
    }

    /* Aim each shown view's slot at its freshly computed target; persistent
     * views ease from their current slot, so the arrangement never snaps. */
    const double monitor_h = output->get_relative_geometry().height;
    for (auto& [cell, cell_views] : cells)
    {
        /* Cell-local: slots live relative to the window's own workspace tile,
         * not an absolute cell*output offset. place() composes the tile's
         * on-screen position each frame, so the stored slot is invariant to a
         * workspace switch (which shifts d.cell but not the real window). */
        wf::geometry_t area = {
            ctx.workarea.x + OUTER_MARGIN,
            ctx.workarea.y + OUTER_MARGIN,
            ctx.workarea.width - OUTER_MARGIN * 2,
            ctx.workarea.height - OUTER_MARGIN * 2};
        for (auto& p : pack_cell(cell_views, area, monitor_h))
        {
            aim_slot(p.view, {cell.first, cell.second}, p.target, animate);
        }
    }
}

/* Position every window for the current frame in two steps: interpolate its real
 * geometry toward its expose slot by ep=clamp(g,0,1) (the fan-out within its
 * workspace), then map that onto the workspace's on-screen cell at g
 * (cell_on_screen / pane_on_screen) through the view's 2D transformer. */
void spread_t::render(const frame_ctx& ctx, const render_state& state)
{
    const double ep = std::clamp(state.g, 0.0, 1.0);

    /* Every view resolves its slot inside the same cell-local [0, output) box,
     * so build it once rather than per view. */
    const wf::geometry_t region = wf::construct_box(wf::pointf_t(0.0, 0.0), ctx.output);

    for (auto& [view, d] : views)
    {
        if (!d.slot || d.dragging) { continue; }

        auto pvg = view->get_geometry();   /* current-ws-relative; drives the family transform */

        /* Resolve the slot in a cell-local frame: shift the real window back by
         * its OWN-cell offset (d.cell, relative to the current workspace -- which
         * is exactly the offset get_geometry() already carries), so it sits in the
         * same [0, output) box the slot was laid out in. This frame does not move
         * when the current workspace changes, so a slide commit needs no snap. */
        const int i = ctx.cur_ws.x + d.cell.x, j = ctx.cur_ws.y + d.cell.y;
        wf::geometry_t pvg_local = pvg;
        pvg_local.x -= (double) d.cell.x * ctx.output.width;
        pvg_local.y -= (double) d.cell.y * ctx.output.height;

        wf::geometry_t in_region = wf::interpolate(pvg_local, (wf::geometry_t) *d.slot, ep);

        auto cell = coords::cell_or_pane(ctx, i, j, state.g, state.pan_dir, state.pan_amount);

        wf::geometry_t fin = wf::scale_box(region, cell, in_region);
        d.screen = fin;

        /* Scale and translate the whole window family (the toplevel plus its
         * dialogs) as one rigid unit about the toplevel's centre, so dialogs ride
         * on their parent preview at the right size and position. */
        const double sx = fin.width  / std::max(1.0, pvg.width);
        const double sy = fin.height / std::max(1.0, pvg.height);
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

    if (backdrop) { backdrop->update(state.g, state.pan_dir, state.pan_amount, state.sel); }
}

bool spread_t::animating()
{
    for (auto& [view, d] : views)
    {
        if (d.slot && d.slot->running()) { return true; }
    }

    return false;
}

void spread_t::aim_slot(wayfire_toplevel_view view, wf::point_t cell, wf::geometry_t target,
    bool animate)
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
        /* Ease from the current position, or snap (animate == false) when the
         * layout only shifted in workspace-relative coordinates. */
        d.slot->set_start(animate ? (wf::geometry_t) *d.slot : target);
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

void spread_t::override_node(wayfire_toplevel_view view, bool on)
{
    /* try_emplace is a no-op if we already forced this view (once per spread);
     * the toggle applies `on` now and restores it when erased/cleared. */
    node_overrides.try_emplace(view, view->get_root_node(), on);
}

void spread_t::restore_node(wayfire_toplevel_view view)
{
    node_overrides.erase(view);   /* toggle destructor restores the node */
}

void spread_t::clear()
{
    for (auto& [view, d] : views) { detach_family(d); }
    views.clear();

    node_overrides.clear();   /* each toggle restores its node on destruction */

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

    restore_node(view);
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
