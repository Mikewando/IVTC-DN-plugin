project('IVTC DN Vapoursynth Plugin', 'c', 'cpp',
  default_options: ['buildtype=release', 'warning_level=2', 'b_lto=true', 'b_ndebug=if-release', 'cpp_std=c++20'],
  license: 'LGPL-2.1-or-later',
  meson_version: '>=0.51.0',
  version: '1'
)

vapoursynth_dep = dependency('vapoursynth', version: '>=55').partial_dependency(compile_args: true, includes: true)

deps = [vapoursynth_dep]

sources = [
  'src/plugin.cpp',
  'src/miniz.c'
]

shared_module('ivtcdn', sources,
  include_directories: 'src',
  dependencies: deps,
  install: true,
  install_dir: join_paths(vapoursynth_dep.get_pkgconfig_variable('libdir'), 'vapoursynth'),
  gnu_symbol_visibility: 'hidden'
)
