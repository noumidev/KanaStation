/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/bus.cpp - Memory bus */

#include <core/hw/bus.hpp>

#include <array>
#include <cstdlib>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace kanacore::hw::bus {

using namespace common;

Bus::Bus(const char* bus_name) {
    logger = spdlog::stdout_color_st(bus_name);

    page_table.fill(PageDescriptor{});
}

Bus::~Bus() {
    
}

template<>
u8 Bus::read(const u32 addr) {
    if (addr >= ADDRESS_SPACE) {
        logger->error("read8 address out of bounds ({:08X})", addr);
        exit(1);
    }

    const ReadHandler8 read8_func = page_table[addr / PAGE_SIZE].read8_func;

    if (read8_func == nullptr) {
        logger->warn("Unmapped read8 @ {:08X}", addr);
        return 0;
    }

    return read8_func(addr);
}

template<>
u16 Bus::read(const u32 addr) {
    if (addr >= ADDRESS_SPACE) {
        logger->error("read16 address out of bounds ({:08X})", addr);
        exit(1);
    }

    const ReadHandler16 read16_func = page_table[addr / PAGE_SIZE].read16_func;

    if (read16_func == nullptr) {
        logger->warn("Unmapped read16 @ {:08X}", addr);
        return 0;
    }

    return read16_func(addr);
}

template<>
u32 Bus::read(const u32 addr) {
    if (addr >= ADDRESS_SPACE) {
        logger->error("read32 address out of bounds ({:08X})", addr);
        exit(1);
    }

    const ReadHandler32 read32_func = page_table[addr / PAGE_SIZE].read32_func;

    if (read32_func == nullptr) {
        logger->warn("Unmapped read32 @ {:08X}", addr);
        return 0;
    }

    return read32_func(addr);
}

template<>
void Bus::write(const u32 addr, const u8 data) {
    if (addr >= ADDRESS_SPACE) {
        logger->error("write8 address out of bounds ({:08X})", addr);
        exit(1);
    }

    const WriteHandler8 write8_func = page_table[addr / PAGE_SIZE].write8_func;

    if (write8_func == nullptr) {
        logger->warn("Unmapped write8 @ {:08X} = {:02X}", addr, data);
        return;
    }

    write8_func(addr, data);
}

template<>
void Bus::write(const u32 addr, const u16 data) {
    if (addr >= ADDRESS_SPACE) {
        logger->error("write16 address out of bounds ({:08X})", addr);
        exit(1);
    }

    const WriteHandler16 write16_func = page_table[addr / PAGE_SIZE].write16_func;

    if (write16_func == nullptr) {
        logger->warn("Unmapped write16 @ {:08X} = {:04X}", addr, data);
        return;
    }

    write16_func(addr, data);
}

template<>
void Bus::write(const u32 addr, const u32 data) {
    if (addr >= ADDRESS_SPACE) {
        logger->error("write32 address out of bounds ({:08X})", addr);
        exit(1);
    }

    const WriteHandler32 write32_func = page_table[addr / PAGE_SIZE].write32_func;

    if (write32_func == nullptr) {
        logger->warn("Unmapped write32 @ {:08X} = {:08X}", addr, data);
        return;
    }

    write32_func(addr, data);
}

void Bus::map(const u32 addr, const u32 size, const PageDescriptor page_desc) {
    if ((addr & PAGE_MASK) != 0) {
        logger->error("Address not aligned on a page boundary {:08X}", addr);
        exit(1);
    }

    if ((size & PAGE_MASK) != 0) {
        logger->error("Size not aligned on a page boundary {}", size);
        exit(1);
    }

    const u64 first_page = addr / PAGE_SIZE;
    const u64 num_pages  = size / PAGE_SIZE;

    for (u64 page = first_page; page < (first_page + num_pages); page++) {
        // Further address decoding happens in the read/write handlers
        page_table[page] = page_desc;
    }
}

void Bus::unmap(const u32 addr, const u32 size) {
    if ((addr & PAGE_MASK) != 0) {
        logger->error("Address not aligned on a page boundary {:08X}", addr);
        exit(1);
    }

    if ((size & PAGE_MASK) != 0) {
        logger->error("Size not aligned on a page boundary {}", size);
        exit(1);
    }

    const u64 first_page = addr / PAGE_SIZE;
    const u64 num_pages  = size / PAGE_SIZE;

    for (u64 page = first_page; page < (first_page + num_pages); page++) {
        page_table[page] = PageDescriptor{};
    }
}

};
