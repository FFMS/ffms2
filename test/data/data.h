#ifndef _TEST_DATA_H
#define _TEST_DATA_H

#include <cstdint>

typedef struct TestFrameData {
    int64_t PTS;
    int64_t Duration;
    int Width;
    int Height;
    const char *PixelFormat;
    bool Keyframe;
    uint8_t SHA256[32]; // SHA256 of the frame's decode planes
} TestFrameData;

#endif
