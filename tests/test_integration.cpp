#include <gtest/gtest.h>
#include <filesystem>
#include "types.hpp"
#include "splat_buffer.hpp"
#include "spatial_grid.hpp"
#include "compression.hpp"

namespace fs = std::filesystem;
using namespace ply2lcc;

class IntegrationTest : public ::testing::Test {
protected:
    std::string test_ply_file_;
    std::string test_lcc_dir_;

    void SetUp() override {
        // Look for test data in various locations
        std::vector<std::string> base_paths = {
            "../test_data",
            "test_data",
            "../../test_data"
        };

        for (const auto& base : base_paths) {
            // Try cheonan dataset first
            std::string cheonan_ply = base + "/cheonan/ply/point_cloud/iteration_100/point_cloud.ply";
            if (fs::exists(cheonan_ply)) {
                test_ply_file_ = cheonan_ply;
                test_lcc_dir_ = base + "/cheonan/lcc/LCC_Results";
                return;
            }
            // Try generic scene_ply structure
            std::string scene_dir = base + "/scene_ply/point_cloud";
            if (fs::exists(scene_dir)) {
                for (const auto& entry : fs::directory_iterator(scene_dir)) {
                    if (entry.is_directory() && entry.path().filename().string().find("iteration") == 0) {
                        std::string ply_path = entry.path().string() + "/point_cloud.ply";
                        if (fs::exists(ply_path)) {
                            test_ply_file_ = ply_path;
                            test_lcc_dir_ = base + "/scene_lcc/LCC_Results";
                            return;
                        }
                    }
                }
            }
        }

        if (test_ply_file_.empty()) {
            GTEST_SKIP() << "Test data not available";
        }
    }
};

TEST_F(IntegrationTest, ReadPLYFile) {
    SplatBuffer buffer;
    ASSERT_TRUE(buffer.initialize(test_ply_file_)) << buffer.error();

    std::vector<Splat> splats = buffer.to_vector();
    EXPECT_GT(splats.size(), 0u);
    EXPECT_EQ(splats.size(), buffer.size());
}

TEST_F(IntegrationTest, PLYBoundingBox) {
    SplatBuffer buffer;
    ASSERT_TRUE(buffer.initialize(test_ply_file_)) << buffer.error();

    BBox bbox = buffer.compute_bbox();

    // BBox should be valid (min < max)
    EXPECT_LT(bbox.min.x, bbox.max.x);
    EXPECT_LT(bbox.min.y, bbox.max.y);
    EXPECT_LT(bbox.min.z, bbox.max.z);
}

// NOTE: FullConversionPipeline test removed - use ConvertApp for end-to-end testing

TEST_F(IntegrationTest, CompareWithReferenceLCC) {
    if (!fs::exists(test_lcc_dir_)) {
        GTEST_SKIP() << "Reference LCC not available at " << test_lcc_dir_;
    }

    // Find data.bin
    std::string ref_data = test_lcc_dir_ + "/data.bin";
    if (!fs::exists(ref_data)) {
        ref_data = test_lcc_dir_ + "/Data.bin";
    }
    ASSERT_TRUE(fs::exists(ref_data)) << "Reference data.bin not found";

    // Verify data.bin is multiple of 32 bytes
    auto size = fs::file_size(ref_data);
    EXPECT_EQ(size % 32, 0u) << "Reference data.bin size not multiple of 32";

    // Verify shcoef.bin if exists
    std::string ref_sh = test_lcc_dir_ + "/shcoef.bin";
    if (!fs::exists(ref_sh)) {
        ref_sh = test_lcc_dir_ + "/Shcoef.bin";
    }
    if (fs::exists(ref_sh)) {
        auto sh_size = fs::file_size(ref_sh);
        EXPECT_EQ(sh_size % 64, 0u) << "Reference shcoef.bin size not multiple of 64";
        EXPECT_EQ(size / 32, sh_size / 64) << "Splat count mismatch";
    }
}
