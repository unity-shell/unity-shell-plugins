#pragma once

#include <string>
#include <string_view>

namespace wfgs
{
std::string to_key(std::string_view wf_option_name);
const char *gvariant_type(std::string_view wf_option_type);
}
