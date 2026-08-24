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

#include "geometry.hpp"
#include "scene.hpp"

namespace spatial
{
class backdrop_node_t;

/* RAII node enable/disable. Wayfire shares the enabled counter, so we pop the
 * inverse on destruction to leave it as found. */
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

    /* true = surfaced a minimized view, false = hid a filtered one. */
    bool surfaced() const { return enable; }

  private:
    wf::scene::node_ptr node;
    bool enable;
};

/* Lays out previews, drives their transforms and the backdrop, and records the
 * pickable frame. */
class present_t
{
  public:
    explicit present_t(wf::output_t *output);
    ~present_t();

    void ensure_layout(const world& ctx, const std::vector<std::string>& filter);
    /* animate==false snaps instead of easing (on-screen positions already correct). */
    void layout(const world& ctx, const std::vector<std::string>& filter, bool animate = true);
    void render(const world& ctx, double g, wf::point_t pan_dir, double pan_amount,
        wf::point_t sel);

    bool animating();

    void clear();

    /* Drag support, backed by the same per-view geometry as the frame. */
    void forget(wayfire_toplevel_view view);

    void release_for_drag(wayfire_toplevel_view view);
    wayfire_toplevel_view view_at(wf::pointf_t local) const;
    wf::geometry_t thumb_of(wayfire_toplevel_view view) const;

  private:
    using transformer_t = std::shared_ptr<wf::scene::view_2d_transformer_t>;

    /* A window plus its dialog family on one animated slot. Survives relayouts
     * so the slot eases from the old arrangement to the new. */
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

    void override_node(wayfire_toplevel_view view, bool on);
    void restore_node(wayfire_toplevel_view view);

    void ensure_backdrop();
    void remove_backdrop();

    wf::output_t *output;
    wf::option_wrapper_t<wf::animation_description_t> anim_dur{"spatial/duration"};
    std::map<wayfire_toplevel_view, view_data> views;
    /* Forced node states (minimized surfaced, filtered hidden). Restored on erase. */
    std::map<wayfire_toplevel_view, node_toggle> node_overrides;
    std::shared_ptr<backdrop_node_t> backdrop;
    wf::point_t laid_out_ws{-1, -1};
    frame frame_;
};
}
