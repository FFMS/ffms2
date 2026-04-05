project(
    'AviSynth+',
    'c',
    license : 'GPL-2.0-or-later',
    meson_version : '>=1.3.0',
    version : '3.7.5',
)

# Header-only dependency for building AviSynth plugins
avisynth_inc = include_directories('avs_core/include')
avisynth_headers_dep = declare_dependency(include_directories : avisynth_inc)
