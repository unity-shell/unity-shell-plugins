#include "rounded.hpp"
#include "config.hpp"

#include <algorithm>

#include <wayfire/opengl.hpp>

/*
 * The only file that touches raw OpenGL: crisp rounded corners on a live texture
 * have to be done on the GPU (a shader plus a bind/uniform/draw handshake). It's
 * quarantined here behind a small interface, following Wayfire's own plugins
 * (squeezimize.hpp / cube.cpp).
 */

namespace spatial
{
static const char *vert_source =
    R"(
#version 100
attribute highp vec2 position;
attribute highp vec2 uv_in;
uniform mat4 matrix;
varying highp vec2 uv;
void main() {
    uv = uv_in;
    gl_Position = matrix * vec4(position, 0.0, 1.0);
}
)";

/* SDF of a rounded rectangle: negative inside, positive outside, |value| is the
 * distance in px. smoothstep across +/- feather gives a 1px anti-aliased edge.
 * Output is premultiplied (rgb and a scaled together) for OVER blending. */
static const char *frag_source =
    R"(
#version 100
@builtin_ext@
@builtin@

precision highp float;

varying highp vec2 uv;
uniform highp vec2 size;      /* box size, logical px */
uniform highp float radius;   /* corner radius, logical px */
uniform highp float feather;  /* AA half-width, logical px */
uniform highp vec2 uv_scale;  /* cover-crop of the source (1,1 = whole texture) */
uniform highp vec2 uv_off;

void main() {
    vec2 p = (uv - 0.5) * size;
    vec2 b = size * 0.5 - vec2(radius);
    vec2 q = abs(p) - b;
    float d = min(max(q.x, q.y), 0.0) + length(max(q, vec2(0.0))) - radius;
    float a = 1.0 - smoothstep(-feather, feather, d);
    /* Sample a cover-cropped sub-rect so the (output-aspect) wallpaper fills the
     * card without distortion, whatever the card's aspect. */
    gl_FragColor = get_pixel(uv_off + uv * uv_scale) * a;
}
)";

/* Focus ring: no texture, just a solid white rounded-rect band outside the card.
 * `d` is the signed distance to the card; the ring lives in [gap, gap+width].
 * `feather` (the AA half-width) is passed in physical px so the edge is equally
 * crisp at any output scale. */
static const char *ring_frag_source =
    R"(
#version 100
@builtin_ext@
@builtin@

precision highp float;

varying highp vec2 uv;
uniform highp vec2 span;      /* expanded quad size, logical px */
uniform highp vec2 half_box;  /* card half-size, logical px */
uniform highp float radius;   /* card corner radius, logical px */
uniform highp float gap;
uniform highp float width;
uniform highp float opacity;
uniform highp float feather;

void main() {
    vec2 p = (uv - 0.5) * span;
    vec2 q = abs(p) - (half_box - vec2(radius));
    float d = min(max(q.x, q.y), 0.0) + length(max(q, vec2(0.0))) - radius;

    float a = smoothstep(gap - feather, gap + feather, d) *
              (1.0 - smoothstep(gap + width - feather, gap + width + feather, d));

    gl_FragColor = vec4(a * opacity);   /* premultiplied white */
}
)";

struct rounded_pass_t::impl
{
    OpenGL::program_t program;       /* rounded texture blit */
    OpenGL::program_t ring;          /* focus ring */
    bool compiled      = false;
    bool ring_compiled = false;
};

namespace
{
/* The shared GPU handshake for both shaders: lazy-compile @prog, bind a
 * TRIANGLE_FAN quad (@pos) with the standard uv corners, let the caller set the
 * shader-specific uniforms, then premultiplied-blend it over @damage. Returns
 * false when the renderer is not GLES (the caller may fall back). */
template<class SetUniforms>
bool draw_quad(wf::render_pass_t& pass, const wf::render_target_t& target,
    OpenGL::program_t& prog, bool& compiled, const char *vs, const char *fs,
    const float pos[8], const wf::regionf_t& damage, SetUniforms&& set_uniforms)
{
    static const float uvd[] = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
    return pass.custom_gles_subpass(target, [&]
    {
        if (!compiled) { prog.compile(vs, fs); compiled = true; }
        prog.use(wf::TEXTURE_TYPE_RGBA);
        prog.uniformMatrix4f("matrix", wf::gles::render_target_orthographic_projection(target));
        prog.attrib_pointer("position", 2, 0, pos);
        prog.attrib_pointer("uv_in", 2, 0, uvd);
        set_uniforms();
        GL_CALL(glEnable(GL_BLEND));
        GL_CALL(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));
        wf::gles::for_each_scissor_rect(target, damage, [&]
        {
            GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));
        });
        prog.deactivate();
    });
}
}

rounded_pass_t::rounded_pass_t() : priv(std::make_unique<impl>()) {}

rounded_pass_t::~rounded_pass_t()
{
    if (priv && (priv->compiled || priv->ring_compiled))
    {
        wf::gles::run_in_context_if_gles([&]
        {
            if (priv->compiled) { priv->program.free_resources(); }
            if (priv->ring_compiled) { priv->ring.free_resources(); }
        });
    }
}

rounded_pass_t::rounded_pass_t(rounded_pass_t&&) noexcept            = default;
rounded_pass_t& rounded_pass_t::operator =(rounded_pass_t&&) noexcept = default;

void rounded_pass_t::render(wf::render_pass_t& pass, const wf::render_target_t& target,
    const std::shared_ptr<wf::texture_t>& texture, const wf::geometry_t& box,
    float radius, const wf::regionf_t& damage,
    const wf::pointf_t& uv_scale, const wf::pointf_t& uv_off)
{
    const float x = (float) box.x, y = (float) box.y;
    const float w = (float) box.width, h = (float) box.height;

    /* TRIANGLE_FAN quad (BL, BR, TR, TL). */
    const float pos[] = {x, y + h, x + w, y + h, x + w, y, x, y};
    auto tex = wf::gles_texture_t{texture};

    const bool ran = draw_quad(pass, target, priv->program, priv->compiled,
        vert_source, frag_source, pos, damage, [&]
    {
        priv->program.uniform2f("size", w, h);
        priv->program.uniform1f("radius", radius);
        priv->program.uniform1f("feather", 1.0f);
        priv->program.uniform2f("uv_scale", (float) uv_scale.x, (float) uv_scale.y);
        priv->program.uniform2f("uv_off", (float) uv_off.x, (float) uv_off.y);
        priv->program.set_active_texture(tex);
    });

    /* Non-GLES renderer (e.g. Vulkan): fall back to a plain square blit so the
     * card still shows. */
    if (!ran)
    {
        pass.add_texture(texture, target, box, damage);
    }
}

void rounded_pass_t::render_ring(wf::render_pass_t& pass, const wf::render_target_t& target,
    const wf::geometry_t& box, float radius, float opacity, const wf::regionf_t& damage)
{
    /* Expand the quad outward far enough to hold the gap + ring (+AA). */
    const float ext = FOCUS_RING_GAP + FOCUS_RING_WIDTH + 2.0f;
    const float x = (float) box.x - ext, y = (float) box.y - ext;
    const float w = (float) box.width + 2.0f * ext, h = (float) box.height + 2.0f * ext;

    const float pos[] = {x, y + h, x + w, y + h, x + w, y, x, y};

    /* One physical pixel of AA, expressed in the shader's logical-px space, so
     * crispness is independent of the output scale. */
    const float scale   = (target.scale > 0.0) ? (float) target.scale : 1.0f;
    const float feather = 1.0f / scale;

    draw_quad(pass, target, priv->ring, priv->ring_compiled,
        vert_source, ring_frag_source, pos, damage, [&]
    {
        priv->ring.uniform2f("span", w, h);
        priv->ring.uniform2f("half_box", (float) box.width * 0.5f, (float) box.height * 0.5f);
        priv->ring.uniform1f("radius", radius);
        priv->ring.uniform1f("gap", FOCUS_RING_GAP);
        priv->ring.uniform1f("width", FOCUS_RING_WIDTH);
        priv->ring.uniform1f("opacity", opacity);
        priv->ring.uniform1f("feather", feather);
    });
}
}
