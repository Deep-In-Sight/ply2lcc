#include <gtest/gtest.h>
#include "compression.hpp"
#include "types.hpp"
#include <cmath>

using namespace ply2lcc;

// Test encode_color
TEST(CompressionTest, EncodeColorBlack) {
    // f_dc = 0 should give color ~0.5 (gray)
    float f_dc[3] = {0.0f, 0.0f, 0.0f};
    float opacity = 0.0f;  // sigmoid(0) = 0.5
    uint32_t color = encode_color(f_dc, opacity);

    uint8_t r = color & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = (color >> 16) & 0xFF;
    uint8_t a = (color >> 24) & 0xFF;

    // With SH_C0 = 0.282, color = 0.5 + 0.282*0 = 0.5 -> 128
    EXPECT_NEAR(r, 128, 1);
    EXPECT_NEAR(g, 128, 1);
    EXPECT_NEAR(b, 128, 1);
    EXPECT_NEAR(a, 128, 1);  // sigmoid(0) = 0.5 -> 128
}

TEST(CompressionTest, EncodeColorWhite) {
    // Large positive f_dc should give white
    float f_dc[3] = {10.0f, 10.0f, 10.0f};
    float opacity = 10.0f;  // sigmoid(10) ~ 1.0
    uint32_t color = encode_color(f_dc, opacity);

    uint8_t r = color & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = (color >> 16) & 0xFF;
    uint8_t a = (color >> 24) & 0xFF;

    EXPECT_EQ(r, 255);
    EXPECT_EQ(g, 255);
    EXPECT_EQ(b, 255);
    EXPECT_NEAR(a, 255, 1);
}

// Test encode_scale
TEST(CompressionTest, EncodeScaleMinMax) {
    Vec3f scale_min(0.1f, 0.1f, 0.1f);
    Vec3f scale_max(10.0f, 10.0f, 10.0f);
    uint16_t out[3];

    // log(0.1) ~ -2.3, should map to 0
    Vec3f log_scale_min(std::log(0.1f), std::log(0.1f), std::log(0.1f));
    encode_scale(log_scale_min, scale_min, scale_max, out);
    EXPECT_EQ(out[0], 0);
    EXPECT_EQ(out[1], 0);
    EXPECT_EQ(out[2], 0);

    // log(10) ~ 2.3, should map to 65535
    Vec3f log_scale_max(std::log(10.0f), std::log(10.0f), std::log(10.0f));
    encode_scale(log_scale_max, scale_min, scale_max, out);
    EXPECT_EQ(out[0], 65535);
    EXPECT_EQ(out[1], 65535);
    EXPECT_EQ(out[2], 65535);
}

TEST(CompressionTest, EncodeScaleMidpoint) {
    Vec3f scale_min(0.0f, 0.0f, 0.0f);
    Vec3f scale_max(2.0f, 2.0f, 2.0f);
    uint16_t out[3];

    // log(1) = 0, linear scale = 1.0, midpoint of [0, 2]
    Vec3f log_scale(0.0f, 0.0f, 0.0f);
    encode_scale(log_scale, scale_min, scale_max, out);
    EXPECT_NEAR(out[0], 32768, 1);  // 0.5 * 65535
}

// Test encode_rotation
TEST(CompressionTest, EncodeRotationIdentity) {
    // Identity quaternion (w=1, x=y=z=0)
    float rot[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    uint32_t encoded = encode_rotation(rot);

    // w is largest, so idx should be 0 (stored in bits 30-31)
    uint32_t idx = (encoded >> 30) & 0x3;
    EXPECT_EQ(idx, 0);

    // x, y, z are all 0, which maps to 0.5 in normalized space
    uint32_t p0 = encoded & 0x3FF;
    uint32_t p1 = (encoded >> 10) & 0x3FF;
    uint32_t p2 = (encoded >> 20) & 0x3FF;
    EXPECT_NEAR(p0, 512, 2);  // 0.5 * 1023
    EXPECT_NEAR(p1, 512, 2);
    EXPECT_NEAR(p2, 512, 2);
}

TEST(CompressionTest, EncodeRotationNormalization) {
    // Non-normalized quaternion should still work
    float rot[4] = {2.0f, 0.0f, 0.0f, 0.0f};
    uint32_t encoded = encode_rotation(rot);

    // Should be same as identity after normalization
    uint32_t idx = (encoded >> 30) & 0x3;
    EXPECT_EQ(idx, 0);
}

// Test encode_sh_triplet
TEST(CompressionTest, EncodeSHTripletMinMax) {
    float sh_min = -3.0f;
    float sh_max = 3.0f;

    // Min values
    uint32_t enc_min = encode_sh_triplet(-3.0f, -3.0f, -3.0f, sh_min, sh_max);
    uint32_t r = enc_min & 0x7FF;         // 11 bits
    uint32_t g = (enc_min >> 11) & 0x3FF; // 10 bits
    uint32_t b = (enc_min >> 21) & 0x7FF; // 11 bits
    EXPECT_EQ(r, 0);
    EXPECT_EQ(g, 0);
    EXPECT_EQ(b, 0);

    // Max values
    uint32_t enc_max = encode_sh_triplet(3.0f, 3.0f, 3.0f, sh_min, sh_max);
    r = enc_max & 0x7FF;
    g = (enc_max >> 11) & 0x3FF;
    b = (enc_max >> 21) & 0x7FF;
    EXPECT_EQ(r, 2047);
    EXPECT_EQ(g, 1023);
    EXPECT_EQ(b, 2047);
}

TEST(CompressionTest, EncodeSHTripletMidpoint) {
    float sh_min = -2.0f;
    float sh_max = 2.0f;

    // Midpoint values (0, 0, 0)
    uint32_t encoded = encode_sh_triplet(0.0f, 0.0f, 0.0f, sh_min, sh_max);
    uint32_t r = encoded & 0x7FF;
    uint32_t g = (encoded >> 11) & 0x3FF;
    uint32_t b = (encoded >> 21) & 0x7FF;

    // Should be approximately halfway
    EXPECT_NEAR(r, 1024, 1);  // 0.5 * 2047
    EXPECT_NEAR(g, 512, 1);   // 0.5 * 1023
    EXPECT_NEAR(b, 1024, 1);  // 0.5 * 2047
}

TEST(CompressionTest, EncodeSHTripletRangeZero) {
    // Edge case: range is zero
    float sh_min = 1.0f;
    float sh_max = 1.0f;

    uint32_t encoded = encode_sh_triplet(1.0f, 1.0f, 1.0f, sh_min, sh_max);
    uint32_t r = encoded & 0x7FF;
    uint32_t g = (encoded >> 11) & 0x3FF;
    uint32_t b = (encoded >> 21) & 0x7FF;

    // With zero range, should default to 0.5 normalized
    EXPECT_NEAR(r, 1024, 1);
    EXPECT_NEAR(g, 512, 1);
    EXPECT_NEAR(b, 1024, 1);
}
