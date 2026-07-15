#pragma once

#include <wayfire/nonstd/wlroots-full.hpp>

namespace spatial
{
class controller;

enum class mode_id { desktop, apps_spread, workspaces_spread };

struct mode
{
    virtual ~mode() = default;

    virtual mode_id id() const = 0;
    virtual mode_id classify(double g) const = 0;

    struct desired
    {
        bool activated = false;
        bool top       = false;
        bool grabbed   = false;
        bool overlay   = false;
    };

    virtual desired want() const = 0;
    virtual bool slides() const { return false; }

    virtual void on_button(controller&, const wlr_pointer_button_event&) {}
    virtual void on_motion(controller&) {}
    virtual void slide_settle(controller&, bool committed) {}
};

struct desktop_mode : mode
{
    mode_id id() const override { return mode_id::desktop; }
    mode_id classify(double g) const override;
    desired want() const override { return {}; }
    bool slides() const override { return true; }
    void slide_settle(controller& c, bool committed) override;
};

struct apps_spread_mode : mode
{
    mode_id id() const override { return mode_id::apps_spread; }
    mode_id classify(double g) const override;
    desired want() const override { return {true, true, true, true}; }
    bool slides() const override { return true; }
    void on_button(controller& c, const wlr_pointer_button_event& ev) override;
    void slide_settle(controller& c, bool committed) override;
};

struct workspaces_spread_mode : mode
{
    mode_id id() const override { return mode_id::workspaces_spread; }
    mode_id classify(double g) const override;
    desired want() const override { return {true, true, true, true}; }
    void on_button(controller& c, const wlr_pointer_button_event& ev) override;
    void on_motion(controller& c) override;
};
}
