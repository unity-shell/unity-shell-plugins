mapping = static_library('wfgs-mapping', 'gsettings-mapping.cpp', install: false)

gen = executable('gsettings-schema-gen', 'tools/gsettings-schema-gen.cpp',
  link_with: mapping, dependencies: dependency('libxml-2.0'), install: false)

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

shared_module('wf-config-gsettings', 'gsettings-config-backend.cpp', 'gsettings-glib-source.cpp', 'gsettings-variant.cpp',
  link_with: mapping,
  dependencies: [wayfire, wfconfig, wlroots, dependency('wayland-server'), dependency('gio-2.0')],
  name_prefix: '',
  install: true,
  install_dir: plugindir)
