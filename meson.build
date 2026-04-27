project(
    'ffms2',
    'cpp',
    default_options : [
        'buildtype=release',
        'warning_level=2',
        'b_ndebug=if-release',
        'cpp_std=gnu++11,c++11'
    ],
    license : 'MIT',
    license_files : 'COPYING',
    meson_version : '>=1.3.0',
    version : '5.1.2'
)

cpp_compiler = meson.get_compiler('cpp')

global_cpp_private_args = [
    '-DFFMS_EXPORTS',
    '-D__STDC_CONSTANT_MACROS',
]

if get_option('buildtype') == 'debug' and host_machine.system() == 'windows'
    global_cpp_private_args += ['-DFFMS_WIN_DEBUG']
endif

ffms2_cpp_static_args = [
    '-DFFMS_STATIC',
]

ffms2_link_args = []
if cpp_compiler.has_link_argument('-Wl,-Bsymbolic')
    # The -Wl,-Bsymbolic flag is required only when building ffms2 as a shared library against a static build of FFmpeg.
    # Unfortunately, there is no reliable way to detect whether FFmpeg is static at configure time (especially when using *-uninstalled.pc files).
    # To avoid unexpected link errors, always use this linker flag.
    # For details, see FFmpeg's advanced linking notes:https://ffmpeg.org/platform.html#Advanced-linking-configuration
    ffms2_link_args += ['-Wl,-Bsymbolic']
endif

ffmsindex_link_args = []
if host_machine.system() == 'windows' and cpp_compiler.has_argument('-municode')
    ffmsindex_link_args += ['-municode']
endif

libavutil_dep = dependency('libavutil', version : '>=59.39.0')

ffms2_deps = [
    dependency('libavformat', version : '>=61.7.0'),
    dependency('libavcodec', version : '>=61.19.0'),
    dependency('libswscale', version : '>=8.3.0'),
    libavutil_dep,
    dependency('libswresample', version : '>=5.3.0'),
    dependency('zlib')
]

ffmsindex_deps = [
    libavutil_dep
]

ffms2_src = [
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
    'src/vapoursynth/VapourSynth4.h',
    'src/vapoursynth/VSHelper4.h',
    'src/vapoursynth/vapoursource4.cpp',
    'src/vapoursynth/vapoursource4.h',
    'src/vapoursynth/vapoursynth4.cpp',
]

if get_option('avisynth').enabled()
    avisynth_dep = dependency('avisynth', required : false, version : '>=3.7.3')

    if avisynth_dep.found()
        avisynth_dep = avisynth_dep.partial_dependency(compile_args: true, includes: true)
    else
        avisynth_subproject = subproject('AviSynth+')
        avisynth_dep = avisynth_subproject.get_variable('avisynth_headers_dep')
    endif

    ffms2_deps += avisynth_dep

    ffms2_src += [
        'src/avisynth/avssources.cpp',
        'src/avisynth/avssources.h',
        'src/avisynth/avisynth.cpp',
    ]
endif

ffmsindex_src = [
  'src/index/ffmsindex.cpp',
  'src/index/vsutf16.h'
]

includes = include_directories('include')

ffms2_lib = library(
    'ffms2',
    ffms2_src,
    cpp_args : global_cpp_private_args,
    cpp_static_args : ffms2_cpp_static_args,
    dependencies : ffms2_deps,
    gnu_symbol_visibility : 'hidden',
    include_directories : includes,
    install : true,
    link_args : ffms2_link_args,
)

executable(
    'ffmsindex',
    ffmsindex_src,
    cpp_args : global_cpp_private_args,
    dependencies : ffmsindex_deps,
    gnu_symbol_visibility : 'hidden',
    include_directories : includes,
    install : true,
    link_args : ffmsindex_link_args,
    link_with : ffms2_lib,
)

static_cflags = []
if get_option('default_library') == 'static'
    static_cflags += '-DFFMS_STATIC'
endif

# TODO - When meson implement a way to specify element in the field "Cflags.private", we will need to add the flag "-DFFMS_STATIC".
# See: https://github.com/mesonbuild/meson/issues/14749
pkg_config = import('pkgconfig')
pkg_config.generate(
    ffms2_lib,
    description : 'The Fabulous FM Library 2',
    extra_cflags : static_cflags,
)

# TODO - When meson implement a way to specify a static compile args, we will need to add the flag "-DFFMS_STATIC".
# See: https://github.com/mesonbuild/meson/issues/5723
ffms2_dep = declare_dependency(
    link_with : ffms2_lib,
    include_directories : includes,
    compile_args : static_cflags,
)
meson.override_dependency('ffms2', ffms2_dep)

install_data(
    ['doc/ffms2-api.md', 'doc/ffms2-avisynth.md', 'doc/ffms2-changelog.md', 'doc/ffms2-vapoursynth.md'],
    install_dir : join_paths(get_option('datadir'), 'doc', 'ffms2')
)

install_headers(['include/ffms.h', 'include/ffmscompat.h'])

if get_option('test').enabled()
    subdir('test')
endif
