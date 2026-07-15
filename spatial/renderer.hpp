#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <wayfire/geometry.hpp>
#include <wayfire/output.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/view-transform.hpp>

#include "coords.hpp"

namespace spatial
{
struct render_state
{
    double      g = 0.0;
    wf::point_t pan_dir{0, 0};
    double      pan_amount = 0.0;
    wf::point_t hover{-1, -1};
};

class backdrop_node_t;

class spread_t
{
  public:
    explicit spread_t(wf::output_t *output);
    ~spread_t();

    void ensure_layout(const frame_ctx& ctx, const std::vector<std::string>& filter);
    void relayout(const frame_ctx& ctx, const std::vector<std::string>& filter);
    void render(const frame_ctx& ctx, const render_state& state);
    void clear();

    void forget(wayfire_toplevel_view view);
    void release_for_drag(wayfire_toplevel_view view);
    wayfire_toplevel_view view_at(wf::pointf_t local) const;
    wf::geometry_t thumb_of(wayfire_toplevel_view view) const;

  private:
    struct view_data
    {
        std::shared_ptr<wf::scene::view_2d_transformer_t> tr;
        wf::point_t    cell{0, 0};
        wf::geometry_t expose{};
        wf::geometry_t screen{};
    };

    std::shared_ptr<wf::scene::view_2d_transformer_t> ensure_transformer(
        wayfire_toplevel_view view);
    void ensure_backdrop();
    void remove_backdrop();
    void clear_windows();
    void layout(const frame_ctx& ctx, const std::vector<std::string>& filter);
    void layout_cell(wf::point_t cell, std::vector<wayfire_toplevel_view>& cell_views,
        wf::geometry_t area);
    void place(const frame_ctx& ctx, const render_state& state);

    wf::output_t *output;
    std::map<wayfire_toplevel_view, view_data> views;
    std::vector<wayfire_toplevel_view> hidden_views;
    std::shared_ptr<backdrop_node_t> backdrop;
    wf::point_t laid_out_ws{-1, -1};
};
}
