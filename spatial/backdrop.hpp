#pragma once

#include <memory>
#include <string>
#include <vector>

#include <wayfire/core.hpp>
#include <wayfire/output.hpp>
#include <wayfire/render.hpp>
#include <wayfire/scene.hpp>
#include <wayfire/scene-render.hpp>
#include <wayfire/view-helpers.hpp>
#include <wayfire/config/types.hpp>
#include <wayfire/option-wrapper.hpp>

#include "coords.hpp"
#include "config.hpp"
#include "rounded.hpp"

namespace spatial
{
/* Scene stream node that captures the background/bottom layers into a texture,
 * so the live wallpaper can be re-drawn as the workspace cards. */
class wallpaper_stream_t : public wf::scene::node_t
{
    class instance_t : public wf::scene::render_instance_t
    {
        wallpaper_stream_t *self;
        std::vector<wf::scene::render_instance_uptr> children;

      public:
        instance_t(wallpaper_stream_t *node, const wf::scene::damage_callback& push) :
            self(node)
        {
            for (auto& view : wf::collect_views_from_output(self->output,
                {wf::scene::layer::BACKGROUND, wf::scene::layer::BOTTOM}))
            {
                view->get_transformed_node()->gen_render_instances(children, push, self->output);
            }
        }

        void schedule_instructions(std::vector<wf::scene::render_instruction_t>& instructions,
            const wf::render_target_t& target, wf::regionf_t& damage) override
        {
            auto bbox = self->get_bounding_box();
            auto ours = damage & bbox;
            if (ours.empty()) { return; }

            for (auto& c : children) { c->schedule_instructions(instructions, target, ours); }
            damage ^= bbox;
            instructions.push_back({.instance = this, .target = target, .damage = ours});
        }

        void render(const wf::scene::render_instruction_t& data) override
        {
            static wf::option_wrapper_t<wf::color_t> bg{"core/background_color"};
            data.pass->clear(data.damage, bg);
        }

        void presentation_feedback(wf::output_t *o) override
        {
            for (auto& c : children) { c->presentation_feedback(o); }
        }

        void compute_visibility(wf::output_t *o, wf::regionf_t& visible) override
        {
            wf::scene::compute_visibility_from_list(children, o, visible, {0, 0});
        }
    };

  public:
    wf::output_t *output;

    explicit wallpaper_stream_t(wf::output_t *o) : node_t(false), output(o) {}

    wf::geometry_t get_bounding_box() override { return output->get_relative_geometry(); }

    void gen_render_instances(std::vector<wf::scene::render_instance_uptr>& instances,
        wf::scene::damage_callback push, wf::output_t *) override
    {
        instances.push_back(std::make_unique<instance_t>(this, push));
    }

    std::string stringify() const override { return "spatial-wallpaper"; }
};

/* The workspace cards: the captured wallpaper drawn once per grid cell, with
 * rounded corners, and a focus ring on the current workspace as the wall opens. */
class backdrop_node_t : public wf::scene::node_t
{
    class instance_t : public wf::scene::render_instance_t
    {
        std::shared_ptr<backdrop_node_t> self;
        std::vector<wf::scene::render_instance_uptr> bg;
        wf::scene::damage_callback push;
        wf::signal::connection_t<wf::scene::node_damage_signal> on_damage =
            [=] (wf::scene::node_damage_signal *ev) { push(ev->region); };

      public:
        instance_t(backdrop_node_t *n, const wf::scene::damage_callback& p) : push(p)
        {
            self = std::dynamic_pointer_cast<backdrop_node_t>(n->shared_from_this());
            self->connect(&on_damage);
            auto mark = [=] (const wf::regionf_t& d)
            {
                self->bg_damage |= d;
                push(self->get_bounding_box());
            };
            self->stream->gen_render_instances(bg, mark, self->output);
        }

        void schedule_instructions(std::vector<wf::scene::render_instruction_t>& instructions,
            const wf::render_target_t& target, wf::regionf_t& damage) override
        {
            if (!self->bg_damage.empty())
            {
                wf::render_target_t bt{self->buffer};
                bt.geometry = self->stream->get_bounding_box();
                bt.scale    = self->output->handle->scale;

                wf::render_pass_params_t p;
                p.instances        = &bg;
                p.damage           = self->bg_damage;
                p.reference_output = self->output;
                p.target           = bt;
                p.flags            = wf::RPASS_EMIT_SIGNALS;
                wf::render_pass_t::run(p);
                self->bg_damage.clear();
            }

            auto bbox = self->get_bounding_box();
            instructions.push_back({.instance = this, .target = target, .damage = damage & bbox});
            damage ^= bbox;
        }

        void render(const wf::scene::render_instruction_t& data) override
        {
            data.pass->clear(data.damage, wall_gap_color());

            auto ctx = make_frame_ctx(self->output);
            const auto bufsz = self->buffer.get_size();
            /* The focus ring fades in with the wall (absent on the app spread). */
            const float ring = (float) std::clamp(self->g - 1.0, 0.0, 1.0);

            for (int i = 0; i < ctx.grid.width; i++)
            {
                for (int j = 0; j < ctx.grid.height; j++)
                {
                    auto card = coords::cell_or_pane(ctx, i, j, self->g,
                        self->pan_dir, self->pan_amount);

                    if ((card.x >= ctx.output.width) || (card.y >= ctx.output.height) ||
                        (card.x + card.width <= 0) || (card.y + card.height <= 0))
                    {
                        continue;
                    }

                    auto tex = wf::texture_t::from_aux(self->buffer);
                    tex->set_filter_mode(WLR_SCALE_FILTER_BILINEAR);
                    tex->set_source_box(wlr_fbox{0.0, 0.0, (double) bufsz.width, (double) bufsz.height});

                    /* Cover-crop the wallpaper to the card's aspect so it never
                     * distorts; the crop opens to the full buffer as the card
                     * reaches the output-aspect wall cell. */
                    const double car = (double) card.width / std::max(1.0, (double) card.height);
                    const double bar = (double) bufsz.width / std::max(1, bufsz.height);
                    wf::pointf_t uv_scale{1.0, 1.0}, uv_off{0.0, 0.0};
                    if (car > bar) { uv_scale.y = bar / car; uv_off.y = (1.0 - uv_scale.y) / 2.0; }
                    else           { uv_scale.x = car / bar; uv_off.x = (1.0 - uv_scale.x) / 2.0; }

                    self->rounder.render(*data.pass, data.target, tex, card,
                        (float) CARD_CORNER_RADIUS, data.damage, uv_scale, uv_off);

                    if ((ring > 0.0f) && (i == ctx.cur_ws.x) && (j == ctx.cur_ws.y))
                    {
                        self->rounder.render_ring(*data.pass, data.target, card,
                            (float) CARD_CORNER_RADIUS, ring, data.damage);
                    }
                }
            }
        }

        void compute_visibility(wf::output_t *o, wf::regionf_t&) override
        {
            wf::regionf_t r = self->stream->get_bounding_box();
            for (auto& c : bg) { c->compute_visibility(o, r); }
        }
    };

  public:
    wf::output_t *output;
    double g = 0.0;
    wf::point_t  pan_dir{0, 0};
    double       pan_amount = 0.0;
    std::shared_ptr<wallpaper_stream_t> stream;
    wf::auxilliary_buffer_t buffer;
    wf::regionf_t bg_damage;
    rounded_pass_t rounder;   /* SDF rounded-corner blit + focus ring for the cards */

    explicit backdrop_node_t(wf::output_t *o) :
        node_t(false), output(o), stream(std::make_shared<wallpaper_stream_t>(o))
    {
        auto bbox = stream->get_bounding_box();
        buffer.allocate(wf::dimensions(bbox), o->handle->scale,
            wf::buffer_allocation_hints_t{.needs_alpha = false});
        bg_damage |= bbox;
    }

    void update(double g_, wf::point_t dir, double amount)
    {
        g = g_;
        pan_dir = dir;
        pan_amount = amount;
        wf::scene::damage_node(shared_from_this(), get_bounding_box());
    }

    void gen_render_instances(std::vector<wf::scene::render_instance_uptr>& instances,
        wf::scene::damage_callback push, wf::output_t *shown_on) override
    {
        if (shown_on != output) { return; }
        instances.push_back(std::make_unique<instance_t>(this, push));
    }

    wf::geometry_t get_bounding_box() override { return output->get_layout_geometry(); }

    std::string stringify() const override { return "spatial-backdrop"; }
};
}
