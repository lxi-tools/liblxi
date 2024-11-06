liblxi_sources = [
  'lxi.c',
  'mdns.c',
  'tcp.c',
  'vxi11.c',
  'vxi11core_clnt.c',
  'vxi11core_xdr.c',
]

liblxi_deps = [
  dependency('avahi-client', required: false),
  dependency('libxml-2.0', required: true),
  dependency('threads', required: true),
]

tirpc_excluded_systems = ['darwin']
liblxi_link_args = ['-fvisibility=hidden']
tirpc_incdir = []

liblxi_c_args = [
  '-Wno-unused-variable',
  '-Wno-unused-parameter',
  '-Wno-unused-result',
  '-fvisibility=hidden',
  '-D_GNU_SOURCE',
]

link_with_shared_libs = []
if(build_machine.system() == 'cygwin')
  add_project_arguments('-DHAVE_CYGWIN_DNSSD', language: 'c')

  liblxi_mdns_sources = 'cygwin_dnssd.c'
  liblxi_mdns_link_args = ['-lws2_32']
  lxi_mdns = shared_library(
    'lxi_mdns',
    liblxi_mdns_sources,
    install: true,
    c_args: liblxi_c_args,
    link_args: liblxi_mdns_link_args,
    version: '1.0.0',
  )
  link_with_shared_libs += lxi_mdns
else
  # without avahi or bonjour, mdns discovery won't do anything
  if meson.get_compiler('c').has_header('avahi-client/client.h')
    add_project_arguments('-DHAVE_AVAHI', language: 'c')
    liblxi_sources += 'avahi.c'
  elif meson.get_compiler('c').has_header('dns_sd.h')
    add_project_arguments('-DHAVE_BONJOUR', language: 'c')
    liblxi_sources += 'bonjour.c'
  endif
endif

if host_machine.system() not in tirpc_excluded_systems
  tirpc_dep = dependency('libtirpc', required: true)
if(build_machine.system() == 'cygwin')
  tirpc_incpath = join_paths(tirpc_dep.get_variable(pkgconfig: 'includedir'), 'rpc')
    liblxi_deps += tirpc_dep
else
  tirpc_incpath = join_paths(tirpc_dep.get_variable(pkgconfig: 'includedir'), 'tirpc')
endif
  tirpc_incdir = include_directories(tirpc_incpath)
  liblxi_link_args += ['-Wl,-init,init']
elif not meson.get_compiler('c').has_header('rpc/rpc.h')
  error('The required RPC headers is not available, cannot proceed with the build.')
endif

liblxi = shared_library(
  'lxi',
  liblxi_sources,
  dependencies: liblxi_deps,
  install: true,
  c_args: liblxi_c_args,
  include_directories: tirpc_incdir,
  link_args: liblxi_link_args,
  link_with: link_with_shared_libs,
  version: '1.0.0',
)

install_headers('lxi.h')
