/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/allegrex/scratchpad.cpp - ALLEGREX scratchpad */

#include <core/hw/allegrex/scratchpad.hpp>

#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>

#include <common/types.hpp>
#include <core/hw/bus.hpp>

namespace kanacore::hw::allegrex::scratchpad {

using namespace common;

constexpr u64 SCRATCHPAD_ADDR = 0x00010000;
constexpr u64 SCRATCHPAD_SIZE = 0x4000;
constexpr u64 SCRATCHPAD_MASK = SCRATCHPAD_SIZE - 1;

static std::array<u8, SCRATCHPAD_SIZE> scratchpad;

template<typename T>
static T read(const u32 addr) {
    assert((addr & (sizeof(T) - 1)) == 0);

    T data;

    std::memcpy(&data, scratchpad.data() + (addr & SCRATCHPAD_MASK), sizeof(T));

    return data;
}

template<typename T>
static void write(const u32 addr, const T data) {
    assert((addr & (sizeof(T) - 1)) == 0);

    std::memcpy(scratchpad.data() + (addr & SCRATCHPAD_MASK), &data, sizeof(T));
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

    bus::map(SCRATCHPAD_ADDR, SCRATCHPAD_SIZE, page_desc);
}

void shutdown() {

}

};
