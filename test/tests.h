#ifndef _FFMS2_TESTS_H
#define _FFMS2_TESTS_H

#include <ffms.h>
#include <gtest/gtest.h>

extern "C" {
#include <libavutil/mem.h>
#include <libavutil/pixdesc.h>
#include <libavutil/sha.h>
}

#include "data/data.h"

#define NULL_CHECK(arg) do { \
    EXPECT_NE(nullptr, arg); \
    if (arg == nullptr) \
        return false; \
    } while(0)

#define EQ_CHECK(arg1,arg2) do { \
    EXPECT_EQ(arg1, arg2); \
    if (arg1 != arg2) \
        return false; \
    } while (0)

#define STRINGIFY2(x) #x
#define STRINGIFY(x) STRINGIFY2(x)

#define TEST_ENTRY(file, data) { file, data, sizeof(data) / sizeof(*data) }

typedef struct TestDataMap {
    const char *Filename;
    const TestFrameData *TestData;
    const size_t TestDataLen;
} TestDataMap;

bool CheckFrame(const FFMS_Frame *Frame, const FFMS_FrameInfo *info, const TestFrameData *Data);

#endif
