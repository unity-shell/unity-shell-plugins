project('unity-shell-plugins', 'cpp',
  version: '0.1',
  meson_version: '>= 0.59',
  default_options: ['cpp_std=c++17', 'warning_level=1'])

wayfire  = dependency('wayfire')
wfconfig = dependency('wf-config')
wlroots  = dependency('wlroots-0.19')

plugindir = get_option('libdir') / 'wayfire'

if get_option('gsettings')
  subdir('gsettings')
endif

summary({
  'gsettings': get_option('gsettings'),
  'plugindir': plugindir,
}, section: 'unity-shell-plugins')
