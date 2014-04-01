[![Build Status](https://travis-ci.org/FFMS/ffms2.svg)](https://travis-ci.org/FFMS/ffms2)

**FFmpegSource** (usually known as **FFMS** or **FFMS2**) is a cross-platform wrapper library around [libav](http://libav.org/)/[FFmpeg](http://ffmpeg.org), plus some additional components to deal with file formats libavformat has (or used to have) problems with. It gives you an easy, convenient way to say "open and decompress this media file for me, I don't care how you do it" and get frame- and sample-accurate access (usually), without having to bother with the sometimes less than straightforward and less than perfectly documented libav API.

The library is written in C++, but the public API is pure C, so if you can link to a C library, you can use FFMS2. The source is available under the MIT license, but the license of the binaries depends on how libav was compiled. There are optional components that require a GPL libav, and if those are compiled in FFMS2 itself becomes covered by the GPL as well. The official Windows builds are GPLv3 for this reason.

For more information on using the library, see the [API documentation](http://htmlpreview.github.io/?https://github.com/FFMS/ffms2/blob/master/doc/ffms2-api.html) and the [changelog](http://htmlpreview.github.io/?https://github.com/FFMS/ffms2/blob/master/doc/ffms2-changelog.html).

## Avisynth plugin
For the end user, the most visible use of FFMS is the implementation of an [Avisynth](http://www.avisynth.org) source plugin that uses the FFMS library to open media files. This plugin is a part of the FFMS2 project and is available for download here; for documentation see the [user guide](http://htmlpreview.github.io/?https://github.com/FFMS/ffms2/blob/master/doc/ffms2-avisynth.html).

### Features
In addition to being able to open almost any common audio or video format, the Avisynth plugin has a number of more or less unique properties that other Avisynth source filters lack:

  * It is the only source filter that has support for Unicode filenames that are not representable in the system codepage.
  * It is the only source filter that has proper variable framerate (VFR) support.
  * It is the only general-purpose (i.e. not restricted to one or a few formats) source filter that will work reliably when running Avisynth under Wine.
  * It is the only general-purpose source filter that does not rely on external decoders.
  * It is (probably) the only source filter that supports mid-stream video resolution switches.

### Versions and variants
If you're confused by all the different variants, here's a small explanation:

  * Vanilla (no suffix): standard 32-bit version. If you don't know what you want, you want this.
  * -x64: 64-bit version; mostly for use with 64-bit Avisynth.
  * -avs-cplugin: Variant of the Avisynth plugin written in C. Primary purpose is to get access to the new colorspaces available in Avisynth 2.6.
  * SDK: software developer's kit, for people who want to develop Windows applications that use FFMS2, using Microsoft Visual Studio 2008 or later.

Packages marked rNUMBER are testing builds made in-between releases. Download them if you need some bleeding-edge feature or just want to test out the upcoming version. Do note that they may be less stable than the official release versions.

### Why is it called FFmpegSource, that makes no sense at all!?!
FFMS originated as an Avisynth file reader plugin, and those are traditionally called FooSource, where Foo usually is the method used to open the file. For historical reasons the entire project is still called FFmpegSource, although these days the name is pretty misleading and makes people think it has something to do with FFmpeg's source code or somesuch. To avoid confusion, it's probably better to refer to the library as FFMS (2, since version 1 was only an Avisynth plugin...) and keep the FFmpegSource name for the Avisynth plugin.
