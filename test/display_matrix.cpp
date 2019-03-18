#include <cfloat>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include <ffms.h>
#include <gtest/gtest.h>

#include "tests.h"

namespace {

class DisplayMatrixTest : public ::testing::Test {
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

void DisplayMatrixTest::SetUp() {
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

void DisplayMatrixTest::TearDown() {
    FFMS_DestroyIndex(index);
    FFMS_DestroyVideoSource(video_source);
    FFMS_Deinit();
}

bool DisplayMatrixTest::DoIndexing(std::string file_name) {
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

TEST_F(DisplayMatrixTest, HFlip270) {
    std::string FilePath = SamplesDir + "/qrvideo_hflip_90.mov";

    ASSERT_TRUE(DoIndexing(FilePath));

    ASSERT_EQ(VP->Flip, 1);
    ASSERT_EQ(VP->Rotation, 90);
}

TEST_F(DisplayMatrixTest, HFlip90) {
    std::string FilePath = SamplesDir + "/qrvideo_hflip_270.mov";

    ASSERT_TRUE(DoIndexing(FilePath));

    ASSERT_EQ(VP->Flip, 1);
    ASSERT_EQ(VP->Rotation, 270);
}

TEST_F(DisplayMatrixTest, VFlip180) {
    std::string FilePath = SamplesDir + "/qrvideo_vflip.mov";

    ASSERT_TRUE(DoIndexing(FilePath));

    ASSERT_EQ(VP->Flip, 1);
    ASSERT_EQ(VP->Rotation, 180);
}

} //namespace

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
