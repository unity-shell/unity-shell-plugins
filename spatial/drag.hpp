#pragma once

#include <functional>
#include <memory>

#include <wayfire/geometry.hpp>
#include <wayfire/toplevel-view.hpp>

namespace wf { class output_t; }

namespace spatial
{
class spread_t;

class window_drag_t
{
  public:
    enum class result { dragged, window_click, empty_click };

    window_drag_t(wf::output_t *output, spread_t *spread,
        std::function<void (wayfire_toplevel_view, wf::point_t)> on_click,
        std::function<void ()> on_moved);
    ~window_drag_t();
    window_drag_t(const window_drag_t&) = delete;
    window_drag_t& operator =(const window_drag_t&) = delete;

    void press();
    void motion();
    result release();
    void cancel();
    void forget(wayfire_toplevel_view view);
    bool active() const;

  private:
    struct impl;
    std::unique_ptr<impl> priv;
};
}
