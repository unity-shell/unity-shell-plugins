project('unity-shell-plugins', 'cpp',
  version: '26',
  meson_version: '>= 1.0',
  default_options: ['cpp_std=c++17', 'warning_level=1'])

wayfire  = dependency('wayfire')
wfconfig = dependency('wf-config')
wlroots  = dependency('wlroots-0.20')

add_project_arguments(
  '-I' + wayfire.get_variable(pkgconfig: 'includedir') / 'wayfire',
  language: 'cpp')

plugindir = get_option('libdir') / 'wayfire'

if get_option('gsettings')
  subdir('gsettings')
endif

if get_option('spatial')
  move_drag_src = files('wayfire' / 'plugins' / 'common' / 'move-drag-interface.cpp')
  subdir('spatial')
endif

if get_option('night-light')
  subdir('night-light')
endif

summary({
  'gsettings': get_option('gsettings'),
  'spatial':   get_option('spatial'),
  'night-light': get_option('night-light'),
  'plugindir': plugindir,
}, section: 'unity-shell-plugins')
