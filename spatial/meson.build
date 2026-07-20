shared_module('spatial',
  'plugin.cpp',
  'controller.cpp',
  'renderer.cpp',
  'coords.cpp',
  'gesture.cpp',
  'drag.cpp',
  move_drag_src,
  name_prefix: 'lib',
  dependencies: [wayfire, wfconfig, wlroots, dependency('pixman-1')],
  install: true,
  install_dir: plugindir)

install_data('spatial.xml', install_dir: get_option('datadir') / 'wayfire' / 'metadata')
