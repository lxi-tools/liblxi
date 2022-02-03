liblxi_sources = [
  'lxi.c',
  'mdns.c',
  'tcp.c',
  'vxi11.c',
  'vxi11core_clnt.c',
  'vxi11core_xdr.c',
]

liblxi_deps = [
  dependency('libxml-2.0', required: true),
  dependency('threads', required: true),
]

tirpc_dep = dependency('libtirpc', required: true)
tirpc_incpath = join_paths(tirpc_dep.get_variable(pkgconfig: 'includedir'), 'tirpc')
tirpc_incdir = include_directories(tirpc_incpath)

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
  install: true,
  c_args: liblxi_c_args,
  include_directories: tirpc_incdir,
  link_args: ['-fvisibility=hidden', '-Wl,-init,init'],
  version: '1.0.0',
)

install_headers('lxi.h')
