#pragma once
#include <cstdint>
#include <string>

static constexpr char CONTAINER_MAGIC[7] = {'Q','G','E','N','I','E','1'}; // "QGENIE1"
static constexpr uint16_t CONTAINER_VERSION = 1;

// Header: 7-byte magic, 2-byte version, 4-byte reserved,
//         8-byte config_offset, 8-byte config_length, 8-byte toc_offset
struct ContainerHeader {
    char     magic[7];
    uint16_t version;
    uint32_t reserved;
    uint64_t config_offset;
    uint64_t config_length;
    uint64_t toc_offset;
};

// TOC entry for tokenizer and model parts
struct TocEntry {
    uint16_t name_length;  // bytes in filename
    // followed in file by name_length bytes of filename
    uint64_t offset;       // start of compressed block
    uint64_t comp_length;  // length of compressed data
    uint64_t raw_length;   // size after decompression
    uint32_t crc32;        // CRC32 of compressed data
};

void unpackModel(const std::string &bundlePath, const std::string &outDir);
