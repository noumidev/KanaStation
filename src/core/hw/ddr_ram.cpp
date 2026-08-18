/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/ddr_ram.cpp - DDR memory (32 MB) */

#include <core/hw/ddr_ram.hpp>

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <common/types.hpp>
#include <core/kanacore.hpp>
#include <core/hw/bus.hpp>

namespace kanacore::hw::ddr_ram {

using namespace common;

constexpr u64 RAM_ADDR = 0x08000000;
constexpr u64 RAM_SIZE = 0x2000000;

static std::vector<u8> ram;

static u64 ram_size;

template<typename T>
static T read(const u32 addr) {
    assert((addr & (sizeof(T) - 1)) == 0);

    T data;

    std::memcpy(&data, ram.data() + (addr & (ram_size - 1)), sizeof(T));

    return data;
}

template<typename T>
static void write(const u32 addr, const T data) {
    assert((addr & (sizeof(T) - 1)) == 0);

    std::memcpy(ram.data() + (addr & (ram_size - 1)), &data, sizeof(T));
}

void initialize(const Configuration config) {
    ram_size = RAM_SIZE;

    // Needs to be changed to the lowest Slim mobo type later on...
    if (config.mobo_type >= MOTHERBOARD_TYPE_TA088) {
        ram_size *= 2;
    }

    ram.resize(ram_size);
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

    kanacore::get_sc_bus_ptr()->map(RAM_ADDR, ram_size, page_desc);
    kanacore::get_me_bus_ptr()->map(RAM_ADDR, ram_size, page_desc);
}

void shutdown() {

}

};
