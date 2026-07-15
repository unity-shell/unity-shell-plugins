#pragma once

#include <string>
#include <string_view>

namespace wfgs
{
/**
 * Converts a Wayfire option name into a valid GSettings key name.
 */
std::string to_key(std::string_view wf_option_name);

/**
 * Maps a Wayfire option type to a GVariant type signature.
 */
const char *gvariant_type(std::string_view wf_option_type);
}
