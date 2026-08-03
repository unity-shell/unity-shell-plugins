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
#include <wayfire/scene.hpp>
#include <wayfire/scene-operations.hpp>
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
    wf::point_t sel{0, 0};   /* keyboard-selected wall cell (seeded on wall entry) */
};

class backdrop_node_t;

/**
 * RAII enable/disable of a view's scene node against the enabled counter Wayfire
 * itself also drives. It pushes the requested state on construction and pops the
 * inverse on destruction, so the counter is always left exactly as found -- no
 * hand-balanced bookkeeping, and impossible to leak.
 */
class node_toggle
{
  public:
    node_toggle(wf::scene::node_ptr node, bool enable) :
        node(std::move(node)), enable(enable)
    {
        wf::scene::set_node_enabled(this->node, enable);
    }

    ~node_toggle()
    {
        if (node) { wf::scene::set_node_enabled(node, !enable); }
    }

    node_toggle(node_toggle&& o) noexcept :
        node(std::move(o.node)), enable(o.enable) { o.node = nullptr; }
    node_toggle(const node_toggle&)            = delete;
    node_toggle& operator =(const node_toggle&) = delete;
    node_toggle& operator =(node_toggle&&)      = delete;

    /* True when we surfaced a node (minimized view), false when we hid one. */
    bool surfaced() const { return enable; }

  private:
    wf::scene::node_ptr node;
    bool enable;
};

/**
 * Maintains spread layout, per-view transforms and backdrop composition.
 */
class spread_t
{
  public:
    explicit spread_t(wf::output_t *output);
    ~spread_t();

    void ensure_layout(const frame_ctx& ctx, const std::vector<std::string>& filter);
    /* Re-slot every preview. animate == false snaps slots to their targets (no
     * ease) -- used when the on-screen positions are already correct and only the
     * workspace-relative slot coordinates changed (e.g. re-centring after a slide
     * commit). */
    void layout(const frame_ctx& ctx, const std::vector<std::string>& filter, bool animate = true);
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
    void aim_slot(wayfire_toplevel_view view, wf::point_t cell, wf::geometry_t target,
        bool animate);

    /* Force a view's scene node on/off, remembering how to restore it. */
    void override_node(wayfire_toplevel_view view, bool on);
    void restore_node(wayfire_toplevel_view view);

    void ensure_backdrop();
    void remove_backdrop();

    wf::output_t *output;
    wf::option_wrapper_t<wf::animation_description_t> anim_dur{"spatial/duration"};
    std::map<wayfire_toplevel_view, view_data> views;
    /* Views whose scene-node enabled state we forced (minimized views surfaced,
     * filtered views hidden). Each toggle restores its node when erased/cleared. */
    std::map<wayfire_toplevel_view, node_toggle> node_overrides;
    std::shared_ptr<backdrop_node_t> backdrop;
    wf::point_t laid_out_ws{-1, -1};
};
}
