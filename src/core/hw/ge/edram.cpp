/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/ge/edram.cpp - Graphics Engine EDRAM (2 MB) */

#include <core/hw/ge/edram.hpp>

#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>

#include <common/types.hpp>
#include <core/kanacore.hpp>
#include <core/hw/bus.hpp>

namespace kanacore::hw::ge::edram {

using namespace common;

constexpr u64 EDRAM_ADDR = 0x04000000;
constexpr u64 EDRAM_SIZE = 0x200000;
constexpr u64 EDRAM_MASK = EDRAM_SIZE - 1;

static std::array<u8, EDRAM_SIZE> edram;

template<typename T>
static T read(const u32 addr) {
    assert((addr & (sizeof(T) - 1)) == 0);

    T data;

    std::memcpy(&data, edram.data() + (addr & EDRAM_MASK), sizeof(T));

    return data;
}

template<typename T>
static void write(const u32 addr, const T data) {
    assert((addr & (sizeof(T) - 1)) == 0);

    std::memcpy(edram.data() + (addr & EDRAM_MASK), &data, sizeof(T));
}

void initialize() {

}

void soft_reset() {

}

void hard_reset() {
    const bus::PageDescriptor page_desc {
        .read8_func   = read<u8>,
        .read16_func  = read<u16>,
        .read32_func  = read<u32>,
        .write8_func  = write<u8>,
        .write16_func = write<u16>,
        .write32_func = write<u32>,
    };

    kanacore::get_sc_bus_ptr()->map(EDRAM_ADDR, EDRAM_SIZE, page_desc);
}

void shutdown() {

}

};
