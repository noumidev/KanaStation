/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/audio.cpp - Audio interface */

#include <core/hw/audio.hpp>

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <core/hw/bus.hpp>

namespace kanacore::hw::audio {

using namespace common;

constexpr u64 AUDIO_ADDR = 0x1E000000;
constexpr u64 AUDIO_SIZE = 0x1000;

enum IoAddress {};

static struct {} ctx;

static std::shared_ptr<spdlog::logger> logger;

static u32 read(const u32 addr) {
    switch (addr) {
        case AUDIO_ADDR + 0x028:
            logger->warn("Unmapped read32 @ {:08X}", addr);

            // Unsure what this is; initially, the kernel waits for these two
            // bits to be set
            return 0x30;
        default:
            logger->warn("Unmapped read32 @ {:08X}", addr);
            return 0;
    }
}

static void write(const u32 addr, const u32 data) {
    switch (addr) {
        default:
            logger->warn("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            break;
    }
}

void initialize() {
    logger = spdlog::stdout_color_st("Audio");

    std::memset(&ctx, 0, sizeof(ctx));
}

void soft_reset() {
    
}

void hard_reset() {
    const bus::PageDescriptor page_desc {
        // To my knowledge, audio I/F I/O is never not read/written using 32-bit accesses
        .read32_func  = read,
        .write32_func = write,
    };

    bus::map(AUDIO_ADDR, AUDIO_SIZE, page_desc);
}

void shutdown() {

}

};
