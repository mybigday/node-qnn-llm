#include "unpack.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <iostream>
#include <zlib.h>      // for crc32()
#include <zstd.h>      // for ZSTD_decompress()

namespace fs = std::filesystem;

void unpackModel(const std::string& bundlePath, const std::string& outDir) {
    // 1. Read entire bundle into memory
    std::ifstream in(bundlePath, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open bundle: " + bundlePath);
    std::vector<char> data((std::istreambuf_iterator<char>(in)), {});

    // 2. Validate header
    if (data.size() < sizeof(ContainerHeader) + sizeof(uint32_t))
        throw std::runtime_error("Bundle too small");
    auto hdr = reinterpret_cast<const ContainerHeader*>(data.data());
    if (std::memcmp(hdr->magic, CONTAINER_MAGIC, 7) != 0 ||
        hdr->version != CONTAINER_VERSION) {
        throw std::runtime_error("Invalid container magic/version");
    }

    // 3. Global CRC32 check (last 4 bytes)
    uint32_t storedCrc;
    std::memcpy(&storedCrc, data.data() + data.size() - sizeof(uint32_t), sizeof(uint32_t));
    uint32_t calcCrc = crc32(0,
                             reinterpret_cast<const Bytef*>(data.data()),
                             data.size() - sizeof(uint32_t));
    if (storedCrc != calcCrc) {
        throw std::runtime_error("Global CRC mismatch");
    }

    // 4. Create output directory
    fs::create_directories(outDir);

    // 5. Decompress config.json (first section)
    {
        uint64_t cfgOff = hdr->config_offset;
        uint64_t cfgLen = hdr->config_length;
        if (cfgOff + cfgLen > data.size())
            throw std::runtime_error("Config section out of range");

        // Decompress
        std::vector<char> rawCfg; 
        // We need original size: assume stored in header? 
        // If not stored, we must attempt decompress to unknown size.
        // Here, assume max size = 10 MB for safety; adjust as needed.
        rawCfg.resize(10 * 1024 * 1024);
        size_t dSize = ZSTD_decompress(rawCfg.data(), rawCfg.size(),
                                       data.data() + cfgOff, cfgLen);
        if (ZSTD_isError(dSize))
            throw std::runtime_error("Zstd decompress config failed");
        rawCfg.resize(dSize);

        // Write config.json
        std::ofstream cfgOut(fs::path(outDir) / "config.json", std::ios::binary);
        cfgOut.write(rawCfg.data(), rawCfg.size());
    }

    // 6. Parse TOC entries and decompress others
    size_t ptr = hdr->toc_offset;
    // TOC runs up until global CRC at end
    while (ptr + sizeof(TocEntry) < data.size() - sizeof(uint32_t)) {
        // Read name length
        uint16_t nameLen;
        std::memcpy(&nameLen, data.data() + ptr, sizeof(nameLen));
        ptr += sizeof(nameLen);

        // Read name
        if (ptr + nameLen > data.size()) break;
        std::string name(data.data() + ptr, nameLen);
        ptr += nameLen;

        // Read entry fields
        TocEntry e;
        std::memcpy(&e.offset,      data.data() + ptr, sizeof(e.offset));      ptr += sizeof(e.offset);
        std::memcpy(&e.comp_length, data.data() + ptr, sizeof(e.comp_length)); ptr += sizeof(e.comp_length);
        std::memcpy(&e.raw_length,  data.data() + ptr, sizeof(e.raw_length));  ptr += sizeof(e.raw_length);
        std::memcpy(&e.crc32,       data.data() + ptr, sizeof(e.crc32));       ptr += sizeof(e.crc32);

        // Bounds check
        if (e.offset + e.comp_length > data.size())
            throw std::runtime_error("Section out of range: " + name);

        // CRC check
        uint32_t secCrc = crc32(0,
                                reinterpret_cast<const Bytef*>(data.data() + e.offset),
                                e.comp_length);
        if (secCrc != e.crc32)
            throw std::runtime_error("CRC mismatch for section: " + name);

        // Decompress
        std::vector<char> raw(e.raw_length);
        size_t dSize = ZSTD_decompress(raw.data(), raw.size(),
                                       data.data() + e.offset, e.comp_length);
        if (ZSTD_isError(dSize) || dSize != e.raw_length)
            throw std::runtime_error("Decompress failed for: " + name);

        // Write file
        std::ofstream out(fs::path(outDir) / name, std::ios::binary);
        out.write(raw.data(), raw.size());
    }
}
