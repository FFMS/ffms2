#!/bin/bash

set -e

FFMPEG_GIT='git://source.ffmpeg.org/ffmpeg.git'
LIBAV_GIT='git://git.libav.org/libav.git'
ZLIB_GIT='git://github.com/madler/zlib.git'
OPENCORE_AMR_GIT='git://git.code.sf.net/p/opencore-amr/code'

usage() {
  echo 'usage: build-win-deps.sh [ffmpeg|libav] <toolchain>'
  echo ''
  echo 'Build files will be placed in the directory the script is run from (so'
  echo 'you may want to run it from outside of the ffms2 tree).'
  exit 1
}


if [ -z $1 ]; then
  usage
fi

fork=$1

platform='Win32'
if [[ $LIB =~ amd64 ]]; then
  platform='x64'
fi

toolchain=${2:-msvc}

clone_or_update() {
  if [ -d $1 ]; then
    cd $1
    git clean -xfd
    git fetch
    git reset --hard origin/master
    cd ..
  else
    git clone $2 $1
    cd $1
    git config --local core.autocrlf false
    git rm --cached -r .
    git reset --hard
    cd ..
  fi
}

if [ x$fork = xffmpeg ]; then
  clone_or_update ffmpeg $FFMPEG_GIT
elif [ x$fork = xlibav ]; then
  clone_or_update libav $LIBAV_GIT
else
  usage
fi

clone_or_update zlib $ZLIB_GIT
clone_or_update opencore-amr $OPENCORE_AMR_GIT

work_dir=$(pwd)
ffms_dir=$(cd $(dirname $0); pwd)
debug_prefix="${ffms_dir}/deps/${platform}-Debug"
release_prefix="${ffms_dir}/deps/${platform}-Release"

mkdir -p $debug_prefix/include
mkdir -p $debug_prefix/lib
mkdir -p $release_prefix/include
mkdir -p $release_prefix/lib

### Zlib

cd $work_dir/zlib

sed -i '/<unistd.h>/d' zconf.h

git clean -xfd
nmake -f win32/Makefile.msc zlib.lib CFLAGS='-nologo -MTd -W3 -Oy- -Zi -Fd"zlib"'
cp zlib.lib ${debug_prefix}/lib
cp zlib.pdb ${debug_prefix}/lib
cp zlib.h ${debug_prefix}/include
cp zconf.h ${debug_prefix}/include

git clean -xfd
nmake -f win32/Makefile.msc zlib.lib CFLAGS='-nologo -MT -W3 -Ox -Oy- -Zi -Fd"zlib"'
cp zlib.lib ${release_prefix}/lib
cp zlib.pdb ${release_prefix}/lib
cp zlib.h ${release_prefix}/include
cp zconf.h ${release_prefix}/include

### opencore-amr

repack_static_lib() {
  pushd $1/lib
  mkdir temp
  cd temp
  ar -x ../$2
  lib *.obj -out:../$2
  cd ..
  rm -r temp
  popd
}

opencore_common_flags="\
  CC=cl \
  CXX=cl \
  --enable-static \
  --disable-shared \
  --disable-compile-c \
  --prefix=${work_dir}/junk"

cd $work_dir/opencore-amr

export CFLAGS='-nologo -Ox -MTd'
export CXXFLAGS='-nologo -Ox -MTd'

git clean -xfd
autoreconf -if
./configure $opencore_common_flags --libdir=${debug_prefix}/lib --includedir=${debug_prefix}/include
make -j$NUMBER_OF_PROCESSORS
make install

repack_static_lib $debug_prefix opencore-amrnb.lib
repack_static_lib $debug_prefix opencore-amrwb.lib

export CFLAGS='-nologo -Ox -MT'
export CXXFLAGS='-nologo -Ox -MT'

make clean
./configure $opencore_common_flags --libdir=${release_prefix}/lib --includedir=${release_prefix}/include
make -j$NUMBER_OF_PROCESSORS
make install

repack_static_lib $release_prefix opencore-amrnb.lib
repack_static_lib $release_prefix opencore-amrwb.lib

unset CFLAGS
unset CXXFLAGS

### FFmpeg/Libav

ffmpeg_common_flags="        \
  --disable-avfilter         \
  --disable-bzlib            \
  --disable-devices          \
  --disable-doc              \
  --disable-encoders         \
  --disable-filters          \
  --disable-hwaccels         \
  --disable-muxers           \
  --disable-pthreads         \
  --disable-shared           \
  --enable-avresample        \
  --enable-gpl               \
  --enable-version3          \
  --enable-runtime-cpudetect \
  --enable-static            \
  --enable-zlib              \
  --enable-libopencore-amrnb \
  --enable-libopencore-amrwb \
  --extra-cflags=-D_SYSCRT   \
  --extra-cflags=-wd4005     \
  --extra-cflags=-wd4189     \
  --toolchain=$toolchain     \
  --prefix=$work_dir/junk"

if [ $fork = 'libav' ]; then
ffmpeg_common_flags="  \
  $ffmpeg_common_flags \
  --disable-avconv     \
  --disable-avplay     \
  --disable-avprobe    \
"
ffmpeg_debug_flags=""
else
ffmpeg_common_flags="  \
  $ffmpeg_common_flags \
  --disable-ffmpeg     \
  --disable-ffplay     \
  --disable-ffprobe    \
  --disable-ffserver   \
  --disable-postproc   \
"
ffmpeg_debug_flags="--disable-stripping"
fi

cd $work_dir/$fork

git clean -xfd
./configure $ffmpeg_common_flags \
  --enable-debug \
  $ffmpeg_debug_flags \
  --extra-cflags=-I$debug_prefix/include \
  --extra-cflags=-MTd \
  --extra-ldflags=-LIBPATH:$(echo $debug_prefix/lib | sed 's@^/\([a-z]\)/@\1:/@') \
  --libdir=$debug_prefix/lib \
  --incdir=$debug_prefix/include

make -j$NUMBER_OF_PROCESSORS
make install

git clean -xfd
./configure $ffmpeg_common_flags \
  --disable-debug \
  --extra-cflags=-I$release_prefix/include \
  --extra-ldflags=-LIBPATH:$(echo $release_prefix/lib | sed 's@^/\([a-z]\)/@\1:/@') \
  --libdir=$release_prefix/lib \
  --incdir=$release_prefix/include
make -j$NUMBER_OF_PROCESSORS
make install
