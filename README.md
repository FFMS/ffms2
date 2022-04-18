[![Build Status](https://api.travis-ci.org/FFMS/ffms2.svg?branch=master)](https://travis-ci.org/FFMS/ffms2)

**FFmpegSource** (usually known as **FFMS** or **FFMS2**) is a cross-platform wrapper library around [FFmpeg](http://ffmpeg.org). It gives you an easy, convenient way to say "open and decompress this media file for me, I don't care how you do it" and get frame- and sample-accurate access (usually), without having to bother with the sometimes less than straightforward and less than perfectly documented FFmpeg API.

The library is written in C++, but the public API is pure C, so if you can link to a C library, you can use FFMS2. The source is available under the MIT license, but the license of the binaries depends on how FFmpeg was compiled. There are optional components that require a GPL FFmpeg, and if those are compiled in FFMS2 itself becomes covered by the GPL as well. The official Windows builds are GPLv3 for this reason.

For more information on using the library, see the [API documentation](doc/ffms2-api.md) and the [changelog](doc/ffms2-changelog.md).

### Avisynth and VapourSynth plugin
For the end user, the most visible use of FFMS is the implementation of both an [Avisynth](http://avisynth.nl) and a [VapourSynth](http://www.vapoursynth.com) source plugin that uses the FFMS library to open media files. This plugin is a part of the FFMS2 project and is available for download here; for documentation see the [Avisynth user guide](doc/ffms2-avisynth.md).

### Features
In addition to being able to open almost any common audio or video format, the Avisynth plugin has a number of more or less unique properties that other Avisynth source filters lack:

  * It is the only source filter that has support for Unicode filenames that are not representable in the system codepage.
  * It is the only source filter that has proper variable framerate (VFR) support.
  * It is the only general-purpose (i.e. not restricted to one or a few formats) source filter that will work reliably when running Avisynth under Wine.
  * It is the only general-purpose source filter that does not rely on external decoders.
  * It is (probably) the only source filter that supports mid-stream video resolution switches.

### Installation in Debian and derivates (Ubuntu, Linux Mint, PopOS, etc.)

Open a terminal and run:

```
sudo -i
apt-get install build-essential autoconf git automake wget ffmpeg libavformat-dev libavcodec-dev libswscale-dev libavutil-dev libswresample-dev
git clone https://github.com/FFMS/ffms2.git
cd ffms2
./autogen.sh --prefix=/usr/local
make -j$(nproc) install
```

### Installation in Fedora and downstream (CentOS, RHEL, AlmaLinux, etc.)

First, please see [rpmfusion.org](https://rpmfusion.org/Configuration/) for enabling free, nonfree, free tainted, and nonfree-tainted repos.

Open a terminal and run:

```
su -
dnf install ffms2 ffmpeg
```

### Installation in CentOS 7 (for CentOS 8+, see Fedora)

Open a terminal and run:

```
su -
yum install epel-release
yum install ffms2 ffmpeg
```

### Installation in Alpine

Enable community repos, then open a terminal and run:

```
su -
apk add alpine-sdk autoconf git automake wget ffmpeg ffmpeg-libs ffmpeg-dev
git clone https://github.com/FFMS/ffms2.git
cd ffms2
./autogen.sh --prefix=/usr/local
make -j$(nproc) install
```

### Installation in OpenSUSE

Open a terminal and run:

```
su -
zypper install --type pattern devel_basis
zypper install autoconf git automake wget ffmpeg libavformat-dev libavcodec-dev libswscale-dev libavutil-dev libswresample-dev
git clone https://github.com/FFMS/ffms2.git
cd ffms2
./autogen.sh --prefix=/usr/local
make -j$(nproc) install
```

### Installation in ArchLinux

Open a terminal and run:

```
su -
pacman -Sy ffmpeg ffms2
```

### Installation on MacOS

First, open a terminal and install homebrew if you don't have it already:

```
# actual installation of brew:
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# add brew to path:
test -d ~/.linuxbrew && eval "$(~/.linuxbrew/bin/brew shellenv)"
test -d /home/linuxbrew/.linuxbrew && eval "$(/home/linuxbrew/.linuxbrew/bin/brew shellenv)"
test -r ~/.bash_profile && echo "eval \"\$($(brew --prefix)/bin/brew shellenv)\"" >> ~/.bash_profile
echo "eval \"\$($(brew --prefix)/bin/brew shellenv)\"" >> ~/.profile
brew update
```

Then:

```
sudo -i
brew install coreutils git autoconf automake wget ffmpeg
git clone https://github.com/FFMS/ffms2.git
cd ffms2
mkdir -pm /usr/local/bin
./autogen.sh --prefix=/usr/local
make -j8 install
update_dyld_shared_cache
```

Then, according to [opensource.apple.com](https://opensource.apple.com/source/dyld/dyld-635.2/doc/man/man1/update_dyld_shared_cache.1.auto.html), you might need to reboot in order for the new shared library cache to take effect.

### Versions and variants
If you're confused by all the different variants, here's a small explanation:

  * Vanilla (no suffix): standard 32-bit version. If you don't know what you want, you want this.
  * -x64: 64-bit version; mostly for use with 64-bit Avisynth.
  * -avs-cplugin: Variant of the Avisynth plugin written in C. Primary purpose is to get access to the new colorspaces available in Avisynth 2.6.
  * SDK: software developer's kit, for people who want to develop Windows applications that use FFMS2, using Microsoft Visual Studio 2008 or later.

Packages marked rNUMBER are testing builds made in-between releases. Download them if you need some bleeding-edge feature or just want to test out the upcoming version. Do note that they may be less stable than the official release versions.

### Why is it called FFmpegSource, that makes no sense at all!?!
FFMS originated as an Avisynth file reader plugin, and those are traditionally called FooSource, where Foo usually is the method used to open the file. For historical reasons the entire project is still called FFmpegSource, although these days the name is pretty misleading and makes people think it has something to do with FFmpeg's source code or somesuch. To avoid confusion, it's probably better to refer to the library as FFMS (2, since version 1 was only an Avisynth plugin...) and keep the FFmpegSource name for the Avisynth plugin.
