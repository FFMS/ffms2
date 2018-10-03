#include <algorithm>
#include <cstring>
#include <string>
#include <random>
#include <vector>

#include <ffms.h>
#include <gtest/gtest.h>

#include "data/test.mp4.cpp"
#include "tests.h"


namespace {

const TestDataMap TestFiles[] = {
    TEST_ENTRY("test.mp4", testmp4data)
};

class IndexerTest : public ::testing::TestWithParam<TestDataMap> {
protected:
    virtual void SetUp();
    virtual void TearDown();
    bool DoIndexing(std::string);

    FFMS_Indexer* indexer;
    FFMS_Index* index;
    int video_track_idx;
    FFMS_VideoSource* video_source;
    const FFMS_VideoProperties* VP;

    FFMS_ErrorInfo E;
    char ErrorMsg[1024];

    std::string SamplesDir;
};

void IndexerTest::SetUp() {
    indexer = nullptr;
    index = nullptr;
    video_track_idx = -1;
    video_source = nullptr;
    VP = nullptr;
    E.Buffer = ErrorMsg;
    E.BufferSize = sizeof(ErrorMsg);
    SamplesDir = STRINGIFY(SAMPLES_DIR);

    FFMS_Init(0,0);
}

void IndexerTest::TearDown() {
    FFMS_DestroyIndex(index);
    FFMS_DestroyVideoSource(video_source);
    FFMS_Deinit();
}

bool IndexerTest::DoIndexing(std::string file_name) {
    indexer = FFMS_CreateIndexer(file_name.c_str(), &E);
    NULL_CHECK(indexer);
    FFMS_TrackTypeIndexSettings(indexer, FFMS_TYPE_VIDEO, 1, 0);

    index = FFMS_DoIndexing2(indexer, 0, &E);
    NULL_CHECK(index);

    video_track_idx = FFMS_GetFirstTrackOfType(index, FFMS_TYPE_VIDEO, &E);
    EXPECT_GE(0, video_track_idx);

    video_source = FFMS_CreateVideoSource(file_name.c_str(), video_track_idx, index, 1, FFMS_SEEK_NORMAL, &E);
    NULL_CHECK(video_source);

    VP = FFMS_GetVideoProperties(video_source); // Can't fail

    return true;
}

TEST_P(IndexerTest, ValidateFrameCount) {
    TestDataMap P = GetParam();
    std::string FilePath = SamplesDir + "/" + P.Filename;

    ASSERT_TRUE(DoIndexing(FilePath));

    ASSERT_EQ(P.TestDataLen, VP->NumFrames);
}

TEST_P(IndexerTest, ReverseAccessingFrame) {
    TestDataMap P = GetParam();
    std::string FilePath = SamplesDir + "/" + P.Filename;

    ASSERT_TRUE(DoIndexing(FilePath));
    for (int i = VP->NumFrames - 1; i > 0; i--) {
        std::stringstream ss;
        ss << "Testing Frame: " << i;
        SCOPED_TRACE(ss.str());

        FFMS_Track *track = FFMS_GetTrackFromIndex(index, video_track_idx);
        const FFMS_FrameInfo *info = FFMS_GetFrameInfo(track, i);

        const FFMS_Frame* frame = FFMS_GetFrame(video_source, i, &E);
        ASSERT_NE(nullptr, frame);
        ASSERT_TRUE(CheckFrame(frame, info, &P.TestData[i]));
    }
}

TEST_P(IndexerTest, RandomAccessingFrame) {
    TestDataMap P = GetParam();
    std::string FilePath = SamplesDir + "/" + P.Filename;

    ASSERT_TRUE(DoIndexing(FilePath));

    std::vector<int> FrameNums;
    for (int i = 0; i < VP->NumFrames; i++)
        FrameNums.push_back(i);

    std::random_device Device;
    std::mt19937 Gen(Device());

    std::shuffle(FrameNums.begin(), FrameNums.end(), Gen);

    for (int i = 0; i < VP->NumFrames; i++) {
        int num = FrameNums[i];

        std::stringstream ss;
        ss << "Testing Frame: " << num;
        SCOPED_TRACE(ss.str());

        FFMS_Track *track = FFMS_GetTrackFromIndex(index, video_track_idx);
        const FFMS_FrameInfo *info = FFMS_GetFrameInfo(track, num);

        const FFMS_Frame* frame = FFMS_GetFrame(video_source, num, &E);
        ASSERT_NE(nullptr, frame);
        ASSERT_TRUE(CheckFrame(frame, info, &P.TestData[num]));
    }
}

INSTANTIATE_TEST_CASE_P(ValidateIndexer, IndexerTest, ::testing::ValuesIn(TestFiles));

} //namespace

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
