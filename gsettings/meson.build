gio      = dependency('gio-2.0')
wlserver = dependency('wayland-server')
libxml   = dependency('libxml-2.0')

# Shared mapping helpers reused by the schema generator and runtime backend.
mapping = static_library('wfgs-mapping', 'gsettings-mapping.cpp', install: false)

# Generates org.wayfire.gschema.xml from Wayfire metadata XML.
gen = executable('gsettings-schema-gen', 'tools/gsettings-schema-gen.cpp',
  link_with: mapping, dependencies: libxml, install: false)

schema = custom_target('wayfire-gschema',
  output: 'org.wayfire.gschema.xml',
  command: [gen, '@OUTPUT@',
    meson.project_source_root() / 'wayfire' / 'metadata'],
  build_by_default: true,
  install: true,
  install_dir: get_option('datadir') / 'glib-2.0' / 'schemas')

import('pkgconfig').generate(
  name: 'unity-shell-plugins',
  description: 'Unity shell Wayfire plugins: org.wayfire.* GSettings schema',
  version: meson.project_version(),
  dataonly: true,
  variables: [
    'schemasdir=' + get_option('prefix') / get_option('datadir') / 'glib-2.0' / 'schemas',
  ])

import('gnome').post_install(glib_compile_schemas: true)
custom_target('gschemas.compiled',
  input: schema, output: 'gschemas.compiled',
  command: [find_program('glib-compile-schemas'), '--targetdir', '@OUTDIR@', '@OUTDIR@'],
  build_by_default: true)

shared_module('wf-config-gsettings', 'gsettings-config-backend.cpp', 'gsettings-glib-source.cpp', 'gsettings-seed.cpp', 'gsettings-variant.cpp',
  link_with: mapping,
  dependencies: [wayfire, wfconfig, wlroots, wlserver, gio],
  name_prefix: '',
  install: true,
  install_dir: plugindir)
