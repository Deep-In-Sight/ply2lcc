#include <gtest/gtest.h>
#include <filesystem>
#include "types.hpp"
#include "ply_reader.hpp"
#include "lcc_writer.hpp"
#include "spatial_grid.hpp"
#include "meta_writer.hpp"
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
    PLYHeader header;
    std::vector<Splat> splats;

    ASSERT_TRUE(PLYReader::read_splats(test_ply_file_, splats, header));
    EXPECT_GT(splats.size(), 0u);
    EXPECT_GT(header.vertex_count, 0u);
    EXPECT_EQ(splats.size(), header.vertex_count);
}

TEST_F(IntegrationTest, PLYBoundingBox) {
    PLYHeader header;
    std::vector<Splat> splats;
    ASSERT_TRUE(PLYReader::read_splats(test_ply_file_, splats, header));

    // BBox should be valid (min < max)
    EXPECT_LT(header.bbox.min.x, header.bbox.max.x);
    EXPECT_LT(header.bbox.min.y, header.bbox.max.y);
    EXPECT_LT(header.bbox.min.z, header.bbox.max.z);
}

TEST_F(IntegrationTest, FullConversionPipeline) {
    // Create temp output directory
    std::string output_dir = "/tmp/ply2lcc_test_" + std::to_string(std::time(nullptr));
    fs::create_directories(output_dir);

    // Read PLY
    PLYHeader header;
    std::vector<Splat> splats;
    ASSERT_TRUE(PLYReader::read_splats(test_ply_file_, splats, header));

    // Compute ranges
    AttributeRanges ranges;
    int bands_per_channel = (header.has_sh && header.num_f_rest > 0) ? header.num_f_rest / 3 : 0;

    for (const auto& s : splats) {
        Vec3f linear_scale(std::exp(s.scale.x), std::exp(s.scale.y), std::exp(s.scale.z));
        ranges.expand_scale(linear_scale);
        ranges.expand_opacity(sigmoid(s.opacity));
        if (header.has_sh && bands_per_channel > 0) {
            for (int band = 0; band < bands_per_channel; ++band) {
                float r = s.f_rest[band];
                float g = s.f_rest[band + bands_per_channel];
                float b = s.f_rest[band + 2 * bands_per_channel];
                ranges.expand_sh(r, g, b);
            }
        }
    }

    // Build grid
    SpatialGrid grid(30.0f, 30.0f, header.bbox, 1);
    for (size_t i = 0; i < splats.size(); ++i) {
        grid.add_splat(0, splats[i].pos, i);
    }
    EXPECT_GT(grid.get_cells().size(), 0u);

    // Write LCC
    LCCWriter writer(output_dir, ranges, 1, header.has_sh);
    for (const auto& [cell_index, cell] : grid.get_cells()) {
        if (cell.splat_indices[0].empty()) continue;

        std::vector<Splat> cell_splats;
        for (size_t idx : cell.splat_indices[0]) {
            cell_splats.push_back(splats[idx]);
        }
        ASSERT_TRUE(writer.write_splats(cell_index, 0, cell_splats));
    }
    writer.finalize();

    // Write index.bin
    ASSERT_TRUE(grid.write_index_bin(output_dir + "/index.bin", writer.get_units(), 1));

    // Write meta.lcc
    MetaInfo meta;
    meta.guid = MetaWriter::generate_guid();
    meta.total_splats = writer.total_splats();
    meta.total_levels = 1;
    meta.cell_length_x = 30.0f;
    meta.cell_length_y = 30.0f;
    meta.index_data_size = 20;
    meta.splats_per_lod = {splats.size()};
    meta.bounding_box = header.bbox;
    meta.file_type = header.has_sh ? "Quality" : "Portable";
    meta.attr_ranges = ranges;
    ASSERT_TRUE(MetaWriter::write(output_dir + "/meta.lcc", meta));

    // Verify output files exist
    EXPECT_TRUE(fs::exists(output_dir + "/data.bin"));
    EXPECT_TRUE(fs::exists(output_dir + "/index.bin"));
    EXPECT_TRUE(fs::exists(output_dir + "/meta.lcc"));
    if (header.has_sh) {
        EXPECT_TRUE(fs::exists(output_dir + "/shcoef.bin"));
    }

    // Verify data.bin size
    auto data_size = fs::file_size(output_dir + "/data.bin");
    EXPECT_EQ(data_size, splats.size() * 32);

    // Verify shcoef.bin size if exists
    if (header.has_sh) {
        auto sh_size = fs::file_size(output_dir + "/shcoef.bin");
        EXPECT_EQ(sh_size, splats.size() * 64);
    }

    // Cleanup
    fs::remove_all(output_dir);
}

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
