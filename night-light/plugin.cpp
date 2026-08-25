#include "ramp.hpp"
#include "schedule.hpp"

#include <ctime>
#include <optional>
#include <string>
#include <vector>

#include <wayfire/core.hpp>
#include <wayfire/nonstd/wlroots-full.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/output.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/util.hpp>
#include <wayfire/util/log.hpp>

namespace night_light
{
namespace
{
/* Once a minute is finer than any transition the options can ask for. */
constexpr uint32_t tick_ms = 60000;

/* Wayfire clears a departed client's ramp on its next frame, so a write now
 * would be thrown away. One frame is enough; this leaves room for the repaint
 * delay as well. */
constexpr uint32_t reclaim_delay_ms = 250;

local_time read_clock()
{
    const std::time_t stamp = std::time(nullptr);
    std::tm tm{};
    localtime_r(&stamp, &tm);

    local_time at;
    at.year   = tm.tm_year + 1900;
    at.month  = tm.tm_mon + 1;
    at.day    = tm.tm_mday;
    at.hours  = tm.tm_hour + tm.tm_min / 60.0 + tm.tm_sec / 3600.0;
    at.utc_offset = double(tm.tm_gmtoff) / 3600.0;
    return at;
}

/* True while a client holds zwlr_gamma_control_v1 for @handle and has actually
 * written a table. Holding the object without writing does not count: a client
 * that only probes gamma would otherwise keep the output forever. */
bool client_owns_gamma(wlr_output *handle)
{
    auto *control = wlr_gamma_control_manager_v1_get_control(
        wf::get_core().protocols.gamma_v1, handle);
    return control && control->table;
}
}

class output_ramp : public wf::per_output_plugin_instance_t
{
  public:
    void init() override
    {
        output->connect(&on_config_changed);
    }

    void fini() override
    {
        /* Unloading must not stomp a client that owns the output. */
        if (!yielded)
        {
            write(neutral_kelvin);
        }
    }

    void apply(double kelvin)
    {
        wanted = kelvin;
        if (yielded || (written && (*written == kelvin)))
        {
            return;
        }

        if (write(kelvin))
        {
            written = kelvin;
        }
    }

    void yield_to_client()
    {
        if (yielded)
        {
            return;
        }

        yielded = true;
        written.reset();
        LOGI("night-light: a client took gamma control of ", output->to_string(),
            ", leaving that output to it");
    }

    void take_back()
    {
        if (!yielded)
        {
            return;
        }

        yielded = false;
        LOGI("night-light: gamma control of ", output->to_string(), " is free again");
        write_soon();
    }

  private:
    bool write(double kelvin)
    {
        wlr_output *handle = output->handle;
        if (!handle->enabled)
        {
            /* Off a tty, or asleep. The next tick retries. */
            return false;
        }

        const size_t size = wlr_output_get_gamma_size(handle);
        if (size == 0)
        {
            if (!no_gamma_logged)
            {
                LOGI("night-light: ", output->to_string(),
                    " has no gamma ramp, leaving it untouched");
                no_gamma_logged = true;
            }

            return false;
        }

        const whitepoint point = whitepoint_for(kelvin);
        std::vector<uint16_t> ramp;
        wlr_color_transform *lut = nullptr;

        /* A null transform is the identity, and lets the driver drop the LUT
         * rather than upload a table that changes nothing. */
        if (!is_neutral(point))
        {
            ramp = make_ramp(size, point);
            lut  = wlr_color_transform_init_lut_3x1d(size, ramp.data(),
                ramp.data() + size, ramp.data() + 2 * size);
            if (!lut)
            {
                LOGE("night-light: could not build a ramp for ", output->to_string());
                return false;
            }
        }

        wlr_output_state state;
        wlr_output_state_init(&state);
        wlr_output_state_set_color_transform(&state, lut);
        const bool ok = wlr_output_test_state(handle, &state) &&
            wlr_output_commit_state(handle, &state);
        wlr_output_state_finish(&state);
        if (lut)
        {
            wlr_color_transform_unref(lut);
        }

        if (!ok)
        {
            if (!rejected_logged)
            {
                LOGE("night-light: ", output->to_string(), " rejected the gamma ramp");
                rejected_logged = true;
            }

            return false;
        }

        rejected_logged = false;
        return true;
    }

    void write_soon()
    {
        written.reset();
        retry.set_timeout(reclaim_delay_ms, [this] ()
        {
            apply(wanted);
        });
    }

    double wanted = neutral_kelvin;
    std::optional<double> written;
    bool yielded = false;
    bool no_gamma_logged = false;
    bool rejected_logged = false;

    wf::wl_timer<false> retry;

    /* A mode set or a CRTC reassignment can drop the ramp the CRTC was holding,
     * and re-committing from inside the handler would re-enter output-layout. */
    wf::signal::connection_t<wf::output_configuration_changed_signal>
    on_config_changed = [this] (wf::output_configuration_changed_signal *ev)
    {
        if (ev->changed_fields & (wf::OUTPUT_SOURCE_CHANGE | wf::OUTPUT_MODE_CHANGE))
        {
            write_soon();
        }
    };
};

class plugin : public wf::plugin_interface_t,
    public wf::per_output_tracker_mixin_t<output_ramp>
{
  public:
    void init() override
    {
        /* Every write lands here within the config backend's debounce, so there
         * is no separate reload path to hook. wf-config fires one callback per
         * key, and a settings page that rewrites nine of them would otherwise
         * cost nine blocking commits per output, so fold them into one. */
        const auto reload = [this] ()
        {
            reload_idle.run_once([this] ()
            {
                refresh(true);
                arm_tick();
            });
        };
        enabled.set_callback(reload);
        temperature.set_callback(reload);
        day_temperature.set_callback(reload);
        schedule.set_callback(reload);
        from.set_callback(reload);
        to.set_callback(reload);
        latitude.set_callback(reload);
        longitude.set_callback(reload);
        transition_duration.set_callback(reload);

        on_gamma_changed.set_callback([this] (void *data)
        {
            auto *ev = static_cast<const wlr_gamma_control_manager_v1_set_gamma_event*>(data);
            auto *wo = wf::get_core().output_layout->find_output(ev->output);
            auto it  = wo ? output_instance.find(wo) : output_instance.end();
            if (it == output_instance.end())
            {
                return;
            }

            if (client_owns_gamma(ev->output))
            {
                it->second->yield_to_client();
            } else
            {
                it->second->take_back();
            }
        });

        on_gamma_changed.connect(&wf::get_core().protocols.gamma_v1->events.set_gamma);

        /* Compute the target first. init_output_tracking() then writes each
         * output once, instead of writing neutral and correcting it. */
        refresh(true);
        init_output_tracking();
        arm_tick();
    }

    void fini() override
    {
        tick.disconnect();
        on_gamma_changed.disconnect();
        fini_output_tracking();
    }

  private:
    /* A new output starts at the current target, not at neutral. */
    void handle_new_output(wf::output_t *wo) override
    {
        wf::per_output_tracker_mixin_t<output_ramp>::handle_new_output(wo);

        output_ramp *ramp = output_instance[wo].get();
        if (client_owns_gamma(wo->handle))
        {
            ramp->yield_to_client();
        }

        ramp->apply(kelvin);
    }

    settings read_settings()
    {
        settings cfg;
        cfg.enabled = enabled;
        cfg.night_kelvin = temperature;
        cfg.day_kelvin   = day_temperature;
        if (const auto mode = parse_mode(schedule.value()))
        {
            cfg.mode = *mode;
            bad_schedule.clear();
        } else if (bad_schedule != schedule.value())
        {
            /* Named once per bad value, since this runs on every tick. */
            bad_schedule = schedule.value();
            LOGE("night-light: bad schedule in config: ", bad_schedule);
        }

        cfg.from = from;
        cfg.to   = to;
        cfg.latitude  = latitude;
        cfg.longitude = longitude;
        cfg.transition_seconds = transition_duration;
        return cfg;
    }

    /* @recompute drops the cached sun times, for a settings change that may have
     * moved the location. */
    void refresh(bool recompute)
    {
        const settings cfg = read_settings();
        const local_time at = read_clock();
        const int date = at.year * 10000 + at.month * 100 + at.day;

        /* Only one schedule reads the sun, and only with a location set.
         * Anything else must not pay libnova, or hold times nothing goes on to
         * use. */
        const bool needs_sun = cfg.enabled &&
            (cfg.mode == schedule_mode::sunset_to_sunrise) && !location_unset(cfg);

        if (!needs_sun)
        {
            sun = {};
            sun_date = 0;
        } else if (recompute || (date != sun_date) || (at.utc_offset != sun_offset))
        {
            /* Reading the wall clock each time means a resume from suspend needs
             * no elapsed-time arithmetic. */
            sun = sun_times_for(cfg.latitude, cfg.longitude, at);
            sun_date = date;
            sun_offset = at.utc_offset;
        }

        kelvin = target_kelvin(cfg, at, sun);
        for (auto& [wo, ramp] : output_instance)
        {
            ramp->apply(kelvin);
        }
    }

    void arm_tick()
    {
        if (enabled && !tick.is_connected())
        {
            tick.set_timeout(tick_ms, [this] ()
            {
                refresh(false);

                /* Hold no timer while off. reload re-arms it. */
                return enabled.value();
            });
        }
    }

    wf::option_wrapper_t<bool> enabled{"night-light/enabled"};
    wf::option_wrapper_t<int> temperature{"night-light/temperature"};
    wf::option_wrapper_t<int> day_temperature{"night-light/day-temperature"};
    wf::option_wrapper_t<std::string> schedule{"night-light/schedule"};
    wf::option_wrapper_t<double> from{"night-light/from"};
    wf::option_wrapper_t<double> to{"night-light/to"};
    wf::option_wrapper_t<double> latitude{"night-light/latitude"};
    wf::option_wrapper_t<double> longitude{"night-light/longitude"};
    wf::option_wrapper_t<int> transition_duration{"night-light/transition-duration"};

    double kelvin = neutral_kelvin;
    std::string bad_schedule;

    /* Cached sun times are good for one local day at one UTC offset. A DST
     * switch moves sunset mid-day, so the offset belongs in the key. */
    sun_times sun;
    int sun_date = 0;
    double sun_offset = 0.0;

    wf::wl_timer<true> tick;
    wf::wl_idle_call reload_idle;
    wf::wl_listener_wrapper on_gamma_changed;
};
}

DECLARE_WAYFIRE_PLUGIN(night_light::plugin);
