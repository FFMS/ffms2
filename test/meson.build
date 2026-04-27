python = import('python').find_installation()
sample_dir = join_paths(meson.current_source_dir(), 'samples')
run_command(
    python, join_paths(meson.current_source_dir(), 'download_samples.py'), sample_dir,
    check : true
)

gtest_dep = dependency('gtest', version : ['>=1.10.0', '<=1.12.1'])

hdr = executable(
    'hdr',
    files('hdr.cpp'),
    cpp_args : ['-DSAMPLES_DIR=' + sample_dir + ''],
    dependencies : [
        gtest_dep,
        ffms2_dep,
        libavutil_dep,
    ],
)
test('hdr', hdr)


indexer = executable(
    'indexer',
    files('indexer.cpp', 'tests.cpp'),
    cpp_args : ['-DSAMPLES_DIR=' + sample_dir + ''],
    dependencies : [
        gtest_dep,
        ffms2_dep,
        libavutil_dep,
    ],
)
test('indexer', indexer, timeout : 120)


display_matrix = executable(
    'display_matrix',
    files('display_matrix.cpp'),
    cpp_args : ['-DSAMPLES_DIR=' + sample_dir + ''],
    dependencies : [
        gtest_dep,
        ffms2_dep,
        libavutil_dep,
    ],
)
test('display_matrix', display_matrix)


# Test that the header are indeed compatible with c99
add_languages('c', native : false)
c_compiler = meson.get_compiler('c')
c99_header = executable(
    'c99_header',
    files('test.c'),
    dependencies : ffms2_dep,
    override_options : {'c_std' : 'c99', 'werror' : true, 'warning_level' : '3'},
)
test('c99_header', c99_header)
