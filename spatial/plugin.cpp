#include "controller.hpp"

#include <string>
#include <vector>

#include <wayfire/core.hpp>
#include <wayfire/seat.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/plugins/ipc/ipc-method-repository.hpp>
#include <wayfire/plugins/ipc/ipc-activator.hpp>
#include <wayfire/plugins/common/shared-core-data.hpp>

namespace spatial
{
class plugin : public wf::plugin_interface_t,
    public wf::per_output_tracker_mixin_t<controller>
{
  public:
    void init() override
    {
        init_output_tracking();

        windows_spread.set_handler([this] (wf::output_t *o, wayfire_view)
        {
            if (auto c = controller_for(o)) { c->toggle_apps_spread(); }
            return true;
        });
        workspaces_spread.set_handler([this] (wf::output_t *o, wayfire_view)
        {
            if (auto c = controller_for(o)) { c->toggle_workspaces_spread(); }
            return true;
        });

        ipc->register_method("spatial/spread-app", [this] (wf::json_t data) -> wf::json_t
        {
            std::vector<std::string> ids;
            if (data.has_member("app_ids") && data["app_ids"].is_array())
            {
                for (size_t i = 0; i < data["app_ids"].size(); i++)
                {
                    if (data["app_ids"][i].is_string()) { ids.push_back(data["app_ids"][i].as_string()); }
                }
            }

            if (auto c = active_controller()) { c->spread_app(ids); }
            return wf::json_t{};
        });

        ipc->register_method("spatial/close", [this] (wf::json_t) -> wf::json_t
        {
            if (auto c = active_controller()) { c->close_overview(); }
            return wf::json_t{};
        });

        ipc->register_method("spatial/inhibit", [this] (wf::json_t) -> wf::json_t
        {
            controller::inhibit();
            for (auto& [o, c] : output_instance) { c->close_overview(); }
            return wf::json_t{};
        });

        ipc->register_method("spatial/uninhibit", [] (wf::json_t) -> wf::json_t
        {
            controller::uninhibit();
            return wf::json_t{};
        });
    }

    void fini() override
    {
        fini_output_tracking();
        ipc->unregister_method("spatial/spread-app");
        ipc->unregister_method("spatial/close");
        ipc->unregister_method("spatial/inhibit");
        ipc->unregister_method("spatial/uninhibit");
    }

  private:
    controller *controller_for(wf::output_t *o)
    {
        auto it = output_instance.find(o);
        return (it != output_instance.end()) ? it->second.get() : nullptr;
    }

    controller *active_controller()
    {
        return controller_for(wf::get_core().seat->get_active_output());
    }

    wf::ipc_activator_t windows_spread{"spatial/windows-spread"};
    wf::ipc_activator_t workspaces_spread{"spatial/workspaces-spread"};
    wf::shared_data::ref_ptr_t<wf::ipc::method_repository_t> ipc;
};
}

DECLARE_WAYFIRE_PLUGIN(spatial::plugin);
