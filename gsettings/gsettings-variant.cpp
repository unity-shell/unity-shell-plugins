#include "gsettings-variant.hpp"

#include <wayfire/config/compound-option.hpp>
#include <wayfire/config/option.hpp>

#include <string>
#include <vector>

using namespace wf::config;

namespace
{
template<class T>
std::shared_ptr<option_t<T>> as(const std::shared_ptr<option_base_t>& o)
{
    return std::dynamic_pointer_cast<option_t<T>>(o);
}
}

namespace wfgs
{
GVariant *option_to_variant(const std::shared_ptr<option_base_t>& o)
{
    if (auto c = std::dynamic_pointer_cast<compound_option_t>(o))
    {
        GVariantBuilder rows;
        g_variant_builder_init(&rows, G_VARIANT_TYPE("aas"));
        for (auto& row : c->get_value_untyped())
        {
            GVariantBuilder fields;
            g_variant_builder_init(&fields, G_VARIANT_TYPE("as"));
            for (auto& s : row)
            {
                g_variant_builder_add(&fields, "s", s.c_str());
            }

            g_variant_builder_add_value(&rows, g_variant_builder_end(&fields));
        }

        return g_variant_builder_end(&rows);
    }

    if (auto i = as<int>(o))
    {
        return g_variant_new_int32(i->get_value());
    }

    if (auto d = as<double>(o))
    {
        return g_variant_new_double(d->get_value());
    }

    if (auto b = as<bool>(o))
    {
        return g_variant_new_boolean(b->get_value());
    }

    return g_variant_new_string(o->get_value_str().c_str());
}

bool apply_to_option(const std::shared_ptr<option_base_t>& o, GVariant *v)
{
    if (g_variant_is_of_type(v, G_VARIANT_TYPE("aas")))
    {
        auto c = std::dynamic_pointer_cast<compound_option_t>(o);
        if (!c)
        {
            return false;
        }

        compound_option_t::stored_type_t rows;
        GVariantIter outer;
        g_variant_iter_init(&outer, v);
        GVariant *row;
        while ((row = g_variant_iter_next_value(&outer)))
        {
            std::vector<std::string> fields;
            GVariantIter inner;
            g_variant_iter_init(&inner, row);
            const char *s;
            while (g_variant_iter_loop(&inner, "&s", &s))
            {
                fields.emplace_back(s);
            }

            rows.push_back(std::move(fields));
            g_variant_unref(row);
        }

        return c->set_value_untyped(std::move(rows));
    }

    if (g_variant_is_of_type(v, G_VARIANT_TYPE_INT32))
    {
        auto i = as<int>(o);
        if (!i)
        {
            return false;
        }

        i->set_value(g_variant_get_int32(v));
        return true;
    }

    if (g_variant_is_of_type(v, G_VARIANT_TYPE_DOUBLE))
    {
        auto d = as<double>(o);
        if (!d)
        {
            return false;
        }

        d->set_value(g_variant_get_double(v));
        return true;
    }

    if (g_variant_is_of_type(v, G_VARIANT_TYPE_BOOLEAN))
    {
        auto b = as<bool>(o);
        if (!b)
        {
            return false;
        }

        b->set_value(g_variant_get_boolean(v));
        return true;
    }

    if (g_variant_is_of_type(v, G_VARIANT_TYPE_STRING))
    {
        return o->set_value_str(g_variant_get_string(v, nullptr));
    }

    return false;
}
}
