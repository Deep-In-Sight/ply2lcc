#include <gtest/gtest.h>
#include "types.hpp"
#include <cmath>

using namespace ply2lcc;

// Vec3f tests
TEST(Vec3fTest, DefaultConstructor) {
    Vec3f v;
    EXPECT_FLOAT_EQ(v.x, 0.0f);
    EXPECT_FLOAT_EQ(v.y, 0.0f);
    EXPECT_FLOAT_EQ(v.z, 0.0f);
}

TEST(Vec3fTest, ParameterizedConstructor) {
    Vec3f v(1.0f, 2.0f, 3.0f);
    EXPECT_FLOAT_EQ(v.x, 1.0f);
    EXPECT_FLOAT_EQ(v.y, 2.0f);
    EXPECT_FLOAT_EQ(v.z, 3.0f);
}

TEST(Vec3fTest, IndexOperator) {
    Vec3f v(1.0f, 2.0f, 3.0f);
    EXPECT_FLOAT_EQ(v[0], 1.0f);
    EXPECT_FLOAT_EQ(v[1], 2.0f);
    EXPECT_FLOAT_EQ(v[2], 3.0f);

    v[0] = 10.0f;
    EXPECT_FLOAT_EQ(v.x, 10.0f);
}

TEST(Vec3fTest, ConstIndexOperator) {
    const Vec3f v(4.0f, 5.0f, 6.0f);
    EXPECT_FLOAT_EQ(v[0], 4.0f);
    EXPECT_FLOAT_EQ(v[1], 5.0f);
    EXPECT_FLOAT_EQ(v[2], 6.0f);
}

// BBox tests
TEST(BBoxTest, DefaultIsInvalid) {
    BBox bbox;
    // Min should be max float, max should be lowest float
    EXPECT_GT(bbox.min.x, bbox.max.x);
}

TEST(BBoxTest, ExpandPoint) {
    BBox bbox;
    bbox.expand(Vec3f(1.0f, 2.0f, 3.0f));

    EXPECT_FLOAT_EQ(bbox.min.x, 1.0f);
    EXPECT_FLOAT_EQ(bbox.min.y, 2.0f);
    EXPECT_FLOAT_EQ(bbox.min.z, 3.0f);
    EXPECT_FLOAT_EQ(bbox.max.x, 1.0f);
    EXPECT_FLOAT_EQ(bbox.max.y, 2.0f);
    EXPECT_FLOAT_EQ(bbox.max.z, 3.0f);

    bbox.expand(Vec3f(-1.0f, 5.0f, 0.0f));
    EXPECT_FLOAT_EQ(bbox.min.x, -1.0f);
    EXPECT_FLOAT_EQ(bbox.max.y, 5.0f);
}

TEST(BBoxTest, ExpandBBox) {
    BBox bbox1, bbox2;
    bbox1.expand(Vec3f(0.0f, 0.0f, 0.0f));
    bbox1.expand(Vec3f(1.0f, 1.0f, 1.0f));

    bbox2.expand(Vec3f(-1.0f, -1.0f, -1.0f));
    bbox2.expand(Vec3f(0.5f, 0.5f, 0.5f));

    bbox1.expand(bbox2);
    EXPECT_FLOAT_EQ(bbox1.min.x, -1.0f);
    EXPECT_FLOAT_EQ(bbox1.max.x, 1.0f);
}

// Utility function tests
TEST(UtilityTest, Sigmoid) {
    EXPECT_FLOAT_EQ(sigmoid(0.0f), 0.5f);
    EXPECT_NEAR(sigmoid(10.0f), 1.0f, 0.001f);
    EXPECT_NEAR(sigmoid(-10.0f), 0.0f, 0.001f);
}

TEST(UtilityTest, SigmoidSymmetry) {
    // sigmoid(-x) + sigmoid(x) = 1
    for (float x : {0.5f, 1.0f, 2.0f, 5.0f}) {
        EXPECT_NEAR(sigmoid(-x) + sigmoid(x), 1.0f, 1e-6f);
    }
}

TEST(UtilityTest, Clamp) {
    EXPECT_FLOAT_EQ(clamp(0.5f, 0.0f, 1.0f), 0.5f);
    EXPECT_FLOAT_EQ(clamp(-1.0f, 0.0f, 1.0f), 0.0f);
    EXPECT_FLOAT_EQ(clamp(2.0f, 0.0f, 1.0f), 1.0f);
}

TEST(UtilityTest, ClampEdgeCases) {
    EXPECT_FLOAT_EQ(clamp(0.0f, 0.0f, 1.0f), 0.0f);
    EXPECT_FLOAT_EQ(clamp(1.0f, 0.0f, 1.0f), 1.0f);
    EXPECT_FLOAT_EQ(clamp(-100.0f, -50.0f, 50.0f), -50.0f);
    EXPECT_FLOAT_EQ(clamp(100.0f, -50.0f, 50.0f), 50.0f);
}

// AttributeRanges tests
TEST(AttributeRangesTest, DefaultValues) {
    AttributeRanges ranges;
    // Scale min should be max float initially
    EXPECT_GT(ranges.scale_min.x, 0.0f);
    EXPECT_LT(ranges.scale_max.x, 0.0f);
    EXPECT_GT(ranges.opacity_min, 0.0f);
    EXPECT_LT(ranges.opacity_max, 0.0f);
}

TEST(AttributeRangesTest, ExpandScale) {
    AttributeRanges ranges;
    ranges.expand_scale(Vec3f(1.0f, 2.0f, 3.0f));

    EXPECT_FLOAT_EQ(ranges.scale_min.x, 1.0f);
    EXPECT_FLOAT_EQ(ranges.scale_max.x, 1.0f);

    ranges.expand_scale(Vec3f(0.5f, 4.0f, 2.0f));
    EXPECT_FLOAT_EQ(ranges.scale_min.x, 0.5f);
    EXPECT_FLOAT_EQ(ranges.scale_max.y, 4.0f);
}

TEST(AttributeRangesTest, ExpandSH) {
    AttributeRanges ranges;
    ranges.expand_sh(1.5f);
    EXPECT_FLOAT_EQ(ranges.sh_min.x, 1.5f);
    EXPECT_FLOAT_EQ(ranges.sh_max.x, 1.5f);

    ranges.expand_sh(-2.0f);
    EXPECT_FLOAT_EQ(ranges.sh_min.x, -2.0f);
    EXPECT_FLOAT_EQ(ranges.sh_max.x, 1.5f);

    ranges.expand_sh(3.0f);
    EXPECT_FLOAT_EQ(ranges.sh_max.x, 3.0f);
}

TEST(AttributeRangesTest, ExpandOpacity) {
    AttributeRanges ranges;
    ranges.expand_opacity(0.5f);
    EXPECT_FLOAT_EQ(ranges.opacity_min, 0.5f);
    EXPECT_FLOAT_EQ(ranges.opacity_max, 0.5f);

    ranges.expand_opacity(0.2f);
    ranges.expand_opacity(0.9f);
    EXPECT_FLOAT_EQ(ranges.opacity_min, 0.2f);
    EXPECT_FLOAT_EQ(ranges.opacity_max, 0.9f);
}

// GridCell tests
TEST(GridCellTest, Constructor) {
    GridCell cell(0x00010002, 3);
    EXPECT_EQ(cell.index, 0x00010002u);
    EXPECT_EQ(cell.splat_indices.size(), 3u);
}

TEST(GridCellTest, IndexEncoding) {
    // Test that index encodes cell_x and cell_y correctly
    uint32_t cell_x = 5;
    uint32_t cell_y = 10;
    uint32_t index = (cell_y << 16) | cell_x;

    GridCell cell(index, 1);
    EXPECT_EQ(cell.index & 0xFFFF, cell_x);
    EXPECT_EQ((cell.index >> 16) & 0xFFFF, cell_y);
}

// Splat tests
TEST(SplatTest, DefaultValues) {
    Splat s;
    EXPECT_FLOAT_EQ(s.pos.x, 0.0f);
    EXPECT_FLOAT_EQ(s.pos.y, 0.0f);
    EXPECT_FLOAT_EQ(s.pos.z, 0.0f);
}

// ConvertConfig tests
TEST(ConvertConfigTest, DefaultValues) {
    ConvertConfig config;
    EXPECT_EQ(config.single_lod, false);
    EXPECT_FLOAT_EQ(config.cell_size_x, 30.0f);
    EXPECT_FLOAT_EQ(config.cell_size_y, 30.0f);
}
