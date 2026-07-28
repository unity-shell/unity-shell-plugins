#pragma once

#include <functional>
#include <memory>

#include <wayfire/geometry.hpp>
#include <wayfire/toplevel-view.hpp>

namespace wf { class output_t; }

namespace spatial
{
class spread_t;

/**
 * Handles picking, dragging and dropping spread thumbnails between workspaces.
 */
class window_drag_t
{
  public:
    window_drag_t(wf::output_t *output, spread_t *spread,
        std::function<void (wayfire_toplevel_view, wf::point_t)> on_click,
        std::function<void ()> on_moved);
    ~window_drag_t();
    window_drag_t(const window_drag_t&) = delete;
    window_drag_t& operator =(const window_drag_t&) = delete;

    void press();
    void motion();
    /* Returns true if the release was an empty click -- no thumbnail was dragged
     * or picked -- which the caller uses to dismiss the wall. */
    bool release();
    void cancel();
    void forget(wayfire_toplevel_view view);
    bool active() const;

  private:
    struct impl;
    std::unique_ptr<impl> priv;
};
}
