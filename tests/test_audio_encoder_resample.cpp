#include <gtest/gtest.h>
#include "audio_encoder.h"
#include <vector>
#include <cmath>
#include <fstream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// 测试 AudioEncoder::resampleAudio 和 resampleHaptics 函数
class AudioEncoderResampleTest : public ::testing::Test {
protected:
    // 创建一个可测试的派生类
    class TestableAudioEncoder : public AudioEncoder {
    public:
        using AudioEncoder::resampleAudio;
        using AudioEncoder::resampleHaptics;
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

// 测试音频重采样（512 -> 480 samples）
TEST_F(AudioEncoderResampleTest, AudioResampling) {
    std::vector<float> input(512 * 2, 0.5f);  // 512 frames, 2 channels
    std::vector<float> output;
    
    // 调用 resampleAudio 方法：512 -> 480
    size_t batches = encoder->resampleAudio(input, output);
    
    // 验证结果
    EXPECT_EQ(batches, 1u);
    EXPECT_EQ(output.size(), 480 * 2);  // 480 frames, 2 channels
    EXPECT_TRUE(input.empty());  // 输入应该被清空
}

// 测试触觉数据重采样（48 -> 3 samples）
TEST_F(AudioEncoderResampleTest, HapticsResampling) {
    std::vector<float> input(48 * 2, 0.5f);  // 48 frames, 2 channels
    std::vector<float> output;
    
    // 调用 resampleHaptics 方法：48 -> 3
    size_t batches = encoder->resampleHaptics(input, output);
    
    // 验证结果
    EXPECT_EQ(batches, 1u);
    EXPECT_EQ(output.size(), 3 * 2);  // 3 frames, 2 channels
    EXPECT_TRUE(input.empty());  // 输入应该被清空
}

// 测试空输入 - 音频
TEST_F(AudioEncoderResampleTest, EmptyInputAudio) {
    std::vector<float> input = {};
    std::vector<float> output;
    
    // 应该返回 0
    size_t batches = encoder->resampleAudio(input, output);
    
    EXPECT_EQ(batches, 0u);
    EXPECT_TRUE(output.empty());
}

// 测试空输入 - 触觉
TEST_F(AudioEncoderResampleTest, EmptyInputHaptics) {
    std::vector<float> input = {};
    std::vector<float> output;
    
    // 应该返回 0
    size_t batches = encoder->resampleHaptics(input, output);
    
    EXPECT_EQ(batches, 0u);
    EXPECT_TRUE(output.empty());
}

// 测试不同批次大小 - 音频
TEST_F(AudioEncoderResampleTest, MultipleBatchesAudio) {
    // 测试多个批次的重采样：3个批次，每个批次512 frames
    std::vector<float> input(512 * 2 * 3, 0.5f);  // 3 batches
    std::vector<float> output;
    
    size_t batches = encoder->resampleAudio(input, output);
    
    // 验证结果
    EXPECT_EQ(batches, 3u);
    EXPECT_EQ(output.size(), 480 * 2 * 3);  // 3 batches * 480 frames * 2 channels
    EXPECT_TRUE(input.empty());
}

// 测试边界情况：刚好一个批次 - 音频
TEST_F(AudioEncoderResampleTest, SingleBatchAudio) {
    std::vector<float> input(512 * 2, 1.0f);  // 正好一个批次
    std::vector<float> output;
    
    size_t batches = encoder->resampleAudio(input, output);
    
    EXPECT_EQ(batches, 1u);
    EXPECT_EQ(output.size(), 480 * 2);
    EXPECT_TRUE(input.empty());
}

// 测试正弦波重采样 - 验证信号完整性
TEST_F(AudioEncoderResampleTest, SineWaveResampling) {
    const float frequency = 440.0f;  // A4 音符频率
    const float sampleRateIn = 48000.0f;
    const float resampleRatio = 480.0f / 512.0f;  // 重采样比例
    const float sampleRateOut = sampleRateIn * resampleRatio;  // = 45000 Hz
    const size_t inputFrames = 512;
    const size_t outputFrames = static_cast<size_t>(inputFrames * resampleRatio);  // = 480
    const size_t channels = 2;
    
    // 生成正弦波输入信号（双声道）
    std::vector<float> input(inputFrames * channels);
    for (size_t i = 0; i < inputFrames; i++) {
        float t = static_cast<float>(i) / sampleRateIn;
        float sample = std::sin(2.0f * M_PI * frequency * t);
        input[i * channels] = sample;         // Left channel
        input[i * channels + 1] = sample;     // Right channel
    }
    
    std::vector<float> output;
    
    // 执行重采样
    size_t batches = encoder->resampleAudio(input, output);
    
    // 验证基本属性
    EXPECT_EQ(batches, 1u);
    EXPECT_EQ(output.size(), outputFrames * channels);
    EXPECT_TRUE(input.empty());  // 验证输入已被移动
    
    // 计算输出信号的统计特性
    float maxAmplitude = 0.0f;
    float rms = 0.0f;
    for (const auto& sample : output) {
        float absSample = std::abs(sample);
        maxAmplitude = std::max(maxAmplitude, absSample);
        rms += sample * sample;
    }
    rms = std::sqrt(rms / output.size());
    
    // 验证幅值范围（理想正弦波峰值应为 1.0，RMS 约为 0.707）
    EXPECT_NEAR(maxAmplitude, 1.0f, 0.3f);  // 允许 30% 的误差
    EXPECT_NEAR(rms, 0.707f, 0.2f);         // RMS 应在合理范围内
    
    // 验证输出信号的周期性（使用自相关或过零点检测）
    int zeroCrossings = 0;
    for (size_t i = channels; i < output.size(); i += channels) {
        float prev = output[i - channels];
        float curr = output[i];
        // 检测过零点（包括正负零的情况）
        if ((prev <= 0 && curr > 0) || (prev >= 0 && curr < 0)) {
            zeroCrossings++;
        }
    }
    
    // 预期周期数 = 信号持续时间 * 频率
    float duration = static_cast<float>(outputFrames) / sampleRateOut;
    float expectedCycles = duration * frequency;  // ≈ 4.7 个周期
    int expectedZeroCrossings = static_cast<int>(expectedCycles * 2);  // ≈ 9-10 个
    
    EXPECT_NEAR(zeroCrossings, expectedZeroCrossings, 2);
    
    // 可选：验证主频分量（使用 FFT 或简单的峰值检测）
    // 计算相邻过零点间的距离来估算频率
    if (zeroCrossings >= 4) {
        std::vector<size_t> crossingIndices;
        for (size_t i = channels; i < output.size(); i += channels) {
            if ((output[i - channels] <= 0 && output[i] > 0) ||
                (output[i - channels] >= 0 && output[i] < 0)) {
                crossingIndices.push_back(i / channels);
            }
        }
        
        // 计算平均半周期长度
        if (crossingIndices.size() >= 2) {
            float avgHalfPeriod = 0.0f;
            for (size_t i = 1; i < crossingIndices.size(); i++) {
                avgHalfPeriod += (crossingIndices[i] - crossingIndices[i-1]);
            }
            avgHalfPeriod /= (crossingIndices.size() - 1);
            
            float estimatedFreq = sampleRateOut / (2.0f * avgHalfPeriod);
            EXPECT_NEAR(estimatedFreq, frequency, 20.0f);  // 允许 20Hz 误差
        }
    }
}

// 测试正弦波重采样 - 验证信号完整性并输出到文件
TEST_F(AudioEncoderResampleTest, SineWaveResamplingWithFileOutput) {
    const float frequency = 440.0f;  // A4 音符频率
    const float sampleRateIn = 48000.0f;
    const float resampleRatio = 480.0f / 512.0f;  // 重采样比例
    const float sampleRateOut = sampleRateIn * resampleRatio;  // = 45000 Hz
    const size_t inputFrames = 512;
    const size_t outputFrames = static_cast<size_t>(inputFrames * resampleRatio);  // = 480
    const size_t channels = 2;
    
    // 生成正弦波输入信号（双声道）
    std::vector<float> input(inputFrames * channels);
    for (size_t i = 0; i < inputFrames; i++) {
        float t = static_cast<float>(i) / sampleRateIn;
        float sample = std::sin(2.0f * M_PI * frequency * t);
        input[i * channels] = sample;         // Left channel
        input[i * channels + 1] = sample;     // Right channel
    }
    
    std::vector<float> output;

    // 写入文件：输入信号（重采样前）
    std::ofstream inFile("sinewave_input.txt");
    if (inFile.is_open()) {
        inFile << "# 输入信号 - 440Hz 正弦波\n";
        inFile << "# 采样率: " << sampleRateIn << " Hz\n";
        inFile << "# 帧数: " << inputFrames << "\n";
        inFile << "# 格式: 左声道\t右声道\n";
        
        for (size_t i = 0; i < inputFrames; i++) {
            inFile << input[i * channels] << "\t" 
                   << input[i * channels + 1] << "\n";
        }
        inFile.close();
        std::cout << "输入信号已保存到 sinewave_input.txt" << std::endl;
    } else {
        std::cerr << "无法打开 sinewave_input.txt 文件" << std::endl;
    }

    
    // 执行重采样
    size_t batches = encoder->resampleAudio(input, output);
    
    // 验证基本属性
    EXPECT_EQ(batches, 1u);
    EXPECT_EQ(output.size(), outputFrames * channels);
    
    // 写入文件：输出信号（重采样后）
    std::ofstream outFile("sinewave_output.txt");
    if (outFile.is_open()) {
        outFile << "# 输出信号 - 重采样后的 440Hz 正弦波\n";
        outFile << "# 原始采样率: " << sampleRateIn << " Hz\n";
        outFile << "# 输出采样率: " << sampleRateOut << " Hz\n";
        outFile << "# 重采样比例: " << resampleRatio << "\n";
        outFile << "# 帧数: " << outputFrames << "\n";
        outFile << "# 格式: 左声道\t右声道\n";
        
        for (size_t i = 0; i < outputFrames; i++) {
            outFile << output[i * channels] << "\t" 
                    << output[i * channels + 1] << "\n";
        }
        outFile.close();
        std::cout << "输出信号已保存到 sinewave_output.txt" << std::endl;
    } else {
        std::cerr << "无法打开 sinewave_output.txt 文件" << std::endl;
    }
    
    
    // 验证输出信号质量（保持原有的验证逻辑）
    float maxAmplitude = 0.0f;
    float rms = 0.0f;
    for (const auto& sample : output) {
        float absSample = std::abs(sample);
        maxAmplitude = std::max(maxAmplitude, absSample);
        rms += sample * sample;
    }
    rms = std::sqrt(rms / output.size());
    
    EXPECT_NEAR(maxAmplitude, 1.0f, 0.3f);
    EXPECT_NEAR(rms, 0.707f, 0.2f);
    
    // 过零点检测
    int zeroCrossings = 0;
    for (size_t i = channels; i < output.size(); i += channels) {
        float prev = output[i - channels];
        float curr = output[i];
        if ((prev <= 0 && curr > 0) || (prev >= 0 && curr < 0)) {
            zeroCrossings++;
        }
    }
    
    float duration = static_cast<float>(outputFrames) / sampleRateOut;
    float expectedCycles = duration * frequency;
    int expectedZeroCrossings = static_cast<int>(expectedCycles * 2);
    
    EXPECT_NEAR(zeroCrossings, expectedZeroCrossings, 2);
    
    std::cout << "验证结果: 最大幅值=" << maxAmplitude 
              << ", RMS=" << rms 
              << ", 过零点=" << zeroCrossings 
              << "/" << expectedZeroCrossings << std::endl;
}