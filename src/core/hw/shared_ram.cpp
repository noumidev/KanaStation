/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/shared_ram.cpp - SC & ME shared RAM (2 MB) */

#include <core/hw/shared_ram.hpp>

#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>

#include <common/types.hpp>
#include <core/kanacore.hpp>
#include <core/hw/bus.hpp>

namespace kanacore::hw::shared_ram {

using namespace common;

[[maybe_unused]]
constexpr u64 SHARED_RAM_ADDR = 0x1FC00000;
constexpr u64 SHARED_RAM_SIZE = 0x200000;
constexpr u64 SHARED_RAM_MASK = SHARED_RAM_SIZE - 1;

constexpr u64 SCRATCHPAD_ADDR = 0x1FD00000;
constexpr u64 SCRATCHPAD_SIZE = 0x1000;

static std::array<u8, SHARED_RAM_SIZE> shared_ram;

template<typename T>
static T read(const u32 addr) {
    assert((addr & (sizeof(T) - 1)) == 0);

    T data;

    std::memcpy(&data, shared_ram.data() + (addr & SHARED_RAM_MASK), sizeof(T));

    return data;
}

template<typename T>
static void write(const u32 addr, const T data) {
    assert((addr & (sizeof(T) - 1)) == 0);

    std::memcpy(shared_ram.data() + (addr & SHARED_RAM_MASK), &data, sizeof(T));
}

void initialize() {

}

void soft_reset() {
    const bus::PageDescriptor page_desc {
        .read8_func   = read<u8>,
        .read16_func  = read<u16>,
        .read32_func  = read<u32>,
        .write8_func  = write<u8>,
        .write16_func = write<u16>,
        .write32_func = write<u32>,
    };

    // Unmaps scratchpad, maps shared RAM where the boot ROM used to be
    kanacore::get_sc_bus_ptr()->unmap(SCRATCHPAD_ADDR, SCRATCHPAD_SIZE);
    kanacore::get_sc_bus_ptr()->map(SHARED_RAM_ADDR, SHARED_RAM_SIZE, page_desc);

    // Because our address decoding logic doesn't recognize that the scratchpad
    // area is the first 4 KB of shared RAM, we manually move said area to the beginning
    // of shared RAM (needed for IPL boot)
    std::memcpy(shared_ram.data(), shared_ram.data() + (SCRATCHPAD_ADDR & SHARED_RAM_MASK), SCRATCHPAD_SIZE);
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

    // This is first mapped as a 4 KB scratchpad for SC
    kanacore::get_sc_bus_ptr()->map(SCRATCHPAD_ADDR, SCRATCHPAD_SIZE, page_desc);
}

void shutdown() {

}

};
