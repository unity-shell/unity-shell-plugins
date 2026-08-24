#pragma once

#include <wayfire/geometry.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>

#include "geometry.hpp"
#include "stage.hpp"

namespace spatial
{
class controller;
class present_t;
class window_drag_t;

/*
 * Input over the pickable frame. One pointer rule: the window under the cursor
 * is activated, else the wall picks the workspace under it and the app spread
 * dismisses. The wall keeps a cell selection for the ring. The app spread
 * switches workspace live.
 */
class interaction
{
  public:
    interaction(controller *owner, present_t *present, window_drag_t *drag) :
        owner(owner), present(present), drag(drag)
    {}

    void seed(wf::point_t workspace) { selected = workspace; }
    wf::point_t selection() const { return selected; }

    void on_button(const wlr_pointer_button_event& ev, const world& ctx, phase stage);
    void on_motion(phase stage);
    void on_key(const wlr_keyboard_key_event& ev, const world& ctx, phase stage);

  private:
    controller *owner;
    present_t *present;
    window_drag_t *drag;
    wf::point_t selected{0, 0};
};
}
