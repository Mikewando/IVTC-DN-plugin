project('IVTC DN Vapoursynth Plugin', 'c', 'cpp',
  default_options: ['buildtype=release', 'warning_level=2', 'b_lto=true', 'b_ndebug=if-release', 'cpp_std=c++20'],
  license: 'LGPL-2.1-or-later',
  meson_version: '>=0.51.0',
  version: '1'
)

add_project_arguments('-Wno-unused-parameter', language: 'cpp')

static = get_option('static')

vapoursynth_dep = dependency('vapoursynth', version: '>=55').partial_dependency(compile_args: true, includes: true)

deps = [
  vapoursynth_dep,
]

install_dir = vapoursynth_dep.get_variable(pkgconfig: 'libdir') / 'vapoursynth'

sources = [
  'src/plugin.cpp',
  'src/miniz.c'
]

if host_machine.cpu_family().startswith('x86')
  add_project_arguments('-mfpmath=sse', '-msse2', language: 'cpp')
endif

if host_machine.system() == 'windows'
  add_project_link_arguments('-lws2_32', language: 'cpp')
endif

if static
  add_project_link_arguments('-static', language: 'cpp')
endif

libs = []

shared_module('ivtcdn', sources,
  include_directories: 'src',
  dependencies: deps,
  link_with: libs,
  install: true,
  install_dir: install_dir,
  gnu_symbol_visibility: 'hidden'
)
