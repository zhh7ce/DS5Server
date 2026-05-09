#include "opus_output.h"
#include <cstring>
#include <iostream>

OpusOutput::OpusOutput() = default;

OpusOutput::~OpusOutput() {
    close();
}

bool OpusOutput::init(const std::string& filename, uint32_t sampleRate, uint32_t channels) {
    if (m_isOpen) {
        std::cerr << "[OpusOutput] File already open" << std::endl;
        return false;
    }

    m_file.open(filename, std::ios::binary);
    if (!m_file.is_open()) {
        std::cerr << "[OpusOutput] Failed to open file: " << filename << std::endl;
        return false;
    }

    m_isOpen = true;
    m_pageSequenceNumber = 0;
    m_granulePosition = 0;

    // 写入 Ogg Opus 头等数据
    writeOpusHead(sampleRate, channels);
    writeOpusTags();

    std::cout << "[OpusOutput] Started writing to: " << filename << std::endl;
    return true;
}

bool OpusOutput::writePacket(const uint8_t* data, uint32_t size, uint32_t frameSize) {
    if (!m_isOpen || !data || size == 0) {
        return false;
    }

    // 累加 granule position
    m_granulePosition += frameSize;

    // 写入 Ogg 页面，headerType = 0x00（普通数据包）
    writeOggPage(data, size, m_granulePosition, 0x00);
    return true;
}

void OpusOutput::close() {
    if (!m_isOpen) {
        return;
    }

    // 写入一个空的结束页面，标记为 last page
    uint8_t emptyData = 0;
    writeOggPage(&emptyData, 0, 0, 0x04);  // 0x04 = end of stream

    if (m_file.is_open()) {
        m_file.flush();
        m_file.close();
    }

    m_isOpen = false;
    std::cout << "[OpusOutput] Closed file output" << std::endl;
}

bool OpusOutput::isOpen() const {
    return m_isOpen;
}

void OpusOutput::writeOggPage(const uint8_t* data, uint32_t dataSize,
                              uint64_t granulePos, uint8_t headerType) {
    if (!m_file.is_open()) {
        return;
    }

    // Ogg 页面头部结构（27字节 + segment table）
    const uint32_t headerSize = 27;

    // 计算需要的 segment 数量（每个 segment 最大 255 字节）
    uint32_t numSegments = (dataSize + 254) / 255;
    if (numSegments == 0) numSegments = 1;

    std::vector<uint8_t> page(headerSize + numSegments + dataSize);

    // Capture pattern: "OggS"
    page[0] = 'O';
    page[1] = 'g';
    page[2] = 'g';
    page[3] = 'S';

    // Stream structure version
    page[4] = 0;

    // Header type flag
    page[5] = headerType;

    // Granule position (8 bytes, little-endian)
    for (int i = 0; i < 8; i++) {
        page[6 + i] = granulePos & 0xFF;
        granulePos >>= 8;
    }

    // Serial number (4 bytes, little-endian)
    // 使用 DualSense vendor:product ID (054c:0ce6)
    uint32_t serial = 0x054c0ce6;
    for (int i = 0; i < 4; i++) {
        page[14 + i] = serial & 0xFF;
        serial >>= 8;
    }

    // Page sequence number (4 bytes, little-endian)
    for (int i = 0; i < 4; i++) {
        page[18 + i] = m_pageSequenceNumber & 0xFF;
        m_pageSequenceNumber++;
    }

    // Checksum (4 bytes, must be zero for CRC calculation)
    page[22] = page[23] = page[24] = page[25] = 0;

    // Number of segments
    page[26] = static_cast<uint8_t>(numSegments);

    // Segment table
    uint32_t remaining = dataSize;
    for (uint32_t i = 0; i < numSegments; i++) {
        uint8_t segSize = (remaining > 255) ? 255 : static_cast<uint8_t>(remaining);
        page[27 + i] = segSize;
        remaining -= segSize;
    }

    // Packet data
    if (dataSize > 0) {
        memcpy(&page[headerSize + numSegments], data, dataSize);
    }

    // Calculate CRC32 over entire page with checksum field zeroed
    uint32_t crc = crc32(page.data(), page.size());
    for (int i = 0; i < 4; i++) {
        page[22 + i] = crc & 0xFF;
        crc >>= 8;
    }

    // Write to file
    m_file.write(reinterpret_cast<const char*>(page.data()), page.size());
    m_file.flush();
}

void OpusOutput::writeOpusHead(uint32_t sampleRate, uint32_t channels) {
    // OpusHead identifier: "OpusHead"
    const uint8_t opusHeadId[8] = {'O', 'p', 'u', 's', 'H', 'e', 'a', 'd'};

    // Build OpusHead packet
    std::vector<uint8_t> opusHead(19);

    // Identifier
    memcpy(&opusHead[0], opusHeadId, 8);

    // Version
    opusHead[8] = 1;

    // Channel count
    opusHead[9] = static_cast<uint8_t>(channels);

    // Pre-skip (little-endian, 2 bytes)
    opusHead[10] = 0;
    opusHead[11] = 0;

    // Input sample rate (little-endian, 4 bytes)
    opusHead[12] = sampleRate & 0xFF;
    opusHead[13] = (sampleRate >> 8) & 0xFF;
    opusHead[14] = (sampleRate >> 16) & 0xFF;
    opusHead[15] = (sampleRate >> 24) & 0xFF;

    // Output gain (little-endian, 2 bytes) - 0 dB
    opusHead[16] = 0;
    opusHead[17] = 0;

    // Channel mapping family
    opusHead[18] = 0;  // 0 = single stream, no channel mapping

    // Write as first Ogg page with BOS flag (0x02) and granule = 0
    writeOggPage(opusHead.data(), opusHead.size(), 0, 0x02);
}

void OpusOutput::writeOpusTags() {
    // OpusTags identifier: "OpusTags"
    const uint8_t opusTagsId[8] = {'O', 'p', 'u', 's', 'T', 'a', 'g', 's'};

    // Build OpusTags packet
    std::string vendorString = "DS5Server AudioEncoder";
    uint32_t vendorLength = vendorString.size();

    std::vector<uint8_t> opusTags(8 + 4 + vendorLength + 4);

    // Identifier
    memcpy(&opusTags[0], opusTagsId, 8);

    // Vendor string length (little-endian, 4 bytes)
    for (int i = 0; i < 4; i++) {
        opusTags[8 + i] = vendorLength & 0xFF;
        vendorLength >>= 8;
    }

    // Vendor string
    memcpy(&opusTags[12], vendorString.c_str(), vendorString.size());

    // User comment list length (little-endian, 4 bytes)
    opusTags[12 + vendorString.size()] = 0;
    opusTags[13 + vendorString.size()] = 0;
    opusTags[14 + vendorString.size()] = 0;
    opusTags[15 + vendorString.size()] = 0;

    // Write as second Ogg page with granule = 0
    writeOggPage(opusTags.data(), opusTags.size(), 0, 0x00);
}

uint32_t OpusOutput::crc32(const uint8_t* data, size_t length) {
    // Ogg 使用的 CRC32 多项式（与标准 CRC32 不同）
    // Polynomial: 0x04C11DB7 (unreflected)
    static uint32_t crcTable[256];
    static bool tableInitialized = false;

    if (!tableInitialized) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t r = i << 24;
            for (int j = 0; j < 8; j++) {
                r = (r & 0x80000000) ? ((r << 1) ^ 0x04C11DB7) : (r << 1);
            }
            crcTable[i] = r;
        }
        tableInitialized = true;
    }

    uint32_t crc = 0;
    for (size_t i = 0; i < length; i++) {
        crc = (crc << 8) ^ crcTable[((crc >> 24) ^ data[i]) & 0xFF];
    }

    return crc;
}
