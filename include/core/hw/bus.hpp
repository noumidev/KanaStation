/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/bus.hpp - Memory bus */

#pragma once

#include <common/types.hpp>

namespace kanacore::hw::bus {

using ReadHandler8   = common::u8  (*)(const common::u32);
using ReadHandler16  = common::u16 (*)(const common::u32);
using ReadHandler32  = common::u32 (*)(const common::u32);
using WriteHandler8  = void (*)(const common::u32, const common::u8);
using WriteHandler16 = void (*)(const common::u32, const common::u16);
using WriteHandler32 = void (*)(const common::u32, const common::u32);

// This describes all read/write handlers for a page (or more)
struct PageDescriptor {
    ReadHandler8  read8_func;
    ReadHandler16 read16_func;
    ReadHandler32 read32_func;

    WriteHandler8  write8_func;
    WriteHandler16 write16_func;
    WriteHandler32 write32_func;
};

void initialize();
void soft_reset();
void hard_reset();
void shutdown();

template<typename T>
T read(const common::u32 addr);

template<typename T>
void write(const common::u32 addr, const T data);

// Maps read/write handlers 
void map(const common::u32 addr, const common::u32 size, const PageDescriptor page_desc);

void unmap(const common::u32 addr, const common::u32 size);

};
