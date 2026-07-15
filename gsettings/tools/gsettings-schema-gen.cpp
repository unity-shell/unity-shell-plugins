#include "gsettings-mapping.hpp"

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlwriter.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
struct Key
{
    std::string name;
    std::string gv;
    std::string def, min, max;
    std::string summary, description;
    std::vector<std::string> choices;
    bool skip = false;
};

struct Schema
{
    std::string name;
    bool relocatable = false;
    std::vector<Key> keys;
};

bool is_elem(const xmlNode *n, const char *name)
{
    return n->type == XML_ELEMENT_NODE && xmlStrcmp(n->name, BAD_CAST name) == 0;
}

std::string prop(xmlNode *n, const char *name)
{
    xmlChar *v = xmlGetProp(n, BAD_CAST name);
    std::string s = v ? reinterpret_cast<const char*>(v) : "";
    xmlFree(v);
    return s;
}

std::string child_text(xmlNode *parent, const char *tag)
{
    for (xmlNode *c = parent->children; c; c = c->next)
    {
        if (is_elem(c, tag))
        {
            xmlChar *t = xmlNodeGetContent(c);
            std::string s = t ? reinterpret_cast<const char*>(t) : "";
            xmlFree(t);
            return s;
        }
    }

    return "";
}

void add_options(xmlNode *parent, Schema& s)
{
    for (xmlNode *n = parent->children; n; n = n->next)
    {
        if (is_elem(n, "group"))
        {
            add_options(n, s);
            continue;
        }

        if (!is_elem(n, "option"))
        {
            continue;
        }

        Key k;
        k.name = prop(n, "name");
        const char *gv = wfgs::gvariant_type(prop(n, "type"));
        k.gv   = gv ? gv : "";
        k.skip = !gv || k.name.empty();
        if (!k.skip)
        {
            k.def         = child_text(n, "default");
            k.min         = child_text(n, "min");
            k.max         = child_text(n, "max");
            k.summary     = child_text(n, "_short");
            k.description = child_text(n, "_long");

            for (xmlNode *d = n->children; d; d = d->next)
            {
                if (is_elem(d, "desc"))
                {
                    std::string value = child_text(d, "value");
                    if (!value.empty())
                    {
                        k.choices.push_back(std::move(value));
                    }
                }
            }
        }

        s.keys.push_back(std::move(k));
    }
}

void parse_file(const std::string& path, std::vector<Schema>& out)
{
    xmlDoc *doc = xmlReadFile(path.c_str(), nullptr, XML_PARSE_NONET | XML_PARSE_NOBLANKS);
    if (!doc)
    {
        std::fprintf(stderr, "gsettings-schema-gen: %s: parse failed\n", path.c_str());
        return;
    }

    xmlNode *root = xmlDocGetRootElement(doc);
    for (xmlNode *sec = root ? root->children : nullptr; sec; sec = sec->next)
    {
        if (!is_elem(sec, "plugin") && !is_elem(sec, "object"))
        {
            continue;
        }

        Schema s;
        s.name = prop(sec, "name");
        s.relocatable = is_elem(sec, "object");
        add_options(sec, s);
        out.push_back(std::move(s));
    }

    xmlFreeDoc(doc);
}

void collect(const std::string& path, std::vector<Schema>& out)
{
    std::error_code ec;
    if (fs::is_regular_file(path, ec))
    {
        parse_file(path, out);
        return;
    }

    for (const auto& entry : fs::directory_iterator(path, ec))
    {
        if (entry.path().extension() == ".xml")
        {
            parse_file(entry.path().string(), out);
        }
    }

    if (ec)
    {
        std::fprintf(stderr, "gsettings-schema-gen: %s: %s\n", path.c_str(), ec.message().c_str());
    }
}

std::string oneline(const std::string& s)
{
    std::string out;
    bool space = false;
    for (char c : s)
    {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
        {
            space = true;
        } else
        {
            if (space && !out.empty())
            {
                out += ' ';
            }

            out += c;
            space = false;
        }
    }

    return out;
}

std::string gvariant_literal(const Key& k)
{
    if (k.gv == "aas")
    {
        return "[]";
    }

    if (k.gv == "b")
    {
        return (k.def == "true" || k.def == "1") ? "true" : "false";
    }

    if (k.gv == "i")
    {
        return k.def.empty() ? "0" : k.def;
    }

    if (k.gv == "d")
    {
        return k.def.empty() ? "0.0" : k.def;
    }

    std::string out = "'";
    for (char c : k.def)
    {
        if (c == '\\' || c == '\'')
        {
            out += '\\';
        }

        out += c;
    }

    out += "'";
    return out;
}

void attr(xmlTextWriterPtr w, const char *name, const std::string& value)
{
    xmlTextWriterWriteAttribute(w, BAD_CAST name, BAD_CAST value.c_str());
}

void element(xmlTextWriterPtr w, const char *name, const std::string& text)
{
    xmlTextWriterWriteElement(w, BAD_CAST name, BAD_CAST text.c_str());
}

void write_schemas(const char *out_path, const std::vector<Schema>& schemas)
{
    xmlTextWriterPtr w = xmlNewTextWriterFilename(out_path, 0);
    if (!w)
    {
        std::fprintf(stderr, "gsettings-schema-gen: cannot write %s\n", out_path);
        return;
    }

    xmlTextWriterSetIndent(w, 1);
    xmlTextWriterSetIndentString(w, BAD_CAST "  ");
    xmlTextWriterStartDocument(w, nullptr, "UTF-8", nullptr);
    xmlTextWriterWriteComment(w, BAD_CAST " Generated from Wayfire metadata by gsettings-schema-gen. Do not edit. ");
    xmlTextWriterStartElement(w, BAD_CAST "schemalist");

    for (const auto& s : schemas)
    {
        if (s.name.empty())
        {
            continue;
        }

        xmlTextWriterStartElement(w, BAD_CAST "schema");
        attr(w, "id", "org.wayfire." + s.name);
        if (!s.relocatable)
        {
            attr(w, "path", "/org/wayfire/" + s.name + "/");
        }

        std::set<std::string> seen;
        for (const auto& k : s.keys)
        {
            if (k.skip)
            {
                continue;
            }

            const std::string key = wfgs::to_key(k.name);
            if (!seen.insert(key).second)
            {
                std::fprintf(stderr, "gsettings-schema-gen: org.wayfire.%s: duplicate key '%s' (from '%s') skipped\n",
                    s.name.c_str(), key.c_str(), k.name.c_str());
                continue;
            }

            xmlTextWriterStartElement(w, BAD_CAST "key");
            attr(w, "name", key);
            attr(w, "type", k.gv);
            element(w, "default", gvariant_literal(k));
            if (!k.summary.empty())
            {
                element(w, "summary", oneline(k.summary));
            }

            if (!k.description.empty())
            {
                element(w, "description", oneline(k.description));
            }

            if (k.gv == "i" || k.gv == "d")
            {
                std::string lo = k.min, hi = k.max;
                if ((lo.empty() || hi.empty()) && !k.choices.empty())
                {
                    double lo_v = 0, hi_v = 0;
                    bool have = false;
                    for (const auto& c : k.choices)
                    {
                        double v;
                        try
                        {
                            v = std::stod(c);
                        } catch (...)
                        {
                            continue;
                        }

                        if (!have || v < lo_v) { lo_v = v; lo = c; }
                        if (!have || v > hi_v) { hi_v = v; hi = c; }
                        have = true;
                    }
                }

                if (!lo.empty() && !hi.empty())
                {
                    xmlTextWriterStartElement(w, BAD_CAST "range");
                    attr(w, "min", lo);
                    attr(w, "max", hi);
                    xmlTextWriterEndElement(w);
                }
            } else if (k.gv == "s" && !k.choices.empty() &&
                std::find(k.choices.begin(), k.choices.end(), k.def) != k.choices.end())
            {
                xmlTextWriterStartElement(w, BAD_CAST "choices");
                for (const auto& choice : k.choices)
                {
                    xmlTextWriterStartElement(w, BAD_CAST "choice");
                    attr(w, "value", choice);
                    xmlTextWriterEndElement(w);
                }

                xmlTextWriterEndElement(w);
            }

            xmlTextWriterEndElement(w);
        }

        xmlTextWriterEndElement(w);
    }

    xmlTextWriterEndElement(w);
    xmlTextWriterEndDocument(w);
    xmlFreeTextWriter(w);
}
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: %s OUTPUT METADATA_DIR_OR_FILE...\n", argv[0]);
        return 2;
    }

    std::vector<Schema> schemas;
    for (int i = 2; i < argc; i++)
    {
        collect(argv[i], schemas);
    }

    xmlCleanupParser();

    if (schemas.empty())
    {
        std::fprintf(stderr, "gsettings-schema-gen: no metadata found (is the wayfire submodule initialised?)\n");
        return 1;
    }

    write_schemas(argv[1], schemas);
    return 0;
}
