cxx = meson.get_compiler('cpp')

# libnova supplies the solar rise/set calculation and has no pkg-config file.
# colord supplies the blackbody whitepoint table.
libnova = cxx.find_library('nova', required: true)
if not cxx.has_header('libnova/solar.h')
  error('libnova headers not found; install libnova-devel')
endif

shared_module('night-light',
  'plugin.cpp',
  'schedule.cpp',
  'ramp.cpp',
  name_prefix: 'lib',
  dependencies: [wayfire, wfconfig, wlroots, libnova, dependency('colord')],
  install: true,
  install_dir: plugindir)

install_data('night-light.xml', install_dir: get_option('datadir') / 'wayfire' / 'metadata')
