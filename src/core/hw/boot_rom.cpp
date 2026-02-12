/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/boot_rom.cpp - Boot ROM */

#include <core/hw/boot_rom.hpp>

#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <common/types.hpp>
#include <core/hw/bus.hpp>

namespace kanacore::hw::boot_rom {

using namespace common;

constexpr u64 BOOT_ROM_ADDR = 0x1FC00000;
constexpr u64 BOOT_ROM_SIZE = 0x1000;
constexpr u64 BOOT_ROM_MASK = BOOT_ROM_SIZE - 1;

static std::shared_ptr<spdlog::logger> logger;

static std::array<u8, BOOT_ROM_SIZE> boot_rom;

template<typename T>
static T read(const u32 addr) {
    assert((addr & (sizeof(T) - 1)) == 0);

    T data;

    std::memcpy(&data, boot_rom.data() + (addr & BOOT_ROM_MASK), sizeof(T));

    return data;
}

void initialize(const char* boot_path) {
    logger = spdlog::stdout_color_st("Boot ROM");
    
    // Load boot ROM image
    FILE* file = std::fopen(boot_path, "rb");

    if (file == nullptr) {
        logger->error("Can't open boot ROM image");
        exit(1);
    }

    std::fseek(file, 0, SEEK_END);

    const u64 file_size = std::ftell(file);

    if (file_size != BOOT_ROM_SIZE) {
        logger->error("Supplied boot ROM image has invalid size (expected: {}, got: {})", BOOT_ROM_SIZE, file_size);
        exit(1);
    }

    std::fseek(file, 0, SEEK_SET);

    if (std::fread(boot_rom.data(), sizeof(u8), BOOT_ROM_SIZE, file) != BOOT_ROM_SIZE) {
        logger->error("Failed to read boot ROM image");
        exit(1);
    }
}

void soft_reset() {

}

void hard_reset() {
    const bus::PageDescriptor page_desc {
        .read8_func  = read<u8>,
        .read16_func = read<u16>,
        .read32_func = read<u32>,
        // Boot ROM can't be written to, so we won't supply write handlers
    };

    bus::map(BOOT_ROM_ADDR, BOOT_ROM_SIZE, page_desc);
}

void shutdown() {

}

};
