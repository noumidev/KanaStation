/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/bus.hpp - Memory bus */

#pragma once

#include <array>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <common/types.hpp>

namespace kanacore::hw::bus {

using ReadHandler8   = common::u8  (*)(const common::u32);
using ReadHandler16  = common::u16 (*)(const common::u32);
using ReadHandler32  = common::u32 (*)(const common::u32);
using WriteHandler8  = void (*)(const common::u32, const common::u8);
using WriteHandler16 = void (*)(const common::u32, const common::u16);
using WriteHandler32 = void (*)(const common::u32, const common::u32);

// 512 MB physical address space
constexpr common::u64 ADDRESS_SPACE = 0x20000000;

constexpr common::u64 PAGE_SIZE = 0x1000;
constexpr common::u64 PAGE_MASK = PAGE_SIZE - 1;
constexpr common::u64 NUM_PAGES = ADDRESS_SPACE / PAGE_SIZE;

// This describes all read/write handlers for a page (or more)
struct PageDescriptor {
    ReadHandler8  read8_func;
    ReadHandler16 read16_func;
    ReadHandler32 read32_func;

    WriteHandler8  write8_func;
    WriteHandler16 write16_func;
    WriteHandler32 write32_func;
};

// SC and ME bus
class Bus {
private:
    std::shared_ptr<spdlog::logger> logger;

    std::array<PageDescriptor, NUM_PAGES> page_table;

public:
    Bus(const char* bus_name);
    ~Bus();

    template<typename T>
    T read(const common::u32 addr);

    template<typename T>
    void write(const common::u32 addr, const T data);

    void dump(const char* dump_path, const common::u32 addr, const common::u32 size);

    // Maps read/write handlers 
    void map(const common::u32 addr, const common::u32 size, const PageDescriptor page_desc);

    void unmap(const common::u32 addr, const common::u32 size);
};

};
