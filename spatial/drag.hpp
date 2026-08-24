#pragma once

#include <functional>
#include <memory>

#include <wayfire/geometry.hpp>
#include <wayfire/toplevel-view.hpp>

namespace wf { class output_t; }

namespace spatial
{
class present_t;

/* Picks, drags and drops spread thumbnails between workspaces. */
class window_drag_t
{
  public:
    window_drag_t(wf::output_t *output, present_t *present,
        std::function<void (wayfire_toplevel_view, wf::point_t)> on_click,
        std::function<void ()> on_moved);
    ~window_drag_t();
    window_drag_t(const window_drag_t&) = delete;
    window_drag_t& operator =(const window_drag_t&) = delete;

    void press();
    void motion();
    /* True when the release was an empty click (nothing dragged or picked). The
     * caller uses it to pick the workspace under the cursor. */
    bool release();
    void cancel();
    void forget(wayfire_toplevel_view view);
    bool active() const;

  private:
    struct impl;
    std::unique_ptr<impl> priv;
};
}
