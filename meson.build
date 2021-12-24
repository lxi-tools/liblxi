project('liblxi', 'c',
  version : '1.13',
  license : [ 'BSD-3-Clause'],
  meson_version : '>= 0.53.2',
  default_options : [ 'warning_level=2', 'buildtype=release', 'c_std=gnu11' ]
)

# The tag date of the project_version(), update when the version bumps.
version_date='2021-12-24'

subdir('src')
subdir('man')

pkg_mod = import('pkgconfig')
pkg_mod.generate(libraries : liblxi,
  name : 'liblxi',
  filebase : 'liblxi',
  description : 'A Library for managing LXI compatible test instruments'
)
