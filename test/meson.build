python = import('python').find_installation()
sample_dir = join_paths(meson.current_source_dir(), 'samples')
run_command(
    python, join_paths(meson.current_source_dir(), 'download_samples.py'), sample_dir,
    check : true
)

gtest_dep = dependency('gtest')

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
