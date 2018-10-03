#include <string>
#include <cstring>

#include <ffms.h>
#include <gtest/gtest.h>

extern "C" {
#include <libavutil/mem.h>
#include <libavutil/pixdesc.h>
#include <libavutil/sha.h>
}

#include "tests.h"

bool CheckFrame(const FFMS_Frame *Frame, const FFMS_FrameInfo *info, const TestFrameData *Data) {
    EQ_CHECK(info->PTS, Data->PTS);
    EQ_CHECK(!!info->KeyFrame, Data->Keyframe);
    EQ_CHECK(Frame->EncodedWidth, Data->Width);
    EQ_CHECK(Frame->EncodedHeight, Data->Height);

    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get((AVPixelFormat) Frame->ConvertedPixelFormat);
    NULL_CHECK(desc);

    EXPECT_STREQ(desc->name, Data->PixelFormat);
    if (!!strcmp(desc->name, Data->PixelFormat))
        return false;

    struct AVSHA *sha = av_sha_alloc();
    NULL_CHECK(sha);

    int ret = av_sha_init(sha, 256);
    EQ_CHECK(ret, 0);

    for (int i = 0; i < av_pix_fmt_count_planes((AVPixelFormat) Frame->ConvertedPixelFormat); i++) {
        const int subh = i == 0 ? 0 : desc->log2_chroma_h;
        const int subw = i == 0 ? 0 : desc->log2_chroma_w;
        for (int y = 0; y < Frame->EncodedHeight >> subh; y++)
            av_sha_update(sha, Frame->Data[i] + y * Frame->Linesize[i], Frame->EncodedWidth >> subw);
    }

    uint8_t digest[32];
    av_sha_final(sha, &digest[0]);
    av_free(sha);

    bool ok;

    EXPECT_TRUE((ok = !memcmp(&Data->SHA256[0], &digest[0], 32)));

    return ok;
}
