liblxi_includedir = [include_directories('include')]

liblxi_sources = [
  'avahi.c',
  'lxi.c',
  'mdns.c',
  'tcp.c',
  'vxi11.c',
  'vxi11core_clnt.c',
  'vxi11core_xdr.c',
]

liblxi_deps = [
  dependency('avahi-client', required: true),
  dependency('libtirpc', required: true),
  dependency('libxml-2.0', required: true),
  meson.get_compiler('c').find_library('pthread', required: true),
]

if meson.get_compiler('c').has_header('avahi-client/client.h')
  add_project_arguments('-DHAVE_AVAHI', language: 'c')
endif

liblxi_c_args = [
  '-Wno-unused-variable',
  '-Wno-unused-parameter',
  '-Wno-unused-result',
  '-fvisibility=hidden',
  '-D_GNU_SOURCE',
]

liblxi = shared_library(
  'lxi',
  liblxi_sources,
  dependencies: liblxi_deps,
  include_directories: liblxi_includedir,
  install: true,
  c_args: liblxi_c_args,
)

subdir('include')
