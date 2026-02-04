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
    static constexpr const char* TEST_DATA_PLY = "test_data/scene_ply";
    static constexpr const char* TEST_DATA_LCC = "test_data/scene_lcc";

    void SetUp() override {
        if (!fs::exists(TEST_DATA_PLY) || !fs::exists(TEST_DATA_LCC)) {
            GTEST_SKIP() << "Test data not available. Copy PLY files to test_data/scene_ply/ "
                         << "and reference LCC to test_data/scene_lcc/";
        }
    }

    std::string getTestPlyPath() {
        // Look for point_cloud.ply or any .ply file
        if (fs::exists(std::string(TEST_DATA_PLY) + "/point_cloud.ply")) {
            return std::string(TEST_DATA_PLY) + "/point_cloud.ply";
        }
        for (const auto& entry : fs::directory_iterator(TEST_DATA_PLY)) {
            if (entry.path().extension() == ".ply") {
                return entry.path().string();
            }
        }
        return "";
    }
};

TEST_F(IntegrationTest, ReadPLYFile) {
    std::string ply_path = getTestPlyPath();
    ASSERT_FALSE(ply_path.empty()) << "No PLY file found in test_data/scene_ply/";

    PLYHeader header;
    std::vector<Splat> splats;

    ASSERT_TRUE(PLYReader::read_splats(ply_path, splats, header));
    EXPECT_GT(splats.size(), 0u);
    EXPECT_GT(header.vertex_count, 0u);
    EXPECT_EQ(splats.size(), header.vertex_count);
}

TEST_F(IntegrationTest, PLYBoundingBox) {
    std::string ply_path = getTestPlyPath();
    ASSERT_FALSE(ply_path.empty());

    PLYHeader header;
    std::vector<Splat> splats;
    ASSERT_TRUE(PLYReader::read_splats(ply_path, splats, header));

    // BBox should be valid (min < max)
    EXPECT_LT(header.bbox.min.x, header.bbox.max.x);
    EXPECT_LT(header.bbox.min.y, header.bbox.max.y);
    EXPECT_LT(header.bbox.min.z, header.bbox.max.z);
}

TEST_F(IntegrationTest, FullConversionPipeline) {
    std::string ply_path = getTestPlyPath();
    ASSERT_FALSE(ply_path.empty());

    // Create temp output directory
    std::string output_dir = "/tmp/ply2lcc_test_" + std::to_string(std::time(nullptr));
    fs::create_directories(output_dir);

    // Read PLY
    PLYHeader header;
    std::vector<Splat> splats;
    ASSERT_TRUE(PLYReader::read_splats(ply_path, splats, header));

    // Compute ranges
    AttributeRanges ranges;
    for (const auto& s : splats) {
        Vec3f linear_scale(std::exp(s.scale.x), std::exp(s.scale.y), std::exp(s.scale.z));
        ranges.expand_scale(linear_scale);
        ranges.expand_opacity(sigmoid(s.opacity));
        if (header.has_sh) {
            for (int i = 0; i < 45; ++i) {
                ranges.expand_sh(s.f_rest[i]);
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

    // Write Index.bin
    ASSERT_TRUE(grid.write_index_bin(output_dir + "/Index.bin", writer.get_units(), 1));

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
    EXPECT_TRUE(fs::exists(output_dir + "/Data.bin"));
    EXPECT_TRUE(fs::exists(output_dir + "/Index.bin"));
    EXPECT_TRUE(fs::exists(output_dir + "/meta.lcc"));
    if (header.has_sh) {
        EXPECT_TRUE(fs::exists(output_dir + "/Shcoef.bin"));
    }

    // Verify Data.bin size
    auto data_size = fs::file_size(output_dir + "/Data.bin");
    EXPECT_EQ(data_size, splats.size() * 32);

    // Verify Shcoef.bin size if exists
    if (header.has_sh) {
        auto sh_size = fs::file_size(output_dir + "/Shcoef.bin");
        EXPECT_EQ(sh_size, splats.size() * 64);
    }

    // Cleanup
    fs::remove_all(output_dir);
}

TEST_F(IntegrationTest, CompareWithReferenceLCC) {
    // Check if reference LCC exists
    std::string ref_meta = std::string(TEST_DATA_LCC) + "/meta.lcc";
    if (!fs::exists(ref_meta)) {
        ref_meta = std::string(TEST_DATA_LCC) + "/converted_from_ply.lcc";
    }
    if (!fs::exists(ref_meta)) {
        GTEST_SKIP() << "Reference meta.lcc not found in test_data/scene_lcc/";
    }

    // Just verify reference files exist and have expected structure
    std::string ref_data = std::string(TEST_DATA_LCC) + "/data.bin";
    if (!fs::exists(ref_data)) {
        ref_data = std::string(TEST_DATA_LCC) + "/Data.bin";
    }
    EXPECT_TRUE(fs::exists(ref_data)) << "Reference data.bin not found";

    // Verify data.bin is multiple of 32 bytes
    if (fs::exists(ref_data)) {
        auto size = fs::file_size(ref_data);
        EXPECT_EQ(size % 32, 0u) << "Reference data.bin size not multiple of 32";
    }
}
