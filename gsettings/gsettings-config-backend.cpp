#include "gsettings-glib-source.hpp"
#include "gsettings-mapping.hpp"
#include "gsettings-seed.hpp"
#include "gsettings-variant.hpp"

#include <wayfire/config-backend.hpp>
#include <wayfire/config/config-manager.hpp>
#include <wayfire/config/file.hpp>
#include <wayfire/config/option.hpp>
#include <wayfire/config/section.hpp>
#include <wayfire/core.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/util.hpp>

#include <gio/gio.h>

#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

using namespace wf::config;

using wfgs::seeder_fn;

namespace
{
/* Custom deleter for owning GSettings handles in std::unique_ptr. */
struct gsettings_deleter
{
    void operator()(GSettings *gs) const
    {
        g_object_unref(gs);
    }
};

using gsettings_ptr = std::unique_ptr<GSettings, gsettings_deleter>;

/* Re-entrancy guard to avoid feedback loops while syncing settings. */
struct guard
{
    bool& flag;
    explicit guard(bool& f) : flag(f)
    {
        flag = true;
    }

    ~guard()
    {
        flag = false;
    }
};
}

/* Wayfire configuration backend backed by org.wayfire.* GSettings schemas. */
class gsettings_config_t : public wf::config_backend_t
{
  public:
    void init(wl_display *display, config_manager_t& config,
        const std::string&) override
    {
        config = wf::config::build_configuration(get_xml_dirs(), "", "");
        cfg = &config;
        loop = std::make_unique<wfgs::glib_source_t>(wl_display_get_event_loop(display));

        for (auto& section : config.get_all_sections())
        {
            bind_section(section->get_name(), section);
        }
    }

    std::shared_ptr<section_t> get_output_section(wlr_output *output) override
    {
        return relocatable_section("output", output->name ? output->name : "",
            wfgs::output_seeder(output));
    }

    std::shared_ptr<section_t> get_input_device_section(const std::string& prefix,
        wlr_input_device *device) override
    {
        if (prefix == "input")
        {
            return cfg->get_section("input");
        }

        return relocatable_section(prefix, device->name ? device->name : "");
    }

    ~gsettings_config_t() override
    {
        /* Drain queued writes and stop all callbacks before tearing down the
         * GSettings objects and the GLib<->wl bridge, so dconf's async writer is
         * not left serializing into a half-closed D-Bus connection. */
        g_settings_sync();

        for (auto& b : bindings)
        {
            if (b.option)
            {
                b.option->rem_updated_handler(&b.on_changed);
            }
        }

        for (auto& gs : settings_objects)
        {
            g_signal_handlers_disconnect_by_data(gs.get(), this);
        }

        settings_objects.clear();
        loop.reset();
    }

  private:
    /* Synchronization record for one Wayfire option and one schema key. */
    struct binding_t
    {
        std::shared_ptr<option_base_t> option;
        GSettings *settings = nullptr;
        std::string key;
        option_base_t::updated_callback_t on_changed;
    };

    std::shared_ptr<section_t> relocatable_section(const std::string& prefix, const std::string& key,
        const seeder_fn& seeder = {})
    {
        const std::string name = prefix + ":" + key;
        if (!cfg->get_section(name))
        {
            if (auto tmpl = cfg->get_section(prefix))
            {
                cfg->merge_section(tmpl->clone_with_name(name));
            }
        }

        auto section = cfg->get_section(name);
        if (section && bound_relocatable.insert(name).second)
        {
            const std::string path = "/org/wayfire/" + prefix + "/" + key + "/";
            bind_section(prefix, section, &path, seeder);
        }

        return section;
    }

    void bind_section(const std::string& suffix, const std::shared_ptr<section_t>& section,
        const std::string *reloc_path = nullptr, const seeder_fn& seeder = {})
    {
        GSettingsSchemaSource *source = g_settings_schema_source_get_default();
        if (!source)
        {
            return;
        }

        const std::string id = "org.wayfire." + suffix;
        g_autoptr(GSettingsSchema) schema =
            g_settings_schema_source_lookup(source, id.c_str(), TRUE);
        if (!schema)
        {
            return;
        }

        const bool relocatable = g_settings_schema_get_path(schema) == nullptr;
        if (relocatable != (reloc_path != nullptr))
        {
            return;
        }

        GSettings *gs = reloc_path
            ? g_settings_new_full(schema, nullptr, reloc_path->c_str())
            : g_settings_new_full(schema, nullptr, nullptr);
        settings_objects.emplace_back(gs);

        g_signal_connect(gs, "changed",
            G_CALLBACK(&gsettings_config_t::on_gsettings_changed), this);

        for (auto& option : section->get_registered_options())
        {
            bind_option(gs, schema, option, seeder);
        }
    }

    void bind_option(GSettings *gs, GSettingsSchema *schema,
        const std::shared_ptr<option_base_t>& option, const seeder_fn& seeder)
    {
        const std::string key = wfgs::to_key(option->get_name());
        if (!g_settings_schema_has_key(schema, key.c_str()))
        {
            return;
        }

        /* Apply the effective value: a user value if set, otherwise the schema
         * default (which carries the session's gschema override, e.g. the unity
         * autostart / plugin list). This is NOT the metadata default, so it must
         * be applied rather than assumed a no-op. */
        {
            g_autoptr(GVariant) v = g_settings_get_value(gs, key.c_str());
            wfgs::apply_to_option(option, v);
        }

        /* Materialize a seeded section (a relocatable output) on attach: write
         * every key that has no user value yet, so the whole section shows up
         * visible and editable in dconf the moment the output is connected. The
         * seeder supplies a computed override for specific keys (e.g. the scale);
         * the rest are written with the effective value applied above. */
        if (seeder)
        {
            g_autoptr(GVariant) user = g_settings_get_user_value(gs, key.c_str());
            if (!user)
            {
                guard g{syncing};
                if (auto computed = seeder(key))
                {
                    option->set_value_str(*computed);
                }

                /* g_settings_set_value consumes the floating reference returned
                 * by option_to_variant; wrapping it in g_autoptr would add a
                 * second unref that frees it under dconf's async writer thread. */
                g_settings_set_value(gs, key.c_str(), wfgs::option_to_variant(option));
            }
        }

        binding_t& b = bindings.emplace_back();
        b.option   = option;
        b.settings = gs;
        b.key      = key;
        b.on_changed = [this, &b]
        {
            if (syncing)
            {
                return;
            }

            guard g{syncing};
            g_settings_set_value(b.settings, b.key.c_str(), wfgs::option_to_variant(b.option));
        };
        option->add_updated_handler(&b.on_changed);
    }

    static void on_gsettings_changed(GSettings *gs, const char *key, gpointer data)
    {
        auto *self = static_cast<gsettings_config_t*>(data);
        if (self->syncing)
        {
            return;
        }

        guard g{self->syncing};
        for (auto& b : self->bindings)
        {
            if (b.settings == gs && b.key == key)
            {
                g_autoptr(GVariant) v = g_settings_get_value(gs, key);
                if (wfgs::apply_to_option(b.option, v))
                {
                    self->schedule_reload();
                }

                return;
            }
        }
    }

    void schedule_reload()
    {
        reload_timer.set_timeout(50, []
        {
            wf::reload_config_signal ev;
            wf::get_core().emit(&ev);
        });
    }

    config_manager_t *cfg = nullptr;
    std::unique_ptr<wfgs::glib_source_t> loop;
    wf::wl_timer<false> reload_timer;
    std::deque<binding_t> bindings;
    std::vector<gsettings_ptr> settings_objects;
    std::set<std::string> bound_relocatable;
    bool syncing = false;
};

DECLARE_WAYFIRE_CONFIG_BACKEND(gsettings_config_t);
