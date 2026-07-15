#include "gsettings-mapping.hpp"

#include <cctype>

namespace wfgs
{
std::string to_key(std::string_view name)
{
    std::string k;
    k.reserve(name.size());
    bool prev_dash = false;
    for (char c : name)
    {
        if (c == '_')
        {
            c = '-';
        }

        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        c = ok ? c : '-';

        if (c == '-')
        {
            if (!prev_dash && !k.empty())
            {
                k += '-';
            }

            prev_dash = true;
        } else
        {
            k += c;
            prev_dash = false;
        }
    }

    while (!k.empty() && k.back() == '-')
    {
        k.pop_back();
    }

    if (k.empty() || !(k.front() >= 'a' && k.front() <= 'z'))
    {
        k = "x-" + k;
    }

    return k;
}

const char *gvariant_type(std::string_view type)
{
    if (type == "int")
    {
        return "i";
    }

    if (type == "double")
    {
        return "d";
    }

    if (type == "bool")
    {
        return "b";
    }

    if (type == "dynamic-list")
    {
        return "aas";
    }

    return "s";
}
}
