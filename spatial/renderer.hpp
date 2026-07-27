#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <wayfire/geometry.hpp>
#include <wayfire/output.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/plugins/common/geometry-animation.hpp>

#include "coords.hpp"

namespace spatial
{
/**
 * Frame parameters consumed by the spread renderer on each repaint.
 */
struct render_state
{
    double      g = 0.0;
    wf::point_t pan_dir{0, 0};
    double      pan_amount = 0.0;
    wf::point_t hover{-1, -1};
};

class backdrop_node_t;

/**
 * Maintains spread layout, per-view transforms and backdrop composition.
 */
class spread_t
{
  public:
    explicit spread_t(wf::output_t *output);
    ~spread_t();

    void ensure_layout(const frame_ctx& ctx, const std::vector<std::string>& filter);
    /* animate == false snaps slots to their targets (no ease) -- used when the
     * on-screen positions are already correct and only the workspace-relative
     * slot coordinates changed (e.g. re-centring after a slide commit). */
    void relayout(const frame_ctx& ctx, const std::vector<std::string>& filter, bool animate = true);
    void render(const frame_ctx& ctx, const render_state& state);
    void clear();

    /** True while any preview is still easing toward its slot. */
    bool animating();

    void forget(wayfire_toplevel_view view);
    void release_for_drag(wayfire_toplevel_view view);
    wayfire_toplevel_view view_at(wf::pointf_t local) const;
    wf::geometry_t thumb_of(wayfire_toplevel_view view) const;

  private:
    using transformer_t = std::shared_ptr<wf::scene::view_2d_transformer_t>;

    /**
     * A persistent preview unit: one top-level window plus its dialog family,
     * with a single animated slot the whole family is transformed onto. Views
     * survive relayouts so the slot can ease from the old arrangement to the new
     * one instead of snapping.
     */
    struct view_data
    {
        wf::point_t    cell{0, 0};
        wf::geometry_t screen{};
        bool           dragging = false;
        std::unique_ptr<wf::geometry_animation_t> slot;
        std::map<wayfire_toplevel_view, transformer_t> family;
    };

    transformer_t ensure_transformer(wayfire_toplevel_view view);
    void detach_family(view_data& d);
    void reconcile_family(wayfire_toplevel_view parent, view_data& d);
    void aim_slot(wayfire_toplevel_view view, wf::point_t cell, wf::geometry_t target);

    /* Force a view's scene node on/off, remembering how to restore it. */
    void override_node(wayfire_toplevel_view view, bool on);
    void restore_node(wayfire_toplevel_view view);

    void ensure_backdrop();
    void remove_backdrop();
    void layout(const frame_ctx& ctx, const std::vector<std::string>& filter, bool animate);
    void layout_cell(wf::point_t cell, std::vector<wayfire_toplevel_view>& cell_views,
        wf::geometry_t area);
    void place(const frame_ctx& ctx, const render_state& state);

    wf::output_t *output;
    wf::option_wrapper_t<wf::animation_description_t> anim_dur{"spatial/duration"};
    std::map<wayfire_toplevel_view, view_data> views;
    /* Views whose scene-node enabled state we forced (minimized views surfaced,
     * filtered views hidden), mapped to the enabled-state to restore on teardown
     * -- the balanced inverse of what we applied, so the shared counter Wayfire
     * also drives is left exactly as we found it. */
    std::map<wayfire_toplevel_view, bool> node_overrides;
    std::shared_ptr<backdrop_node_t> backdrop;
    wf::point_t laid_out_ws{-1, -1};
    bool slot_animate = true;   /* set per layout(); consumed by aim_slot */
};
}
