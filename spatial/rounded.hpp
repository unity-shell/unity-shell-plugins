#pragma once

#include <memory>

#include <wayfire/geometry.hpp>
#include <wayfire/render.hpp>
#include <wayfire/config/types.hpp>

namespace spatial
{
/*
 * Draws a texture into a rectangle with rounded corners, cut per-pixel by a
 * signed-distance field in the fragment shader. Unlike a raster corner mask this
 * stays sharp at any output scale, is anti-aliased, and has no
 * premultiplied-alpha fringing.
 *
 * One instance owns the GL program. It compiles on first render and frees on
 * destruction. Falls back to a plain square blit when the renderer is not GLES,
 * so the card still draws.
 */
class rounded_pass_t
{
  public:
    rounded_pass_t();
    ~rounded_pass_t();
    rounded_pass_t(rounded_pass_t&&) noexcept;
    rounded_pass_t& operator =(rounded_pass_t&&) noexcept;
    rounded_pass_t(const rounded_pass_t&)            = delete;
    rounded_pass_t& operator =(const rounded_pass_t&) = delete;

    /**
     * Blit @texture into logical rect @box on @target with corners rounded to
     * @radius logical px, clipped to @damage. @texture is expected to be opaque
     * and cover @box (uv 0..1).
     */
    void render(wf::render_pass_t& pass, const wf::render_target_t& target,
        const std::shared_ptr<wf::texture_t>& texture, const wf::geometry_t& box,
        float radius, const wf::regionf_t& damage,
        const wf::pointf_t& uv_scale = {1.0, 1.0}, const wf::pointf_t& uv_off = {0.0, 0.0});

    /*
     * Draw a rounded-rect ring around @box (corner @radius) in @color: a stroke
     * of @width logical px starting @gap px outside the card edge, at @opacity.
     */
    void render_ring(wf::render_pass_t& pass, const wf::render_target_t& target,
        const wf::geometry_t& box, float radius, float gap, float width,
        float opacity, const wf::color_t& color, const wf::regionf_t& damage);

  private:
    struct impl;
    std::unique_ptr<impl> priv;
};
}
