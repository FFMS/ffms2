# FFmpegSource2 User Manual

Opens files using FFmpeg and (almost) nothing else.
May be frame accurate on good days.
The source is MIT licensed and can be obtained from https://github.com/FFMS/ffms2/.
The precompiled binary is GPL3 licensed.
If you are religious you may consider this the second coming.

## Donate

Donate if you like this software.
Collecting weird clips from the internet and making them play takes more time than you'd think.

<form action="https://www.paypal.com/cgi-bin/webscr" method="post">
<p>
<input type="hidden" name="cmd" value="_s-xclick" />
<input type="hidden" name="hosted_button_id" value="6944567" />
<input type="image" src="https://www.paypal.com/en_GB/i/btn/btn_donate_LG.gif" name="submit" alt="PayPal - The safer, easier way to pay online." />
<img alt="" src="https://www.paypal.com/en_US/i/scr/pixel.gif" width="1" height="1" />
</p>
</form>

## Limitations

 - Requires [Haali's Media Splitter][haali] if you want to seek in OGM or MPEG PS/TS.
   Trying to do non-linear access in those containers without it will end in tears.
 - Haali's splitter requires transport streams to but cut at packed boundaries.
   Use [TsRemux][tsremux] to fix the stream if you get an error message complaining about this.
 - Because of LAVF's demuxer, most raw streams (such as elementary h264 and other mpeg video streams) will fail to work properly.
 - FFAudioSource() will have to remake any index implicitly created by FFVideoSource() and therefore code like
   ```
   AudioDub(FFVideoSource(X), FFAudioSource(X))
   ```
   will require two indexing passes. Apart from the time consumed this is harmless.
   To work around it open the audio first:
   ```
   A = FFAudioSource(X)
   V = FFVideoSource(X)
   AudioDub(V, A)
   ```
   or use FFIndex(), like so:
   ```
   FFIndex(X)
   AudioDub(FFVideoSource(X), FFAudioSource(X))
   ```

[haali]: http://haali.su/mkv/
[tsremux]: http://forum.doom9.org/showthread.php?t=125447

## Known issues
 - Interlaced H.264 is decoded in an odd way; each field gets its own full-height frame and the fieldrate is reported as the framerate, and furthermore one of the fields (odd or even) may "jump around".
   To get the correct behavior, you can try setting fpsnum and fpsden so that the framerate is halved (may or may not work).
   This issue is caused by libavcodec.
 - Decoding some M2TS files using Haali's splitter will cause massive blocking and other corruption issues.
   You can work around the issue either by remuxing the file to MKV (using GDSMux (make sure you untick "minimize output file size" in the Global settings tab) or eac3to), or (if you will be doing linear decoding only) by setting `demuxer="lavf"` in `FFIndex` and using `seekmode=0` with `FFVideoSource`.
   The cause of this issue is unknown but being investigated.

## Compatibility

### Video
 - AVI, MKV, MP4, FLV: Frame accurate
 - WMV: Frame accurate(?) but avformat seems to pick keyframes relatively far away
 - OGM: Frame accurate(?)
 - VOB, MPG: Seeking seems to be off by one or two frames now and then
 - M2TS, TS: Seeking seems to be off a few frames here and there
 - Image files: Most formats can be opened if seekmode=-1 is set, no animation support

### Audio
Seeking should be sample-accurate with most codecs in AVI, MKV, MP4 and FLV with two notable exceptions, namely MP3 and AC3 where FFmpeg's decoders seem to be completely broken (with MP3 in particular you can feed the decoder the same encoded data three times in a row and get a different decoded result every time).
Still, results should usually be "good enough" for most purposes.

Decoding linearly will almost always work correctly.

## Indexing and You

Before FFMS2 can open a file, it must be indexed first so that keyframe/sample positions are known and seeking is easily accomplished.
This is done automatically when using `FFVideoSource()` or `FFAudioSource()`,
but if you want to you can invoke the indexing yourself by calling `FFIndex()`, or by running `ffmsindex.exe`.
By default the index is written to a file so it can be reused the next time you open the same file, but this behavior can be turned off if desired.

If you wonder why FFMS2 takes so long opening files, the indexing is the answer.
If you want a progress report on the indexing, you can use the supplied `ffmsindex.exe` commandline program.

## Function reference

### FFIndex
```
FFIndex(string source, string cachefile = source + ".ffindex", int indexmask = -1,
    int dumpmask = 0, string audiofile = "%sourcefile%.%trackzn%.w64", int errorhandling = 3,
    bool overwrite = false, bool utf8 = false, string demuxer = "default")
```
Indexes a number of tracks in a given source file and writes the index file to disk, where it can be picked up and used by `FFVideoSource` or `FFAudioSource`.
Normally you do not need to call this function manually; it's invoked automatically if necessary by `FFVideoSource`/`FFAudioSource`.
It does, however, give you more control over how indexing is done and it can also dump audio tracks to WAVE64 files while indexing is in progress.

Note that this function returns an integer, not a clip (since it doesn't open video, nor audio).
The return value isn't particularly interesting, but for the record it's 0 if the index file already exists (and is valid) and overwrite was not enabled, 1 if the index file was created and no previous index existed, and 2 if the index file was created by overwriting an existing, valid index file.

#### Arguments

##### string source
The source file to index.

##### string cachefile = source + ".ffindex"
The filename of the index file (where the indexing data is saved).
Defaults to `sourcefilename.ffindex`.

##### int indexmask = -1
A binary mask representing what audio tracks should be indexed (all video tracks are always indexed; you have no choice in the matter).
The mask is constructed by bitshifting 1 left by the track number; if multiple tracks are desired, bitwise OR each value so created together to get the full mask.
In other words, the mask is a bit field where each bit is a track number (the least significant bit is track number 0).
Since Avisynth doesn't have any bitwise operators at all, constructing a mask for more than one track inside an Avisynth script is a rather annoying task; for a single track the mask is, naturally, 2 to the power of the track number minus 1 (i.e. if you want to index track 3, the mask is `2^(3-1) = 4`).

Since the mask works like it does, and FFMS2 is designed to run on a machine that uses two's complement integers, -1 (the default) means index all tracks and 0 means index none.

Note that FFMS2's idea about what track has what number may be completely different from what any other application might think, and that track numbering starts from 1.

##### int dumpmask = 0
The same as indexmask, but the tracks flagged by this mask are dumped to disk as decompressed Wave64 files.
This mask overrides indexmask if set to nonzero (more specifically, they are bitwise OR'ed together), since dumping a track indexes it at the same time.

##### string audiofile = "%sourcefile%.%trackzn%.w64"
A string representing a filename template that determines where the audio tracks set to be dumped by the `dumpmask` will be written.
You can use a number of variables here; make sure you include a track number variable if you're dumping multiple tracks, or you'll get really weird results when FFMS2 tries to write multiple tracks to the same file.
Available variables:

 - **%sourcefile%**: same as the source argument, i.e. the file the audio is decoded from
 - **%trackn%**: the track number
 - **%trackzn%**: the track number, zero padded to two digits
 - **%samplerate%**: sample rate in Hertz
 - **%channels%**: number of channels
 - **%bps%**: bits per sample
 - **%delay%**: delay, or more exactly the first timestamp encountered in the audio stream

##### int errorhandling = 3
Controls what happens if an audio decoding error is encountered during indexing.
Possible values are:

 - **0**: Raise an error and abort indexing. No index file is written.</li>
 - **1**: Clear the affected track (effectively making it silent) and continue.</li>
 - **2**: Stop indexing the track but keep all the index entries so far, effectively ending the track where the error occured.</li>
 - **3**: Pretend it's raining and continue anyway. This is the default; if you encounter odd noises in the audio, try mode 0 instead and see if it's FFMS2's fault.</li>

##### bool overwrite = false
If set to true, `FFIndex()` will reindex the source file and overwrite the index file even if the index file already exists and is valid.
Mostly useful for trackmask changes and testing.

##### bool utf8 = false
If set to true, FFMS will assume that the .avs script is encoded as UTF-8 and therefore interpret all filenames as UTF-8 encoded strings.
This makes it possible to open files with funny filenames that otherwise would not be openable.
You only need to set this parameter on the first FFMS2 function you call in a script; subsequent uses will have no further effect.

**NOTE:** You must make sure you save the file without a BOM (byte-order marker) or Avisynth will refuse to open it.
Notepad will write a BOM, so use something else.

You should also note that setting this parameter incorrectly will cause all file openings to fail unless your filenames are exclusively 7-bit ASCII compatible.

##### string demuxer = "default"
Forces FFMS to use a given demuxer, namely one of:

 - **default**: probe for the best source module, i.e. choose automatically.
 - **lavf**: use libavformat.
 - **matroska**: use Haali's Matroska parser. Obviously only works for Matroska and WebM files.
 - **haalimpeg**: use Haali's DirectShow MPEG TS/PS parser. Only works if Haali Media Splitter is installed and only on MPEG TS/PS files (.ts/.m2ts/.mpg/.mpeg).
 - **haaliogg:** use Haali's DirectShow Ogg parser. As above, only works if Haali Media Splitter is installed, and only on Ogg files (.ogg/.ogm).

You should only use this parameter if you know exactly what you're doing and exactly why you want to force another demuxer.

### FFVideoSource
```
FFVideoSource(string source, int track = -1, bool cache = true,
    string cachefile = source + ".ffindex", int fpsnum = -1, int fpsden = 1,
    int threads = -1, string timecodes = "", int seekmode = 1, int rffmode = 0,
    int width = -1, int height = -1, string resizer = "BICUBIC",
    string colorspace = "", bool utf8 = false, string varprefix = "")
```
Opens video. Will invoke indexing of all video tracks (but no audio tracks) if no valid index file is found.

#### Arguments

##### string source
The source file to open.

##### int track = -1
The video track number to open, as seen by the relevant demuxer.
Track numbers start from zero, and are guaranteed to be continous (i.e. there must be a track 1 if there is a track 0 and a track 2). -1 means open the first video track.
Note that FFMS2's idea about what track has what number may (or may not) be completely different from what some other application might think.
Trying to open an audio track with `FFVideoSource` will naturally fail.

##### bool cache = true
If set to true (the default), `FFVideoSource` will first check if the `cachefile` contains a valid index, and if it does, that index will be used.
If no index is found, all video tracks will be indexed, and the indexing data will be written to `cachefile` afterwards.
If set to false, `FFVideoSource` will not look for an existing index file; instead all video tracks will be indexed when the script is opened, and the indexing data will be discarded after the script is closed; you will have to index again next time you open the script.

##### string cachefile = source + ".ffindex"
The filename of the index file (where the indexing data is saved).
Defaults to `sourcefilename.ffindex`.
Note that if you didn't change this parameter from its default value and `FFVideoSource` encounters an index file that doesn't seem to match the file it's trying to open, it will automatically reindex and then overwrite the old index file.
On the other hand, if you *do* change it, `FFVideoSource` will assume you have your reasons and throw an error instead if the index doesn't match the file.

##### int fpsnum = -1, int fpsden = 1
Controls the framerate of the output; used for VFR to CFR conversions.
If `fpsnum` is less than or equal to zero (the default), the output will contain the same frames that the input did, and the frame rate reported to Avisynth will be set based on the input clip's average frame duration.
If `fpsnum` is greater than zero, `FFVideoSource` will force a constant frame rate, expressed as a rational number where `fpsnum` is the numerator and `fpsden` is the denominator.
This may naturally cause `FFVideoSource` to drop or duplicate frames to achieve the desired frame rate, and the output is not guaranteed to have the same number of frames that the input did.

##### int threads = -1
The number of decoding threads to request from libavcodec.
Setting it to less than or equal to zero means it defaults to the number of logical CPU's reported by Windows.
Note that this setting might be completely ignored by libavcodec under a number of conditions; most commonly because a lot of decoders actually do not support multithreading.

##### string timecodes = ""
Filename to write Matroska v2 timecodes for the opened video track to.
If the file exists, it will be truncated and overwritten.
Set to the empty string to disable timecodes writing (this is the default).

##### int seekmode = 1
Controls how seeking is done.
Mostly useful for getting uncooperative files to work.
Only has an effect on files opened with the libavformat demuxer; on other files the equivalent of mode 1 is always used.
Valid modes are:

 - **-1**: Linear access without rewind; i.e. will throw an error if each successive requested frame number isn't bigger than the last one.
   Only intended for opening images but might work on well with some obscure video format.</li>
 - **0**: Linear access (i.e. if you request frame `n` without having requested all frames from 0 to `n-1` in order first, all frames from 0 to `n` will have to be decoded before `n` can be delivered).
   The definition of slow, but should make some formats "usable".
 - **1**: Safe normal. Bases seeking decisions on the keyframe positions reported by libavformat.</li>
 - **2**: Unsafe normal. Same as mode 1, but no error will be thrown if the exact seek destination has to be guessed.</li>
 - **3**: Aggressive. Seeks in the forward direction even if no closer keyframe is known to exist. Only useful for testing and containers where libavformat doesn't report keyframes properly.</li>

##### int rffmode = 0
Controls how RFF flags in the video stream are treated; in other words it's equivalent to the "field operation" mode switch in DVD2AVI/DGIndex.
Valid modes are:

 - **0**: Ignore all flags (the default mode).
 - **1**: Honor all pulldown flags.
 - **2**: Equivalent to DVD2AVI's "force film" mode.

Note that using modes 1 or 2 will make `FFVideoSource` throw an error if the video stream has no RFF flags at all.
When using either of those modes, it will also make the output be assumed as CFR, disallow vertical scaling and disallow setting the output colorspace.
`FFPICT_TYPE` will also not be set as the output is a combination of several frames.
Other subtle behavior changes may also exist.

Also note that "force film" is mostly useless and only here for completeness' sake, since if your source really is safe to force film on, using mode 0 will have the exact same effect while being considerably more efficient.

##### int width = -1, int height = -1
Sets the resolution of the output video, in pixels.
Setting either dimension to less than or equal to zero means the resolution of the first decoded video frame is used for that dimension.
These parameters are mostly useful because FFMS2 supports video streams that change resolution mid-stream; since Avisynth does not, these parameters are used to set single resolution for the output.

##### string resizer = "BICUBIC"
The resizing algorithm to use if rescaling the image is necessary.
If the video uses subsampled chroma but your chosen output colorspace does not, the chosen resizer will be used to upscale the chroma planes, even if you did not request an image rescaling.
The available choices are `FAST_BILINEAR`, `BILINEAR`, `BICUBIC` (default), `X`, `POINT`, `AREA`, `BICUBLIN`, `GAUSS`, `SINC`, `LANCZOS` and `SPLINE`.
Note that `SPLINE` is completely different from Avisynth's builtin Spline resizers.

##### string colorspace = ""
Convert the output from whatever it was to the given colorspace, which can be one of `YV12`, `YUY2`, `RGB24` or `RGB32`.
Setting this to an empty string (the default) means keeping the same colorspace as the input.

##### bool utf8 = false
Does the same thing as in `FFIndex()`; see that function for details.

##### string varprefix = ""
A string that is added as a prefix to all exported Avisynth variables.
This makes it possible to differentiate between variables from different clips.
For convenience the last used FFMS function in a script sets the global variable `FFVAR_PREFIX` to its own variable prefix so that `FFInfo()` can default to it.

### FFAudioSource
```
FFAudioSource(string source, int track = -1, bool cache = true,
    string cachefile = source + ".ffindex", int adjustdelay = -1, bool utf8 = false,
string varprefix = "")
```
Opens audio.
Invokes indexing of all tracks if no valid index file is found, or if the requested track isn't present in the index.

#### Arguments
Are exactly the same as to `FFVideoSource`, with one exception:

##### int adjustdelay = -1
Controls how audio delay is handled, i.e. what happens if the first audio sample in the file doesn't have a timestamp of zero.
The following arguments are valid:

 - **-3**: No adjustment is made; the first decodable audio sample becomes the first sample in the output.
 - **-2**: Samples are created (with silence) or discarded so that sample 0 in the decoded audio starts at time zero.
 - **-1**: Samples are created (with silence) or discarded so that sample 0 in the decoded audio starts at the same time as frame 0 of the first video track. This is the default, and probably what most people want.
 - **Any integer >= 0**: Same as -1, but adjust relative to the video track with the given track number instead.
   If the provided track number isn't a video track, an error is raised.

-2 obviously does the same thing as -1 if the first video frame of the first video track starts at time zero.
In some containers this will always be the case, in others (most notably 188-byte MPEG TS) it will almost never happen.

### FFmpegSource2
```
FFmpegSource2(string source, int vtrack = -1, int atrack = -2, bool cache = true,
    string cachefile = source + ".ffindex", int fpsnum = -1, int fpsden = 1,
    int threads = -1, string timecodes = "", int seekmode = 1,
    bool overwrite = false, int width = -1, int height = -1,
    string resizer = "BICUBIC", string colorspace = "", int rffmode = 0,
    int adjustdelay = -1, bool utf8 = false, string varprefix = "")
```
A convenience function that combines the functionality of `FFVideoSource` and `FFAudioSource`.
The arguments do the same thing as in `FFVideoSource` and `FFAudioSource`; see those functions for details.
`vtrack` and `atrack` are the video and audio track to open, respectively; setting `atrack` <= -2 means audio is disabled.

**Note:** this function is provided by `FFMS2.avsi` and is not available unless that script has been imported or autoloaded.</p>

### FFImageSource
```
FFImageSource(string source, int width = -1, int height = -1,
    string resizer = "BICUBIC", string colorspace = "", bool utf8 = false)
```
A convenience alias for `FFVideoSource`, with the options set optimally for using it as an image reader.
Disables caching and seeking for maximum compatiblity.

**Note:** this function is provided by `FFMS2.avsi` and is not available unless that script has been imported or autoloaded.

### SWScale
```
SWScale(clip, int width = -1, int height = -1, string resizer = "BICUBIC", string colorspace = "")
```
An image resizing and colorspace conversion filter.
Does nothing special; it's almost always a better idea to just use Avisynth's builtins instead.
Might potentially be useful for testing or odd experiments just because it does things in a different way from Avisynth.
See the relevant arguments to `FFVideoSource` for details.

### FFFormatTime
<pre>FFFormatTime(int ms)</pre>
A helper function used to format a time given in milliseconds into a h:mm:ss.ttt string.
Used internally by `FFInfo`.

**Note:** this function is provided by `FFMS2.avsi` and is not available unless that script has been imported or autoloaded.

### FFInfo
```
FFInfo(clip c, bool framenum = true, bool frametype = true, bool cfrtime = true,
    bool vfrtime = true, string varprefix = "")
```
A helper function similar to Avisynth's internal `Info()` function; shows general information about the current frame.
Note that not all values are exported in all source modes, so some information may not always be shown.
The arguments can be used to disable the drawing of certain information if so desired.
Use the varprefix argument to determine which clip you want information about.

**Note:** this function is provided by `FFMS2.avsi` and is not available unless that script has been imported or autoloaded.

### FFSetLogLevel
```
FFSetLogLevel(int Level = -8)
```
Sets the FFmpeg logging level, i.e. how much diagnostic spam it prints to STDERR.
Since most applications that open Avisynth scripts do not provide a way to display things printed to STDERR, and since it's rather hard to make any sense of the printed messages unless you're quite familiar with FFmpeg internals, the usefulness of this function is rather limited for end users. It's mostly intended for debugging.
Defaults to quiet (no messages printed); a list of meaningful values can be found in `libavutil/log.h`.

### FFGetLogLevel
```
FFGetLogLevel()
```
Returns the current log level, as an integer.

### FFGetVersion
```
FFGetVersion()
```
Returns the FFMS2 version, as a string.

## Exported Avisynth variables
All variable names are prefixed by the `varprefix` argument to the respective `FFVideoSource` or `FFAudioSource` call that generated them.</p>

##### FFSAR_NUM, FFSAR_DEN, FFSAR
The playback aspect ratio specified by the container.
`FFSAR_NUM` and `FFSAR_DEN` make up the rational number of the ratio; `FFSAR` is only provided for convenience and may not be set in case it cannot be calculated (i.e. if `FFSAR_DEN` is zero).

##### FFCROP_LEFT, FFCROP_RIGHT, FFCROP_TOP, FFCROP_BOTTOM
The on-playback cropping specified by the container.

##### FFCOLOR_SPACE
The colorimetry the input claims to be using. Only meaningful for YUV inputs.
The source for this variable is a metadata flag that can arbitrarily be set or manipulated at will by incompetent users or buggy programs without changing the actual video content, so blindly trusting its correctness is not recommended.

The value is exported as a cryptic numerical constant that matches the values in the MPEG-2 specification.
You can find the gory details in the FFMS2 API documentation, but the important ones are:

 - **0**: RGB (usually indicates the stream isn't actually YUV, but RGB flagged as YUV)
 - **1**: ITU-R Rec.709
 - **2**: Unknown or unspecified
 - **5 and 6**: ITU-R Rec.601

##### FFCOLOR_RANGE
The color range the input video claims to be using.
Much like FFCOLOR_SPACE, the source for this variable is a metadata flag that can freely be set to arbitrary values, so trusting it blindly might not be a good idea.

Note that using SWScale() or the width/height/colorspace parameters to FFVideoSource may under some circumstances change the output color range.

 - **0**: Unknown/unspecified
 - **1**: Limited range (usually 16-235)
 - **2**: Full range (0-255)

##### FFPICT_TYPE
The picture type of the most recently requested frame as the ASCII number of the character listed below.
Use `Chr()` to convert it to an actual letter in Avisynth. Use after_frame=true in Avisynth's conditional scripting for proper results.
Only set when rffmode=0.
The FFmpeg source definition of the characters:
```
I: Intra
P: Predicted
B: Bi-dir predicted
S: S(GMC)-VOP MPEG4
i: Switching Intra
p: Switching Predicted
b: FF_BI_TYPE (no good explanation available)
?: Unknown
```

##### FFVFR_TIME
The actual time of the source frame in milliseconds.
Only set when no type of CFR conversion is being done (`rffmode` and `fpsnum` left at their defaults).

##### FFCHANNEL_LAYOUT
The audio channel layout of the audio stream.
This is exported as a very cryptic integer that is constructed in the same way as the `dwChannelMask` property of the Windows `WAVEFORMATEXTENSIBLE` struct.
If you don't know what a `WAVEFORMATEXTENSIBLE` is or what the `dwChannelMask` does, don't worry about it.</dd>

##### FFVAR_PREFIX
The variable prefix of the last called FFMS source function.
Note that this is a global variable.
