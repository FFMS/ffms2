project('ffms2', 'cpp',
  default_options : ['buildtype=release', 'warning_level=2', 'b_ndebug=if-release', 'cpp_std=c++11'],
  meson_version : '>=0.48.0',
  version : '2.30.0'
)

add_project_arguments('-DFFMS_EXPORTS', '-D_FILE_OFFSET_BITS=64', '-D__STDC_CONSTANT_MACROS', language : 'cpp')

ffms2_sources = [
  'src/core/audiosource.cpp',
  'src/core/audiosource.h',
  'src/core/ffms.cpp',
  'src/core/filehandle.cpp',
  'src/core/filehandle.h',
  'src/core/indexing.cpp',
  'src/core/indexing.h',
  'src/core/track.cpp',
  'src/core/track.h',
  'src/core/utils.cpp',
  'src/core/utils.h',
  'src/core/videosource.cpp',
  'src/core/videosource.h',
  'src/core/videoutils.cpp',
  'src/core/videoutils.h',
  'src/core/zipfile.cpp',
  'src/core/zipfile.h',
  'src/vapoursynth/vapoursource.cpp',
  'src/vapoursynth/vapoursource.h',
  'src/vapoursynth/vapoursynth.cpp',
  'src/vapoursynth/VapourSynth.h',
  'src/vapoursynth/VSHelper.h'
]

ffmsindex_sources = [
  'src/index/ffmsindex.cpp',
  'src/index/vsutf16.h'
]

includes = include_directories('include')

deps = [
  dependency('libavformat', version : '>=53.20.0'),
  dependency('libavcodec', version : '>=53.24.0'),
  dependency('libswresample', version : '>=1.0.0'),
  dependency('libswscale', version : '>=0.7.0'),
  dependency('libavutil', version : '>=51.21.0'),
  dependency('zlib')
]

if host_machine.cpu_family().startswith('x86')
  add_project_arguments('-mfpmath=sse', '-msse2', language : 'cpp')
endif

ffms2_lib = library('ffms2', ffms2_sources,
  dependencies : deps,
  include_directories : includes,
  install : true,
  gnu_symbol_visibility : 'hidden'
)

compiler = meson.get_compiler('cpp')
ffmsindex_ldflags = []
if (compiler.get_id() == 'gcc' or compiler.get_id() == 'clang') and host_machine.system() == 'windows'
  ffmsindex_ldflags += '-municode'
endif

executable('ffmsindex', ffmsindex_sources,
  link_args : ffmsindex_ldflags,
  link_with : ffms2_lib,
  include_directories : includes,
  install : true,
  gnu_symbol_visibility : 'hidden'
)

import('pkgconfig').generate(ffms2_lib,
  filebase : 'ffms2',
  name : 'ffms2',
  description : 'The Fabulous FM Library 2'
)

install_data(['doc/ffms2-api.md', 'doc/ffms2-changelog.md'],
  install_dir : join_paths(get_option('datadir'), 'doc', 'ffms2')
)

install_headers(['include/ffms.h', 'include/ffmscompat.h'])
