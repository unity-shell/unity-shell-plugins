#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <wayfire/per-output-plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/scene-input.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/plugins/common/input-grab.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>

#include "config.hpp"
#include "geometry.hpp"
#include "axis.hpp"
#include "resources.hpp"
#include "stage.hpp"
#include "present.hpp"
#include "slide.hpp"
#include "gesture.hpp"
#include "drag.hpp"
#include "interaction.hpp"

namespace spatial
{
/* Per-output bridge: owns the axis, the resources, and the frame loop, and
 * wires the gesture source, renderer, and input layer together. */
class controller : public wf::per_output_plugin_instance_t,
    public wf::pointer_interaction_t,
    public wf::keyboard_interaction_t
{
  public:
    void init() override;
    void fini() override;

    void toggle_apps_spread();
    void toggle_workspaces_spread();
    void close_spread();
    void spread_app(const std::vector<std::string>& ids);
    void reflow_for_inset();   /* the shell inset changed: reflow if a spread is settled */

    /* Commands the input layer runs. */
    void activate_window(wayfire_toplevel_view view, wf::point_t ws);
    void enter_workspace(wf::point_t ws);
    void switch_workspace(wf::point_t ws);
    void repaint();

    static bool inhibited();
    static void inhibit();
    static void uninhibit();

  private:
    void gesture_begin(int fingers);
    void gesture_update(double dx, double dy);
    void gesture_end(double vx, double vy);
    void gesture_pinch(int fingers, double scale);

    void settle_to(double target);
    void relayout(bool animate = true);
    void end_to_desktop();
    void recenter_apps_spread();

    void set_phase(phase stage);   /* clears the filter on desktop, seeds the ring on the wall */
    void reconcile();
    void apply_resources();
    void publish_phase(phase stage);   /* panel event, deduped by `published` */
    void render_frame();
    void advance();
    void set_hook();
    void unhook();

    void begin_slide();
    void slide_update(double dx, double dy);
    void slide_end();
    void finish_slide();

    void handle_pointer_button(const wlr_pointer_button_event& ev) override;
    void handle_pointer_motion(wf::pointf_t position, uint32_t time_ms) override;
    void handle_keyboard_key(wf::seat_t*, wlr_keyboard_key_event ev) override;
    void update_cursor();

    void handle_mapped(wf::view_mapped_signal *ev);
    void handle_unmapped(wf::view_unmapped_signal *ev);
    void handle_focus_request(wf::view_focus_request_signal *ev);
    void relayout_if_idle();
    bool can_relayout();   /* true when the spread is settled and safe to reflow */
    bool cursor_here() const;

    std::unique_ptr<present_t> present;
    std::unique_ptr<window_drag_t> drag;
    std::unique_ptr<wf::input_grab_t> grab;
    std::unique_ptr<slide_t> slide;
    std::unique_ptr<interaction> input;
    swipe_gesture_t swipe;

    axis g_axis{"spatial/duration"};

    phase current = phase::desktop;     /* input phase, latched through a settle */
    phase published = phase::desktop;   /* last phase sent to the panel, tracks the visible g */

    std::optional<toggled> t_activate, t_top, t_grab, t_hooks;

    std::vector<std::string> filter;
    bool self_activating = false;

    wf::effect_hook_t pre_hook  = [this] { render_frame(); };
    wf::effect_hook_t post_hook = [this] { advance(); };

    wf::plugin_activation_data_t grab_interface{
        .name = PLUGIN_NAME,
        .capabilities = wf::CAPABILITY_MANAGE_COMPOSITOR,
        .cancel = [this] { end_to_desktop(); },
    };

    wf::signal::connection_t<wf::view_unmapped_signal> on_view_unmapped =
        [this] (wf::view_unmapped_signal *ev) { handle_unmapped(ev); };
    wf::signal::connection_t<wf::view_mapped_signal> on_view_mapped =
        [this] (wf::view_mapped_signal *ev) { handle_mapped(ev); };
    wf::signal::connection_t<wf::view_focus_request_signal> on_focus_request =
        [this] (wf::view_focus_request_signal *ev) { handle_focus_request(ev); };
    wf::signal::connection_t<wf::workarea_changed_signal> on_workarea_changed =
        [this] (wf::workarea_changed_signal *) { relayout_if_idle(); };
};
}
