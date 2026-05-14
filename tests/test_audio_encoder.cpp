#include <gtest/gtest.h>
#include "audio_encoder.h"
#include <vector>
#include <cmath>

// 测试 AudioEncoder::convertToInt8 函数
class AudioEncoderConvertToInt8Test : public ::testing::Test {
protected:
    // 创建一个可测试的派生类
    class TestableAudioEncoder : public AudioEncoder {
    public:
        using AudioEncoder::convertToInt8;
    };

    void SetUp() override {
        encoder = new TestableAudioEncoder();
        // 初始化编码器（使用默认参数）
        encoder->init(48000, 4, 2, 48000, 160000, 3000, 480);
    }

    void TearDown() override {
        if (encoder) {
            encoder->stop();
            delete encoder;
            encoder = nullptr;
        }
    }

    TestableAudioEncoder* encoder = nullptr;
};

// 测试正常范围的浮点数转换
TEST_F(AudioEncoderConvertToInt8Test, NormalRangeConversion) {
    std::vector<float> input = {0.0f, 0.5f, -0.5f, 1.0f, -1.0f, 0.25f, -0.75f};
    std::vector<int8_t> output;

    // 调用 convertToInt8 方法
    encoder->convertToInt8(input, output);

    // 验证结果：float * 127.0f 并 clamp 到 [-128, 127]
    ASSERT_EQ(output.size(), 7u);
    EXPECT_EQ(output[0], 0);        // 0.0f * 127 = 0
    EXPECT_EQ(output[1], 64);       // 0.5f * 127 = 63.5 -> 64 (rounding)
    EXPECT_EQ(output[2], -64);      // -0.5f * 127 = -63.5 -> -64
    EXPECT_EQ(output[3], 127);      // 1.0f * 127 = 127
    EXPECT_EQ(output[4], -127);     // -1.0f * 127 = -127 (注意：不是-128)
    EXPECT_EQ(output[5], 32);       // 0.25f * 127 = 31.75 -> 32
    EXPECT_EQ(output[6], -95);      // -0.75f * 127 = -95.25 -> -95
}

// 测试超出范围的浮点数（应该被 clamp）
TEST_F(AudioEncoderConvertToInt8Test, OutOfRangeClamping) {
    std::vector<float> input = {2.0f, -2.0f, 1.5f, -1.5f};
    std::vector<int8_t> output;

    encoder->convertToInt8(input, output);

    // 所有值都应该被 clamp 到 [-128, 127]
    ASSERT_EQ(output.size(), 4u);
    EXPECT_EQ(output[0], 127);      // 2.0f * 127 = 254 -> clamped to 127
    EXPECT_EQ(output[1], -128);     // -2.0f * 127 = -254 -> clamped to -128
    EXPECT_EQ(output[2], 127);      // 1.5f * 127 = 190.5 -> clamped to 127
    EXPECT_EQ(output[3], -128);     // -1.5f * 127 = -190.5 -> clamped to -128
}

// 测试空输入
TEST_F(AudioEncoderConvertToInt8Test, EmptyInput) {
    std::vector<float> input = {};
    std::vector<int8_t> output;

    encoder->convertToInt8(input, output);

    // 应该不产生任何输出
    EXPECT_TRUE(output.empty());
}

// 测试边界值
TEST_F(AudioEncoderConvertToInt8Test, BoundaryValues) {
    std::vector<float> input = {
        127.0f / 127.0f,   // 正好是 1.0f
        -128.0f / 127.0f,  // 约 -1.008f
        1.0f,              // 最大值
        -1.0f              // 最小值
    };
    std::vector<int8_t> output;

    encoder->convertToInt8(input, output);

    ASSERT_EQ(output.size(), 4u);
    EXPECT_EQ(output[0], 127);      // 1.0f * 127 = 127
    EXPECT_EQ(output[1], -128);     // -128/127 * 127 = -128
    EXPECT_EQ(output[2], 127);      // 1.0f * 127 = 127
    EXPECT_EQ(output[3], -127);     // -1.0f * 127 = -127
}
