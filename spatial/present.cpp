#include "present.hpp"
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
present_t::present_t(wf::output_t *o) : output(o) {}
present_t::~present_t() = default;

void present_t::ensure_backdrop()
{
    if (backdrop) { return; }
    backdrop = std::make_shared<backdrop_node_t>(output);
    auto root = wf::get_core().scene();
    wf::scene::add_front(root->layers[(size_t) wf::scene::layer::BOTTOM], backdrop);
}

void present_t::remove_backdrop()
{
    if (!backdrop) { return; }
    wf::scene::remove_child(backdrop);
    backdrop = nullptr;
}

present_t::transformer_t present_t::ensure_transformer(wayfire_toplevel_view view)
{
    auto node = view->get_transformed_node();
    if (auto existing = node->get_transformer<wf::scene::view_2d_transformer_t>(PLUGIN_NAME))
    {
        return existing;
    }

    auto transformer = std::make_shared<wf::scene::view_2d_transformer_t>(view);
    node->add_transformer(transformer, wf::TRANSFORMER_2D + 1, PLUGIN_NAME);
    return transformer;
}

void present_t::ensure_layout(const world& ctx, const std::vector<std::string>& filter)
{
    ensure_backdrop();
    if ((ctx.cur_ws.x != laid_out_ws.x) || (ctx.cur_ws.y != laid_out_ws.y))
    {
        layout(ctx, filter, true);
    }
}

void present_t::layout(const world& ctx, const std::vector<std::string>& filter, bool animate)
{
    ensure_backdrop();
    laid_out_ws = ctx.cur_ws;

    /* Partition the workspace views: surface minimized ones, gather the app-id
     * filtered-out ones, and bucket the rest by workspace cell. */
    std::map<std::pair<int, int>, std::vector<wayfire_toplevel_view>> cells;
    std::set<wayfire_toplevel_view> wanted;
    std::vector<wayfire_toplevel_view> want_hidden;

    for (auto& view : output->wset()->get_views(wf::WSET_MAPPED_ONLY))
    {
        if (!filter.empty() &&
            (std::find(filter.begin(), filter.end(), view->get_app_id()) == filter.end()))
        {
            want_hidden.push_back(view);
            continue;
        }

        /* Surface once. Wayfire shares the enabled counter, so toggling per
         * relayout would strand views. */
        if (view->minimized) { override_node(view, true); }

        /* Wayfire's grid-clamped workspace, not the geometry centre (phantom cells). */
        const wf::point_t view_ws = output->wset()->get_view_main_workspace(view);
        cells[{view_ws.x - ctx.cur_ws.x, view_ws.y - ctx.cur_ws.y}].push_back(view);
        wanted.insert(view);
    }

    /* Un-hide views no longer filtered, then hide newly filtered ones. Surfaced
     * minimized overrides stay for teardown. */
    for (auto it = node_overrides.begin(); it != node_overrides.end(); )
    {
        const bool we_hid = !it->second.surfaced();
        const bool still  = std::find(want_hidden.begin(), want_hidden.end(), it->first) != want_hidden.end();
        if (we_hid && !still) { it = node_overrides.erase(it); }   /* destructor re-enables */
        else { ++it; }
    }
    for (auto& view : want_hidden) { override_node(view, false); }

    /* Drop previews for views that are no longer shown. */
    for (auto it = views.begin(); it != views.end(); )
    {
        if (!wanted.count(it->first)) { detach_family(it->second); it = views.erase(it); }
        else { ++it; }
    }

    /* Persistent slots ease from where they are. Cell-local, so a workspace
     * switch (or slide commit) never snaps them. */
    const double monitor_h = output->get_relative_geometry().height;
    for (auto& [cell, cell_views] : cells)
    {
        wf::geometry_t area = {
            ctx.workarea.x + OUTER_MARGIN,
            ctx.workarea.y + OUTER_MARGIN,
            ctx.workarea.width - OUTER_MARGIN * 2,
            ctx.workarea.height - OUTER_MARGIN * 2};
        for (auto& slot : pack_cell(cell_views, area, monitor_h))
        {
            aim_slot(slot.view, {cell.first, cell.second}, slot.target, animate);
        }
    }
}

/* Interpolate each window toward its expose slot by `expose`, then map onto its
 * cell via the 2D transformer, recording the on-screen rect as a pickable. */
void present_t::render(const world& ctx, double g, wf::point_t pan_dir, double pan_amount,
    wf::point_t sel)
{
    const double expose = std::clamp(g, 0.0, 1.0);

    /* Shared cell-local box, built once. */
    const wf::geometry_t unit_box = wf::construct_box(wf::pointf_t(0.0, 0.0), ctx.output);

    frame_.clear();

    for (auto& [view, data] : views)
    {
        if (!data.slot || data.dragging) { continue; }

        auto view_geo = view->get_geometry();   /* current-ws-relative */

        /* Shift the window back by its cell offset (get_geometry already carries
         * it) into the cell-local box the slot was laid out in. */
        const int col = ctx.cur_ws.x + data.cell.x, row = ctx.cur_ws.y + data.cell.y;
        wf::geometry_t local_geo = view_geo;
        local_geo.x -= (double) data.cell.x * ctx.output.width;
        local_geo.y -= (double) data.cell.y * ctx.output.height;

        wf::geometry_t eased = wf::interpolate(local_geo, (wf::geometry_t) *data.slot, expose);

        auto cell = geom::cell_or_pane(ctx, col, row, g, pan_dir, pan_amount);

        wf::geometry_t screen_rect = wf::scale_box(unit_box, cell, eased);
        data.screen = screen_rect;
        frame_.push_back({screen_rect, view});

        /* Scale/translate the family as one rigid unit about the toplevel
         * centre, so dialogs ride on their parent. */
        const double scale_x = screen_rect.width  / std::max(1.0, view_geo.width);
        const double scale_y = screen_rect.height / std::max(1.0, view_geo.height);
        const double parent_cx = view_geo.x + view_geo.width / 2.0;
        const double parent_cy = view_geo.y + view_geo.height / 2.0;
        const double dest_cx = screen_rect.x + screen_rect.width / 2.0;
        const double dest_cy = screen_rect.y + screen_rect.height / 2.0;

        reconcile_family(view, data);
        for (auto& [member, transformer] : data.family)
        {
            auto member_geo = member->get_geometry();
            const double member_cx = member_geo.x + member_geo.width / 2.0;
            const double member_cy = member_geo.y + member_geo.height / 2.0;

            auto node = member->get_transformed_node();
            node->begin_transform_update();
            transformer->scale_x       = (float) scale_x;
            transformer->scale_y       = (float) scale_y;
            transformer->translation_x = (float) ((dest_cx + scale_x * (member_cx - parent_cx)) - member_cx);
            transformer->translation_y = (float) ((dest_cy + scale_y * (member_cy - parent_cy)) - member_cy);
            node->end_transform_update();
        }
    }

    if (backdrop) { backdrop->update(ctx, g, pan_dir, pan_amount, sel); }
}

bool present_t::animating()
{
    for (auto& [view, data] : views)
    {
        if (data.slot && data.slot->running()) { return true; }
    }

    return false;
}

void present_t::aim_slot(wayfire_toplevel_view view, wf::point_t cell, wf::geometry_t target,
    bool animate)
{
    auto& data = views[view];
    data.cell     = cell;
    data.dragging = false;

    if (!data.slot)
    {
        /* New preview starts at its target. The g-axis still fans it out. */
        data.slot = std::make_unique<wf::geometry_animation_t>(anim_dur);
        data.slot->set_start(target);
    } else
    {
        data.slot->set_start(animate ? (wf::geometry_t) *data.slot : target);
    }

    data.slot->set_end(target);
    data.slot->start();
}

void present_t::detach_family(view_data& data)
{
    for (auto& [member, transformer] : data.family)
    {
        member->get_transformed_node()
            ->rem_transformer<wf::scene::view_2d_transformer_t>(PLUGIN_NAME);
    }

    data.family.clear();
}

void present_t::reconcile_family(wayfire_toplevel_view parent, view_data& data)
{
    std::set<wayfire_toplevel_view> members;
    for (auto& member : parent->enumerate_views()) { members.insert(member); }

    for (auto it = data.family.begin(); it != data.family.end(); )
    {
        if (!members.count(it->first))
        {
            it->first->get_transformed_node()
                ->rem_transformer<wf::scene::view_2d_transformer_t>(PLUGIN_NAME);
            it = data.family.erase(it);
        } else { ++it; }
    }

    for (auto& member : members)
    {
        if (!data.family.count(member)) { data.family[member] = ensure_transformer(member); }
    }
}

void present_t::override_node(wayfire_toplevel_view view, bool on)
{
    /* No-op if already forced. Restores on erase. */
    node_overrides.try_emplace(view, view->get_root_node(), on);
}

void present_t::restore_node(wayfire_toplevel_view view)
{
    node_overrides.erase(view);   /* toggle destructor restores the node */
}

void present_t::clear()
{
    for (auto& [view, data] : views) { detach_family(data); }
    views.clear();
    frame_.clear();

    node_overrides.clear();

    remove_backdrop();
    laid_out_ws = {-1, -1};
}

void present_t::forget(wayfire_toplevel_view view)
{
    if (auto it = views.find(view); it != views.end())
    {
        detach_family(it->second);
        views.erase(it);
    } else
    {
        /* Not a preview of its own. It may be a dialog in some family. */
        for (auto& [parent, data] : views)
        {
            auto member = data.family.find(view);
            if (member != data.family.end())
            {
                view->get_transformed_node()
                    ->rem_transformer<wf::scene::view_2d_transformer_t>(PLUGIN_NAME);
                data.family.erase(member);
                break;
            }
        }
    }

    restore_node(view);
}

void present_t::release_for_drag(wayfire_toplevel_view view)
{
    auto it = views.find(view);
    if (it == views.end()) { return; }

    /* Hand off to the drag core: drop transformers, stop placing until relayout. */
    detach_family(it->second);
    it->second.dragging = true;
}

wayfire_toplevel_view present_t::view_at(wf::pointf_t local) const
{
    /* frame_ is a per-render snapshot; confirm the hit is still a live preview,
     * so a click landing on a just-closed window falls through to empty. */
    if (auto hit = topmost(frame_, local))
    {
        if (views.count(hit->view)) { return hit->view; }
    }
    return nullptr;
}

wf::geometry_t present_t::thumb_of(wayfire_toplevel_view view) const
{
    auto it = views.find(view);
    return (it != views.end()) ? it->second.screen : wf::geometry_t{0, 0, 0, 0};
}
}
