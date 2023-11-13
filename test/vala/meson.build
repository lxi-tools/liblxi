project('liblxi_test', ['c', 'vala'],
          version: '0.1.0',
    meson_version: '>= 0.62.0',
  default_options: [ 'warning_level=2', 'werror=false', ],
)

vapi_dir = meson.current_source_dir() + '/vapi'

add_project_arguments(['--vapidir', vapi_dir], language: 'vala')

lxitest_sources = [
  'main.vala',
  'receive-image-data.vala',
  'scpi.vala',
  'search-mdns.vala',
  'search.vala'
]

labui_deps = [
  dependency('glib-2.0'),
  dependency('liblxi'),
  meson.get_compiler('c').find_library('tirpc')
]

executable('liblxi_test', lxitest_sources,
  dependencies: labui_deps,
       install: true,
)