#pragma once

#include <gio/gio.h>

#include <memory>

namespace wf::config { class option_base_t; }

namespace wfgs
{
GVariant *option_to_variant(const std::shared_ptr<wf::config::option_base_t>& option);
bool apply_to_option(const std::shared_ptr<wf::config::option_base_t>& option, GVariant *value);
}
