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
    std::string test_data_ply_;
    std::string test_data_lcc_;

    void SetUp() override {
        // Try multiple paths (running from build/ or project root)
        std::vector<std::string> base_paths = {
            "../test_data",           // Running from build/
            "test_data",              // Running from project root
            "../../test_data"         // Running from build/subdir
        };

        for (const auto& base : base_paths) {
            if (fs::exists(base + "/scene_ply")) {
                test_data_ply_ = base + "/scene_ply";
                test_data_lcc_ = base + "/scene_lcc";
                break;
            }
        }

        if (test_data_ply_.empty() || !fs::exists(test_data_ply_)) {
            GTEST_SKIP() << "Test data not available. Copy PLY files to test_data/scene_ply/ "
                         << "and reference LCC to test_data/scene_lcc/";
        }
    }

    std::string getTestPlyPath() {
        // Look for point_cloud.ply or any .ply file
        if (fs::exists(test_data_ply_ + "/point_cloud.ply")) {
            return test_data_ply_ + "/point_cloud.ply";
        }
        // Check in point_cloud subdirectory
        std::string subdir = test_data_ply_ + "/point_cloud/iteration_100";
        if (fs::exists(subdir)) {
            for (const auto& entry : fs::directory_iterator(subdir)) {
                if (entry.path().extension() == ".ply" &&
                    entry.path().filename().string().find("point_cloud") != std::string::npos) {
                    return entry.path().string();
                }
            }
        }
        // Fall back to any .ply in scene_ply
        for (const auto& entry : fs::directory_iterator(test_data_ply_)) {
            if (entry.path().extension() == ".ply") {
                return entry.path().string();
            }
        }
        return "";
    }

    std::string getReferenceLccPath() {
        // Check common locations for LCC reference data
        std::vector<std::string> paths = {
            test_data_lcc_ + "/LCC_Results",
            test_data_lcc_,
        };
        for (const auto& path : paths) {
            if (fs::exists(path + "/data.bin") || fs::exists(path + "/Data.bin")) {
                return path;
            }
        }
        return test_data_lcc_;
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
    std::string ref_dir = getReferenceLccPath();

    // Find meta.lcc file (might have different name)
    std::string ref_meta;
    for (const auto& entry : fs::directory_iterator(ref_dir)) {
        if (entry.path().extension() == ".lcc") {
            ref_meta = entry.path().string();
            break;
        }
    }
    if (ref_meta.empty()) {
        GTEST_SKIP() << "Reference .lcc file not found in " << ref_dir;
    }

    // Find data.bin
    std::string ref_data = ref_dir + "/data.bin";
    if (!fs::exists(ref_data)) {
        ref_data = ref_dir + "/Data.bin";
    }
    ASSERT_TRUE(fs::exists(ref_data)) << "Reference data.bin not found in " << ref_dir;

    // Verify data.bin is multiple of 32 bytes
    auto size = fs::file_size(ref_data);
    EXPECT_EQ(size % 32, 0u) << "Reference data.bin size not multiple of 32";

    // Verify shcoef.bin if exists
    std::string ref_sh = ref_dir + "/shcoef.bin";
    if (!fs::exists(ref_sh)) {
        ref_sh = ref_dir + "/Shcoef.bin";
    }
    if (fs::exists(ref_sh)) {
        auto sh_size = fs::file_size(ref_sh);
        EXPECT_EQ(sh_size % 64, 0u) << "Reference shcoef.bin size not multiple of 64";
        // Verify data.bin and shcoef.bin have consistent splat counts
        EXPECT_EQ(size / 32, sh_size / 64) << "Splat count mismatch between data.bin and shcoef.bin";
    }
}
