#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

class OpusOutput {
public:
    OpusOutput();
    ~OpusOutput();

    /**
     * @brief 初始化Opus输出文件，写入头等数据
     * @param filename 输出文件名
     * @param sampleRate 音频采样率
     * @param channels 音频通道数
     * @return 成功返回 true，失败返回 false
     */
    bool init(const std::string& filename, uint32_t sampleRate = 48000, uint32_t channels = 2);

    /**
     * @brief 写入编码后的Opus数据包（内部自动管理granule position）
     * @param data 编码数据指针
     * @param size 数据大小
     * @param frameSize 每帧样本数（用于累加granule position）
     * @return 成功返回 true，失败返回 false
     */
    bool writePacket(const uint8_t* data, uint32_t size, uint32_t frameSize);

    /**
     * @brief 关闭文件并写入结束标记
     */
    void close();

    /**
     * @brief 检查文件是否已打开
     * @return 文件打开状态
     */
    bool isOpen() const;

private:
    // Ogg 页面写入
    void writeOggPage(const uint8_t* data, uint32_t dataSize,
                      uint64_t granulePos, uint8_t headerType);

    // Ogg Opus 头等数据写入
    void writeOpusHead(uint32_t sampleRate, uint32_t channels);
    void writeOpusTags();

    // CRC32 计算
    uint32_t crc32(const uint8_t* data, size_t length);

    // 成员变量
    std::ofstream m_file;
    bool m_isOpen = false;
    uint32_t m_pageSequenceNumber = 0;
    uint64_t m_granulePosition = 0;  // Ogg granule position (sample count)
};
