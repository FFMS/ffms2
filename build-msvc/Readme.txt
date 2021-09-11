BUILD INSTRUCTIONS FOR WINDOWS

The included projects require Visual Studio 2019 with an integrated vcpkg.

Run `vcpkg install ffmpeg:x64-windows-static` or `vcpkg install ffmpeg:x86-windows-static`
depending on the configuration you want to build.

In addition to this the Avisynth+ headers are also needed. The include path is set
to be relative to the FFMS2 directory by default.