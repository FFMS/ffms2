//  Copyright (c) 2007-2011 Fredrik Mellbin
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  THE SOFTWARE.

#include "codectype.h"

#include "matroskaparser.h"
#include "utils.h"

typedef struct CodecTags {
	char str[20];
	FFMS_CodecID id;
} CodecTags;

static const CodecTags mkv_codec_tags[] = {
	{"A_AAC"               , FFMS_ID(AAC)},
	{"A_AC3"               , FFMS_ID(AC3)},
	{"A_DTS"               , FFMS_ID(DTS)},
	{"A_EAC3"              , FFMS_ID(EAC3)},
	{"A_FLAC"              , FFMS_ID(FLAC)},
	{"A_MLP"               , FFMS_ID(MLP)},
	{"A_MPEG/L2"           , FFMS_ID(MP2)},
	{"A_MPEG/L1"           , FFMS_ID(MP2)},
	{"A_MPEG/L3"           , FFMS_ID(MP3)},
	{"A_OPUS"              , FFMS_ID(OPUS)},
	{"A_OPUS/EXPERIMENTAL" , FFMS_ID(OPUS)}, /* Pre-final ID, for compatibility */
	{"A_PCM/FLOAT/IEEE"    , FFMS_ID(PCM_F32LE)},
	{"A_PCM/FLOAT/IEEE"    , FFMS_ID(PCM_F64LE)},
	{"A_PCM/INT/BIG"       , FFMS_ID(PCM_S16BE)},
	{"A_PCM/INT/BIG"       , FFMS_ID(PCM_S24BE)},
	{"A_PCM/INT/BIG"       , FFMS_ID(PCM_S32BE)},
	{"A_PCM/INT/LIT"       , FFMS_ID(PCM_S16LE)},
	{"A_PCM/INT/LIT"       , FFMS_ID(PCM_S24LE)},
	{"A_PCM/INT/LIT"       , FFMS_ID(PCM_S32LE)},
	{"A_PCM/INT/LIT"       , FFMS_ID(PCM_U8)},
	{"A_QUICKTIME/QDM2"    , FFMS_ID(QDM2)},
	{"A_REAL/14_4"         , FFMS_ID(RA_144)},
	{"A_REAL/28_8"         , FFMS_ID(RA_288)},
	{"A_REAL/ATRC"         , FFMS_ID(ATRAC3)},
	{"A_REAL/COOK"         , FFMS_ID(COOK)},
	{"A_REAL/SIPR"         , FFMS_ID(SIPR)},
	{"A_TRUEHD"            , FFMS_ID(TRUEHD)},
	{"A_TTA1"              , FFMS_ID(TTA)},
	{"A_VORBIS"            , FFMS_ID(VORBIS)},
	{"A_WAVPACK4"          , FFMS_ID(WAVPACK)},

	{"S_TEXT/UTF8"         , FFMS_ID(TEXT)},
	{"S_TEXT/UTF8"         , FFMS_ID(SRT)},
	{"S_TEXT/ASCII"        , FFMS_ID(TEXT)},
	{"S_TEXT/ASS"          , FFMS_ID(SSA)},
	{"S_TEXT/SSA"          , FFMS_ID(SSA)},
	{"S_ASS"               , FFMS_ID(SSA)},
	{"S_SSA"               , FFMS_ID(SSA)},
	{"S_VOBSUB"            , FFMS_ID(DVD_SUBTITLE)},
	{"S_HDMV/PGS"          , FFMS_ID(HDMV_PGS_SUBTITLE)},

	{"V_DIRAC"             , FFMS_ID(DIRAC)},
	{"V_MJPEG"             , FFMS_ID(MJPEG)},
	{"V_MPEG1"             , FFMS_ID(MPEG1VIDEO)},
	{"V_MPEG2"             , FFMS_ID(MPEG2VIDEO)},
	{"V_MPEG4/ISO/ASP"     , FFMS_ID(MPEG4)},
	{"V_MPEG4/ISO/AP"      , FFMS_ID(MPEG4)},
	{"V_MPEG4/ISO/SP"      , FFMS_ID(MPEG4)},
	{"V_MPEG4/ISO/AVC"     , FFMS_ID(H264)},
	{"V_MPEG4/MS/V3"       , FFMS_ID(MSMPEG4V3)},
	{"V_MPEGH/ISO/HEVC"    , FFMS_ID(H265)},
	{"V_REAL/RV10"         , FFMS_ID(RV10)},
	{"V_REAL/RV20"         , FFMS_ID(RV20)},
	{"V_REAL/RV30"         , FFMS_ID(RV30)},
	{"V_REAL/RV40"         , FFMS_ID(RV40)},
#if LIBAVCODEC_VERSION_MAJOR < 55
	{"V_SNOW"              , FFMS_ID(SNOW)},
#endif
	{"V_THEORA"            , FFMS_ID(THEORA)},
	{"V_UNCOMPRESSED"      , FFMS_ID(RAWVIDEO)},
	{"V_VP8"               , FFMS_ID(VP8)},

	{""                    , FFMS_ID(NONE)}
};

typedef struct AVCodecTag {
	enum FFMS_CodecID id;
	unsigned int tag;
} AVCodecTag;

#if VERSION_CHECK(LIBAVFORMAT_VERSION_INT, <, 54, 1, 0, 53, 32, 100) && !defined(RETARDED_LIBAV_RELEASE_BRANCH_WHICH_INTRODUCED_AVFORMAT_GET_RIFF_VIDEO_TAGS_AT_A_VERSION_NUMBER_WHICH_HAD_ALREADY_BEEN_USED)
static const AVCodecTag codec_bmp_tags[] = {
	{ CODEC_ID_H264,         MKTAG('H', '2', '6', '4') },
	{ CODEC_ID_H264,         MKTAG('h', '2', '6', '4') },
	{ CODEC_ID_H264,         MKTAG('X', '2', '6', '4') },
	{ CODEC_ID_H264,         MKTAG('x', '2', '6', '4') },
	{ CODEC_ID_H264,         MKTAG('a', 'v', 'c', '1') },
	{ CODEC_ID_H264,         MKTAG('D', 'A', 'V', 'C') },
	{ CODEC_ID_H264,         MKTAG('V', 'S', 'S', 'H') },
	{ CODEC_ID_H264,         MKTAG('Q', '2', '6', '4') }, /* QNAP surveillance system */
	{ CODEC_ID_H263,         MKTAG('H', '2', '6', '3') },
	{ CODEC_ID_H263,         MKTAG('X', '2', '6', '3') },
	{ CODEC_ID_H263,         MKTAG('T', '2', '6', '3') },
	{ CODEC_ID_H263,         MKTAG('L', '2', '6', '3') },
	{ CODEC_ID_H263,         MKTAG('V', 'X', '1', 'K') },
	{ CODEC_ID_H263,         MKTAG('Z', 'y', 'G', 'o') },
	{ CODEC_ID_H263,         MKTAG('M', '2', '6', '3') },
	{ CODEC_ID_H263P,        MKTAG('H', '2', '6', '3') },
	{ CODEC_ID_H263I,        MKTAG('I', '2', '6', '3') }, /* intel h263 */
	{ CODEC_ID_H261,         MKTAG('H', '2', '6', '1') },
	{ CODEC_ID_H263P,        MKTAG('U', '2', '6', '3') },
	{ CODEC_ID_H263P,        MKTAG('v', 'i', 'v', '1') },
	{ CODEC_ID_MPEG4,        MKTAG('F', 'M', 'P', '4') },
	{ CODEC_ID_MPEG4,        MKTAG('D', 'I', 'V', 'X') },
	{ CODEC_ID_MPEG4,        MKTAG('D', 'X', '5', '0') },
	{ CODEC_ID_MPEG4,        MKTAG('X', 'V', 'I', 'D') },
	{ CODEC_ID_MPEG4,        MKTAG('M', 'P', '4', 'S') },
	{ CODEC_ID_MPEG4,        MKTAG('M', '4', 'S', '2') },
	{ CODEC_ID_MPEG4,        MKTAG( 4 ,  0 ,  0 ,  0 ) }, /* some broken avi use this */
	{ CODEC_ID_MPEG4,        MKTAG('D', 'I', 'V', '1') },
	{ CODEC_ID_MPEG4,        MKTAG('B', 'L', 'Z', '0') },
	{ CODEC_ID_MPEG4,        MKTAG('m', 'p', '4', 'v') },
	{ CODEC_ID_MPEG4,        MKTAG('U', 'M', 'P', '4') },
	{ CODEC_ID_MPEG4,        MKTAG('W', 'V', '1', 'F') },
	{ CODEC_ID_MPEG4,        MKTAG('S', 'E', 'D', 'G') },
	{ CODEC_ID_MPEG4,        MKTAG('R', 'M', 'P', '4') },
	{ CODEC_ID_MPEG4,        MKTAG('3', 'I', 'V', '2') },
	{ CODEC_ID_MPEG4,        MKTAG('W', 'A', 'W', 'V') }, /* WaWv MPEG-4 Video Codec */
	{ CODEC_ID_MPEG4,        MKTAG('F', 'F', 'D', 'S') },
	{ CODEC_ID_MPEG4,        MKTAG('F', 'V', 'F', 'W') },
	{ CODEC_ID_MPEG4,        MKTAG('D', 'C', 'O', 'D') },
	{ CODEC_ID_MPEG4,        MKTAG('M', 'V', 'X', 'M') },
	{ CODEC_ID_MPEG4,        MKTAG('P', 'M', '4', 'V') },
	{ CODEC_ID_MPEG4,        MKTAG('S', 'M', 'P', '4') },
	{ CODEC_ID_MPEG4,        MKTAG('D', 'X', 'G', 'M') },
	{ CODEC_ID_MPEG4,        MKTAG('V', 'I', 'D', 'M') },
	{ CODEC_ID_MPEG4,        MKTAG('M', '4', 'T', '3') },
	{ CODEC_ID_MPEG4,        MKTAG('G', 'E', 'O', 'X') },
	{ CODEC_ID_MPEG4,        MKTAG('H', 'D', 'X', '4') }, /* flipped video */
	{ CODEC_ID_MPEG4,        MKTAG('D', 'M', 'K', '2') },
	{ CODEC_ID_MPEG4,        MKTAG('D', 'I', 'G', 'I') },
	{ CODEC_ID_MPEG4,        MKTAG('I', 'N', 'M', 'C') },
	{ CODEC_ID_MPEG4,        MKTAG('E', 'P', 'H', 'V') }, /* Ephv MPEG-4 */
	{ CODEC_ID_MPEG4,        MKTAG('E', 'M', '4', 'A') },
	{ CODEC_ID_MPEG4,        MKTAG('M', '4', 'C', 'C') }, /* Divio MPEG-4 */
	{ CODEC_ID_MPEG4,        MKTAG('S', 'N', '4', '0') },
	{ CODEC_ID_MPEG4,        MKTAG('V', 'S', 'P', 'X') },
	{ CODEC_ID_MPEG4,        MKTAG('U', 'L', 'D', 'X') },
	{ CODEC_ID_MPEG4,        MKTAG('G', 'E', 'O', 'V') },
	{ CODEC_ID_MPEG4,        MKTAG('S', 'I', 'P', 'P') }, /* Samsung SHR-6040 */
	{ CODEC_ID_MPEG4,        MKTAG('X', 'V', 'I', 'X') },
	{ CODEC_ID_MPEG4,        MKTAG('D', 'r', 'e', 'X') },
	{ CODEC_ID_MPEG4,        MKTAG('Q', 'M', 'P', '4') }, /* QNAP Systems */
	{ CODEC_ID_MSMPEG4V3,    MKTAG('M', 'P', '4', '3') },
	{ CODEC_ID_MSMPEG4V3,    MKTAG('D', 'I', 'V', '3') },
	{ CODEC_ID_MSMPEG4V3,    MKTAG('M', 'P', 'G', '3') },
	{ CODEC_ID_MSMPEG4V3,    MKTAG('D', 'I', 'V', '5') },
	{ CODEC_ID_MSMPEG4V3,    MKTAG('D', 'I', 'V', '6') },
	{ CODEC_ID_MSMPEG4V3,    MKTAG('D', 'I', 'V', '4') },
	{ CODEC_ID_MSMPEG4V3,    MKTAG('D', 'V', 'X', '3') },
	{ CODEC_ID_MSMPEG4V3,    MKTAG('A', 'P', '4', '1') },
	{ CODEC_ID_MSMPEG4V3,    MKTAG('C', 'O', 'L', '1') },
	{ CODEC_ID_MSMPEG4V3,    MKTAG('C', 'O', 'L', '0') },
	{ CODEC_ID_MSMPEG4V2,    MKTAG('M', 'P', '4', '2') },
	{ CODEC_ID_MSMPEG4V2,    MKTAG('D', 'I', 'V', '2') },
	{ CODEC_ID_MSMPEG4V1,    MKTAG('M', 'P', 'G', '4') },
	{ CODEC_ID_MSMPEG4V1,    MKTAG('M', 'P', '4', '1') },
	{ CODEC_ID_WMV1,         MKTAG('W', 'M', 'V', '1') },
	{ CODEC_ID_WMV2,         MKTAG('W', 'M', 'V', '2') },
	{ CODEC_ID_DVVIDEO,      MKTAG('d', 'v', 's', 'd') },
	{ CODEC_ID_DVVIDEO,      MKTAG('d', 'v', 'h', 'd') },
	{ CODEC_ID_DVVIDEO,      MKTAG('d', 'v', 'h', '1') },
	{ CODEC_ID_DVVIDEO,      MKTAG('d', 'v', 's', 'l') },
	{ CODEC_ID_DVVIDEO,      MKTAG('d', 'v', '2', '5') },
	{ CODEC_ID_DVVIDEO,      MKTAG('d', 'v', '5', '0') },
	{ CODEC_ID_DVVIDEO,      MKTAG('c', 'd', 'v', 'c') }, /* Canopus DV */
	{ CODEC_ID_DVVIDEO,      MKTAG('C', 'D', 'V', 'H') }, /* Canopus DV */
	{ CODEC_ID_DVVIDEO,      MKTAG('C', 'D', 'V', '5') }, /* Canopus DV */
	{ CODEC_ID_DVVIDEO,      MKTAG('d', 'v', 'c', ' ') },
	{ CODEC_ID_DVVIDEO,      MKTAG('d', 'v', 'c', 's') },
	{ CODEC_ID_DVVIDEO,      MKTAG('d', 'v', 'h', '1') },
	{ CODEC_ID_MPEG1VIDEO,   MKTAG('m', 'p', 'g', '1') },
	{ CODEC_ID_MPEG1VIDEO,   MKTAG('m', 'p', 'g', '2') },
	{ CODEC_ID_MPEG2VIDEO,   MKTAG('m', 'p', 'g', '2') },
	{ CODEC_ID_MPEG2VIDEO,   MKTAG('M', 'P', 'E', 'G') },
	{ CODEC_ID_MPEG1VIDEO,   MKTAG('P', 'I', 'M', '1') },
	{ CODEC_ID_MPEG2VIDEO,   MKTAG('P', 'I', 'M', '2') },
	{ CODEC_ID_MPEG1VIDEO,   MKTAG('V', 'C', 'R', '2') },
	{ CODEC_ID_MPEG1VIDEO,   MKTAG( 1 ,  0 ,  0 ,  16) },
	{ CODEC_ID_MPEG2VIDEO,   MKTAG( 2 ,  0 ,  0 ,  16) },
	{ CODEC_ID_MPEG4,        MKTAG( 4 ,  0 ,  0 ,  16) },
	{ CODEC_ID_MPEG2VIDEO,   MKTAG('D', 'V', 'R', ' ') },
	{ CODEC_ID_MPEG2VIDEO,   MKTAG('M', 'M', 'E', 'S') },
	{ CODEC_ID_MPEG2VIDEO,   MKTAG('L', 'M', 'P', '2') }, /* Lead MPEG2 in avi */
	{ CODEC_ID_MPEG2VIDEO,   MKTAG('s', 'l', 'i', 'f') },
	{ CODEC_ID_MPEG2VIDEO,   MKTAG('E', 'M', '2', 'V') },
	{ CODEC_ID_MPEG2VIDEO,   MKTAG('M', '7', '0', '1') }, /* Matrox MPEG2 intra-only */
	{ CODEC_ID_MPEG2VIDEO,   MKTAG('m', 'p', 'g', 'v') },
	{ CODEC_ID_MJPEG,        MKTAG('M', 'J', 'P', 'G') },
	{ CODEC_ID_MJPEG,        MKTAG('L', 'J', 'P', 'G') },
	{ CODEC_ID_MJPEG,        MKTAG('d', 'm', 'b', '1') },
	{ CODEC_ID_MJPEG,        MKTAG('m', 'j', 'p', 'a') },
	{ CODEC_ID_LJPEG,        MKTAG('L', 'J', 'P', 'G') },
	{ CODEC_ID_MJPEG,        MKTAG('J', 'P', 'G', 'L') }, /* Pegasus lossless JPEG */
	{ CODEC_ID_JPEGLS,       MKTAG('M', 'J', 'L', 'S') }, /* JPEG-LS custom FOURCC for avi - encoder */
	{ CODEC_ID_JPEGLS,       MKTAG('M', 'J', 'P', 'G') },
	{ CODEC_ID_MJPEG,        MKTAG('M', 'J', 'L', 'S') }, /* JPEG-LS custom FOURCC for avi - decoder */
	{ CODEC_ID_MJPEG,        MKTAG('j', 'p', 'e', 'g') },
	{ CODEC_ID_MJPEG,        MKTAG('I', 'J', 'P', 'G') },
	{ CODEC_ID_MJPEG,        MKTAG('A', 'V', 'R', 'n') },
	{ CODEC_ID_MJPEG,        MKTAG('A', 'C', 'D', 'V') },
	{ CODEC_ID_MJPEG,        MKTAG('Q', 'I', 'V', 'G') },
	{ CODEC_ID_MJPEG,        MKTAG('S', 'L', 'M', 'J') }, /* SL M-JPEG */
	{ CODEC_ID_MJPEG,        MKTAG('C', 'J', 'P', 'G') }, /* Creative Webcam JPEG */
	{ CODEC_ID_MJPEG,        MKTAG('I', 'J', 'L', 'V') }, /* Intel JPEG Library Video Codec */
	{ CODEC_ID_MJPEG,        MKTAG('M', 'V', 'J', 'P') }, /* Midvid JPEG Video Codec */
	{ CODEC_ID_MJPEG,        MKTAG('A', 'V', 'I', '1') },
	{ CODEC_ID_MJPEG,        MKTAG('A', 'V', 'I', '2') },
	{ CODEC_ID_MJPEG,        MKTAG('M', 'T', 'S', 'J') },
	{ CODEC_ID_MJPEG,        MKTAG('Z', 'J', 'P', 'G') }, /* Paradigm Matrix M-JPEG Codec */
	{ CODEC_ID_MJPEG,        MKTAG('M', 'M', 'J', 'P') },
	{ CODEC_ID_HUFFYUV,      MKTAG('H', 'F', 'Y', 'U') },
	{ CODEC_ID_FFVHUFF,      MKTAG('F', 'F', 'V', 'H') },
	{ CODEC_ID_CYUV,         MKTAG('C', 'Y', 'U', 'V') },
	{ CODEC_ID_RAWVIDEO,     MKTAG( 0 ,  0 ,  0 ,  0 ) },
	{ CODEC_ID_RAWVIDEO,     MKTAG( 3 ,  0 ,  0 ,  0 ) },
	{ CODEC_ID_RAWVIDEO,     MKTAG('I', '4', '2', '0') },
	{ CODEC_ID_RAWVIDEO,     MKTAG('Y', 'U', 'Y', '2') },
	{ CODEC_ID_RAWVIDEO,     MKTAG('Y', '4', '2', '2') },
	{ CODEC_ID_RAWVIDEO,     MKTAG('V', '4', '2', '2') },
	{ CODEC_ID_RAWVIDEO,     MKTAG('Y', 'U', 'N', 'V') },
	{ CODEC_ID_RAWVIDEO,     MKTAG('U', 'Y', 'N', 'V') },
	{ CODEC_ID_RAWVIDEO,     MKTAG('U', 'Y', 'N', 'Y') },
	{ CODEC_ID_RAWVIDEO,     MKTAG('u', 'y', 'v', '1') },
	{ CODEC_ID_RAWVIDEO,     MKTAG('2', 'V', 'u', '1') },
	{ CODEC_ID_RAWVIDEO,     MKTAG('2', 'v', 'u', 'y') },
	{ CODEC_ID_RAWVIDEO,     MKTAG('y', 'u', 'v', 's') },
	{ CODEC_ID_RAWVIDEO,     MKTAG('P', '4', '2', '2') },
	{ CODEC_ID_RAWVIDEO,     MKTAG('Y', 'V', '1', '2') },
	{ CODEC_ID_RAWVIDEO,     MKTAG('Y', 'V', '1', '6') },
	{ CODEC_ID_RAWVIDEO,     MKTAG('Y', 'V', '2', '4') },
	{ CODEC_ID_RAWVIDEO,     MKTAG('U', 'Y', 'V', 'Y') },
	{ CODEC_ID_RAWVIDEO,     MKTAG('V', 'Y', 'U', 'Y') },
	{ CODEC_ID_RAWVIDEO,     MKTAG('I', 'Y', 'U', 'V') },
	{ CODEC_ID_RAWVIDEO,     MKTAG('Y', '8', '0', '0') },
	{ CODEC_ID_RAWVIDEO,     MKTAG('Y', '8', ' ', ' ') },
	{ CODEC_ID_RAWVIDEO,     MKTAG('H', 'D', 'Y', 'C') },
	{ CODEC_ID_RAWVIDEO,     MKTAG('Y', 'V', 'U', '9') },
	{ CODEC_ID_RAWVIDEO,     MKTAG('V', 'D', 'T', 'Z') }, /* SoftLab-NSK VideoTizer */
	{ CODEC_ID_RAWVIDEO,     MKTAG('Y', '4', '1', '1') },
	{ CODEC_ID_RAWVIDEO,     MKTAG('N', 'V', '1', '2') },
	{ CODEC_ID_RAWVIDEO,     MKTAG('N', 'V', '2', '1') },
	{ CODEC_ID_RAWVIDEO,     MKTAG('Y', '4', '1', 'B') },
	{ CODEC_ID_RAWVIDEO,     MKTAG('Y', '4', '2', 'B') },
	{ CODEC_ID_RAWVIDEO,     MKTAG('Y', 'U', 'V', '9') },
	{ CODEC_ID_RAWVIDEO,     MKTAG('Y', 'V', 'U', '9') },
	{ CODEC_ID_RAWVIDEO,     MKTAG('a', 'u', 'v', '2') },
	{ CODEC_ID_FRWU,         MKTAG('F', 'R', 'W', 'U') },
	{ CODEC_ID_R10K,         MKTAG('R', '1', '0', 'k') },
	{ CODEC_ID_R210,         MKTAG('r', '2', '1', '0') },
	{ CODEC_ID_V210,         MKTAG('v', '2', '1', '0') },
#if VERSION_CHECK(LIBAVCODEC_VERSION_INT, >=, 53, 28, 0, 53, 44, 0)
	{ CODEC_ID_V410,         MKTAG('v', '2', '1', '0') },
#endif
	{ CODEC_ID_INDEO3,       MKTAG('I', 'V', '3', '1') },
	{ CODEC_ID_INDEO3,       MKTAG('I', 'V', '3', '2') },
	{ CODEC_ID_INDEO4,       MKTAG('I', 'V', '4', '1') },
	{ CODEC_ID_INDEO5,       MKTAG('I', 'V', '5', '0') },
	{ CODEC_ID_VP3,          MKTAG('V', 'P', '3', '1') },
	{ CODEC_ID_VP3,          MKTAG('V', 'P', '3', '0') },
	{ CODEC_ID_VP5,          MKTAG('V', 'P', '5', '0') },
	{ CODEC_ID_VP6,          MKTAG('V', 'P', '6', '0') },
	{ CODEC_ID_VP6,          MKTAG('V', 'P', '6', '1') },
	{ CODEC_ID_VP6,          MKTAG('V', 'P', '6', '2') },
	{ CODEC_ID_VP6F,         MKTAG('V', 'P', '6', 'F') },
	{ CODEC_ID_VP6F,         MKTAG('F', 'L', 'V', '4') },
	{ CODEC_ID_VP8,          MKTAG('V', 'P', '8', '0') },
	{ CODEC_ID_ASV1,         MKTAG('A', 'S', 'V', '1') },
	{ CODEC_ID_ASV2,         MKTAG('A', 'S', 'V', '2') },
	{ CODEC_ID_VCR1,         MKTAG('V', 'C', 'R', '1') },
	{ CODEC_ID_FFV1,         MKTAG('F', 'F', 'V', '1') },
	{ CODEC_ID_XAN_WC4,      MKTAG('X', 'x', 'a', 'n') },
	{ CODEC_ID_MIMIC,        MKTAG('L', 'M', '2', '0') },
	{ CODEC_ID_MSRLE,        MKTAG('m', 'r', 'l', 'e') },
	{ CODEC_ID_MSRLE,        MKTAG( 1 ,  0 ,  0 ,  0 ) },
	{ CODEC_ID_MSRLE,        MKTAG( 2 ,  0 ,  0 ,  0 ) },
	{ CODEC_ID_MSVIDEO1,     MKTAG('M', 'S', 'V', 'C') },
	{ CODEC_ID_MSVIDEO1,     MKTAG('m', 's', 'v', 'c') },
	{ CODEC_ID_MSVIDEO1,     MKTAG('C', 'R', 'A', 'M') },
	{ CODEC_ID_MSVIDEO1,     MKTAG('c', 'r', 'a', 'm') },
	{ CODEC_ID_MSVIDEO1,     MKTAG('W', 'H', 'A', 'M') },
	{ CODEC_ID_MSVIDEO1,     MKTAG('w', 'h', 'a', 'm') },
	{ CODEC_ID_CINEPAK,      MKTAG('c', 'v', 'i', 'd') },
	{ CODEC_ID_TRUEMOTION1,  MKTAG('D', 'U', 'C', 'K') },
	{ CODEC_ID_TRUEMOTION1,  MKTAG('P', 'V', 'E', 'Z') },
	{ CODEC_ID_MSZH,         MKTAG('M', 'S', 'Z', 'H') },
	{ CODEC_ID_ZLIB,         MKTAG('Z', 'L', 'I', 'B') },
	{ CODEC_ID_SNOW,         MKTAG('S', 'N', 'O', 'W') },
	{ CODEC_ID_4XM,          MKTAG('4', 'X', 'M', 'V') },
	{ CODEC_ID_FLV1,         MKTAG('F', 'L', 'V', '1') },
	{ CODEC_ID_FLASHSV,      MKTAG('F', 'S', 'V', '1') },
	{ CODEC_ID_SVQ1,         MKTAG('s', 'v', 'q', '1') },
	{ CODEC_ID_TSCC,         MKTAG('t', 's', 'c', 'c') },
	{ CODEC_ID_ULTI,         MKTAG('U', 'L', 'T', 'I') },
	{ CODEC_ID_VIXL,         MKTAG('V', 'I', 'X', 'L') },
	{ CODEC_ID_QPEG,         MKTAG('Q', 'P', 'E', 'G') },
	{ CODEC_ID_QPEG,         MKTAG('Q', '1', '.', '0') },
	{ CODEC_ID_QPEG,         MKTAG('Q', '1', '.', '1') },
	{ CODEC_ID_WMV3,         MKTAG('W', 'M', 'V', '3') },
	{ CODEC_ID_WMV3IMAGE,    MKTAG('W', 'M', 'V', 'P') },
	{ CODEC_ID_VC1,          MKTAG('W', 'V', 'C', '1') },
	{ CODEC_ID_VC1,          MKTAG('W', 'M', 'V', 'A') },
	{ CODEC_ID_VC1IMAGE,     MKTAG('W', 'V', 'P', '2') },
	{ CODEC_ID_LOCO,         MKTAG('L', 'O', 'C', 'O') },
	{ CODEC_ID_WNV1,         MKTAG('W', 'N', 'V', '1') },
	{ CODEC_ID_AASC,         MKTAG('A', 'A', 'S', 'C') },
	{ CODEC_ID_INDEO2,       MKTAG('R', 'T', '2', '1') },
	{ CODEC_ID_FRAPS,        MKTAG('F', 'P', 'S', '1') },
	{ CODEC_ID_THEORA,       MKTAG('t', 'h', 'e', 'o') },
	{ CODEC_ID_TRUEMOTION2,  MKTAG('T', 'M', '2', '0') },
	{ CODEC_ID_CSCD,         MKTAG('C', 'S', 'C', 'D') },
	{ CODEC_ID_ZMBV,         MKTAG('Z', 'M', 'B', 'V') },
	{ CODEC_ID_KMVC,         MKTAG('K', 'M', 'V', 'C') },
	{ CODEC_ID_CAVS,         MKTAG('C', 'A', 'V', 'S') },
	{ CODEC_ID_JPEG2000,     MKTAG('m', 'j', 'p', '2') },
	{ CODEC_ID_JPEG2000,     MKTAG('M', 'J', '2', 'C') },
	{ CODEC_ID_JPEG2000,     MKTAG('L', 'J', '2', 'C') },
	{ CODEC_ID_JPEG2000,     MKTAG('L', 'J', '2', 'K') },
	{ CODEC_ID_VMNC,         MKTAG('V', 'M', 'n', 'c') },
	{ CODEC_ID_TARGA,        MKTAG('t', 'g', 'a', ' ') },
	{ CODEC_ID_PNG,          MKTAG('M', 'P', 'N', 'G') },
	{ CODEC_ID_PNG,          MKTAG('P', 'N', 'G', '1') },
	{ CODEC_ID_CLJR,         MKTAG('c', 'l', 'j', 'r') },
	{ CODEC_ID_DIRAC,        MKTAG('d', 'r', 'a', 'c') },
	{ CODEC_ID_RPZA,         MKTAG('a', 'z', 'p', 'r') },
	{ CODEC_ID_RPZA,         MKTAG('R', 'P', 'Z', 'A') },
	{ CODEC_ID_RPZA,         MKTAG('r', 'p', 'z', 'a') },
	{ CODEC_ID_SP5X,         MKTAG('S', 'P', '5', '4') },
	{ CODEC_ID_AURA,         MKTAG('A', 'U', 'R', 'A') },
	{ CODEC_ID_AURA2,        MKTAG('A', 'U', 'R', '2') },
	{ CODEC_ID_DPX,          MKTAG('d', 'p', 'x', ' ') },
	{ CODEC_ID_KGV1,         MKTAG('K', 'G', 'V', '1') },
#if (LIBAVCODEC_VERSION_INT) >= (AV_VERSION_INT(52, 108, 0))
	{ CODEC_ID_LAGARITH,     MKTAG('L', 'A', 'G', 'S') },
#endif
#ifdef FFMS_USE_FFMPEG_COMPAT
	{ CODEC_ID_G2M,          MKTAG('G', '2', 'M', '2') },
	{ CODEC_ID_G2M,          MKTAG('G', '2', 'M', '3') },
	{ CODEC_ID_G2M,          MKTAG('G', '2', 'M', '4') },
#endif
	{ CODEC_ID_AMV,          MKTAG('A', 'M', 'V', 'F') },
	{ CODEC_ID_UTVIDEO,      MKTAG('U', 'L', 'R', 'A') },
	{ CODEC_ID_UTVIDEO,      MKTAG('U', 'L', 'R', 'G') },
	{ CODEC_ID_UTVIDEO,      MKTAG('U', 'L', 'Y', '0') },
	{ CODEC_ID_UTVIDEO,      MKTAG('U', 'L', 'Y', '2') },
	{ CODEC_ID_VBLE,         MKTAG('V', 'B', 'L', 'E') },
#if defined(FFMS_USE_FFMPEG_COMPAT) && (LIBAVCODEC_VERSION_INT) >= (AV_VERSION_INT(53, 43, 0))
	{ CODEC_ID_ESCAPE130,    MKTAG('E', '1', '3', '0') },
#endif
#if VERSION_CHECK(LIBAVCODEC_VERSION_INT, >=, 53, 28, 0, 53, 43, 0)
	{ CODEC_ID_DXTORY,       MKTAG('x', 't', 'o', 'r') },
#endif
	{ CODEC_ID_NONE,         0 }
};

static const AVCodecTag *avformat_get_riff_video_tags() {
	return codec_bmp_tags;
}
#endif

FFMS_TrackType HaaliTrackTypeToFFTrackType(int TT) {
	switch (TT) {
		case TT_VIDEO: return FFMS_TYPE_VIDEO; break;
		case TT_AUDIO: return FFMS_TYPE_AUDIO; break;
		case TT_SUB: return FFMS_TYPE_SUBTITLE; break;
		default: return FFMS_TYPE_UNKNOWN;
	}
}

const char *GetLAVCSampleFormatName(AVSampleFormat s) {
	switch (s) {
		case AV_SAMPLE_FMT_U8:	return "8-bit unsigned integer";
		case AV_SAMPLE_FMT_S16:	return "16-bit signed integer";
		case AV_SAMPLE_FMT_S32:	return "32-bit signed integer";
		case AV_SAMPLE_FMT_FLT:	return "Single-precision floating point";
		case AV_SAMPLE_FMT_DBL:	return "Double-precision floating point";
		default:				return "Unknown";
	}
}

FFMS_CodecID MatroskaToFFCodecID(char *Codec, void *CodecPrivate, unsigned int FourCC, unsigned int BitsPerSample) {
	/* Look up native codecs */
	for(int i = 0; mkv_codec_tags[i].id != FFMS_ID(NONE); i++){
		if(!strncmp(mkv_codec_tags[i].str, Codec,
			strlen(mkv_codec_tags[i].str))) {

				// Uncompressed and exotic format fixup
				// This list is incomplete
				FFMS_CodecID CID = mkv_codec_tags[i].id;
				switch (CID) {
					case FFMS_ID(PCM_S16LE):
						switch (BitsPerSample) {
							case 8: CID = FFMS_ID(PCM_S8); break;
							case 16: CID = FFMS_ID(PCM_S16LE); break;
							case 24: CID = FFMS_ID(PCM_S24LE); break;
							case 32: CID = FFMS_ID(PCM_S32LE); break;
						}
						break;
					case FFMS_ID(PCM_S16BE):
						switch (BitsPerSample) {
							case 8: CID = FFMS_ID(PCM_S8); break;
							case 16: CID = FFMS_ID(PCM_S16BE); break;
							case 24: CID = FFMS_ID(PCM_S24BE); break;
							case 32: CID = FFMS_ID(PCM_S32BE); break;
						}
						break;
					default:
						break;
				}

				return CID;
		}
	}

	/* Video codecs for "avi in mkv" mode */
	const AVCodecTag *const tags[] = { avformat_get_riff_video_tags(), 0 };

	if (!strcmp(Codec, "V_MS/VFW/FOURCC")) {
		FFMS_BITMAPINFOHEADER *b = reinterpret_cast<FFMS_BITMAPINFOHEADER *>(CodecPrivate);
		return av_codec_get_id(tags, b->biCompression);
	}

	if (!strcmp(Codec, "V_FOURCC")) {
		return av_codec_get_id(tags, FourCC);
	}

	// FIXME
	/* Audio codecs for "acm in mkv" mode */
	//#include "Mmreg.h"
	//((WAVEFORMATEX *)TI->CodecPrivate)->wFormatTag

	/* Fixup for uncompressed video formats */

	/* Fixup for uncompressed audio formats */

	return FFMS_ID(NONE);
}
