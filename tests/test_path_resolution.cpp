#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "path_resolution.hpp"

namespace fs = std::filesystem;
using namespace ply2lcc;

class PathResolutionTest : public ::testing::Test {
protected:
    std::string temp_dir_;

    void SetUp() override {
        temp_dir_ = "/tmp/path_resolution_test_" + std::to_string(std::time(nullptr));
        fs::create_directories(temp_dir_);
    }

    void TearDown() override {
        fs::remove_all(temp_dir_);
    }

    void create_structure(const std::vector<std::string>& paths) {
        for (const auto& p : paths) {
            fs::create_directories(temp_dir_ + "/" + p);
        }
    }

    void create_file(const std::string& path) {
        std::ofstream f(temp_dir_ + "/" + path);
        f << "dummy";
    }
};

TEST_F(PathResolutionTest, FindHighestIteration) {
    create_structure({
        "point_cloud/iteration_100",
        "point_cloud/iteration_7000",
        "point_cloud/iteration_30000"
    });
    create_file("point_cloud/iteration_30000/point_cloud.ply");

    auto result = resolve_input_path(temp_dir_);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->iteration_number, 30000);
    EXPECT_TRUE(result->path.find("iteration_30000") != std::string::npos);
}

TEST_F(PathResolutionTest, SingleIteration) {
    create_structure({"point_cloud/iteration_100"});
    create_file("point_cloud/iteration_100/point_cloud.ply");

    auto result = resolve_input_path(temp_dir_);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->iteration_number, 100);
}

TEST_F(PathResolutionTest, MissingPointCloudDir) {
    // No point_cloud directory
    auto result = resolve_input_path(temp_dir_);
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(result.error().find("point_cloud") != std::string::npos);
}

TEST_F(PathResolutionTest, NoIterationDirs) {
    create_structure({"point_cloud"});
    // point_cloud exists but no iteration_* inside

    auto result = resolve_input_path(temp_dir_);
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(result.error().find("iteration") != std::string::npos);
}

TEST_F(PathResolutionTest, EmptyIteration) {
    create_structure({"point_cloud/iteration_100"});
    // iteration_100 exists but no PLY files

    auto result = resolve_input_path(temp_dir_);
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(result.error().find("ply") != std::string::npos ||
                result.error().find("PLY") != std::string::npos);
}

TEST_F(PathResolutionTest, IgnoresNonIterationDirs) {
    create_structure({
        "point_cloud/iteration_100",
        "point_cloud/backup",
        "point_cloud/old_iteration_500"
    });
    create_file("point_cloud/iteration_100/point_cloud.ply");

    auto result = resolve_input_path(temp_dir_);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->iteration_number, 100);
}
