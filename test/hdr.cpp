#include <cfloat>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include <ffms.h>
#include <gtest/gtest.h>

#include "tests.h"

typedef struct HDR10Data {
    double MasteringDisplayPrimariesX[3];
    double MasteringDisplayPrimariesY[3];
    double MasteringDisplayWhitePointX;
    double MasteringDisplayWhitePointY;
    double MasteringDisplayMinLuminance;
    double MasteringDisplayMaxLuminance;
} HDR10Data;

// Tests relative error with an error of 1e-5, given the accuracy of
// the hardcoded test values, and how it would affect HDR.
#define TEST_DOUBLE(A, B) (fabs((A / B) - 1) < (double) 1e-5)

const HDR10Data StreamHDR10Data = {
    { 35400.0 / 50000.0, 8500.0 / 50000.0, 6550.0 / 50000.0 },
    { 14599.0 / 50000.0 , 39850.0 / 50000.0 , 2300.0 / 50000.0 },
    15634.0 / 50000.0,
    16450.0 / 50000.0,
    10.0 / 10000.0,
    10000000.0 / 10000.0
};

const HDR10Data ContainerHDR10Data {
    { 34000.0 / 50000.0, 13250.0 / 50000.0, 7500.0 / 50000.0 },
    { 16000.0 / 50000.0, 34500.0 / 50000.0, 3000.0 / 50000.0 },
    15635.0 / 50000.0,
    16450.0 / 50000.0,
    100.0 / 10000.0,
    10000000.0 / 10000.0
};

namespace {

class HDR10Test : public ::testing::Test {
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

void HDR10Test::SetUp() {
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

void HDR10Test::TearDown() {
    FFMS_DestroyIndex(index);
    FFMS_DestroyVideoSource(video_source);
}

bool HDR10Test::DoIndexing(std::string file_name) {
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

TEST_F(HDR10Test, StreamData) {
    std::string FilePath = SamplesDir + "/hdr10tags-stream.mp4";

    ASSERT_TRUE(DoIndexing(FilePath));

    ASSERT_TRUE(!!VP->HasMasteringDisplayPrimaries);
    for (int i = 0; i < 3; i++) {
        ASSERT_TRUE(TEST_DOUBLE(VP->MasteringDisplayPrimariesX[i], StreamHDR10Data.MasteringDisplayPrimariesX[i]));
        ASSERT_TRUE(TEST_DOUBLE(VP->MasteringDisplayPrimariesY[i], StreamHDR10Data.MasteringDisplayPrimariesY[i]));
    }
    ASSERT_TRUE(TEST_DOUBLE(VP->MasteringDisplayWhitePointX, StreamHDR10Data.MasteringDisplayWhitePointX));
    ASSERT_TRUE(TEST_DOUBLE(VP->MasteringDisplayWhitePointY, StreamHDR10Data.MasteringDisplayWhitePointY));

    ASSERT_TRUE(!!VP->HasMasteringDisplayLuminance);
    ASSERT_TRUE(TEST_DOUBLE(VP->MasteringDisplayMinLuminance, StreamHDR10Data.MasteringDisplayMinLuminance));
    ASSERT_TRUE(TEST_DOUBLE(VP->MasteringDisplayMaxLuminance, StreamHDR10Data.MasteringDisplayMaxLuminance));
}

TEST_F(HDR10Test, ContainerData) {
    std::string FilePath = SamplesDir + "/hdr10tags-container.mkv";

    ASSERT_TRUE(DoIndexing(FilePath));

    // Stream HDR metadata should be used.
    ASSERT_TRUE(!!VP->HasMasteringDisplayPrimaries);
    for (int i = 0; i < 3; i++) {
        ASSERT_TRUE(TEST_DOUBLE(VP->MasteringDisplayPrimariesX[i], ContainerHDR10Data.MasteringDisplayPrimariesX[i]));
        ASSERT_TRUE(TEST_DOUBLE(VP->MasteringDisplayPrimariesY[i], ContainerHDR10Data.MasteringDisplayPrimariesY[i]));
    }
    ASSERT_TRUE(TEST_DOUBLE(VP->MasteringDisplayWhitePointX, ContainerHDR10Data.MasteringDisplayWhitePointX));
    ASSERT_TRUE(TEST_DOUBLE(VP->MasteringDisplayWhitePointY, ContainerHDR10Data.MasteringDisplayWhitePointY));

    ASSERT_TRUE(!!VP->HasMasteringDisplayLuminance);
    ASSERT_TRUE(TEST_DOUBLE(VP->MasteringDisplayMinLuminance, ContainerHDR10Data.MasteringDisplayMinLuminance));
    ASSERT_TRUE(TEST_DOUBLE(VP->MasteringDisplayMaxLuminance, ContainerHDR10Data.MasteringDisplayMaxLuminance));
}

TEST_F(HDR10Test, StreamAndContainerData) {
    std::string FilePath = SamplesDir + "/hdr10tags-both.mkv";

    ASSERT_TRUE(DoIndexing(FilePath));

    // Stream HDR metadata should be used.
    ASSERT_TRUE(!!VP->HasMasteringDisplayPrimaries);
    for (int i = 0; i < 3; i++) {
        ASSERT_TRUE(TEST_DOUBLE(VP->MasteringDisplayPrimariesX[i], StreamHDR10Data.MasteringDisplayPrimariesX[i]));
        ASSERT_TRUE(TEST_DOUBLE(VP->MasteringDisplayPrimariesY[i], StreamHDR10Data.MasteringDisplayPrimariesY[i]));
    }
    ASSERT_TRUE(TEST_DOUBLE(VP->MasteringDisplayWhitePointX, StreamHDR10Data.MasteringDisplayWhitePointX));
    ASSERT_TRUE(TEST_DOUBLE(VP->MasteringDisplayWhitePointY, StreamHDR10Data.MasteringDisplayWhitePointY));

    ASSERT_TRUE(!!VP->HasMasteringDisplayLuminance);
    ASSERT_TRUE(TEST_DOUBLE(VP->MasteringDisplayMinLuminance, StreamHDR10Data.MasteringDisplayMinLuminance));
    ASSERT_TRUE(TEST_DOUBLE(VP->MasteringDisplayMaxLuminance, StreamHDR10Data.MasteringDisplayMaxLuminance));
}

} //namespace

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
