/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/intc.cpp - Interrupt controller registers */

#include <core/hw/intc.hpp>

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <core/hw/bus.hpp>

namespace kanacore::hw::intc {

using namespace common;

constexpr u64 INTC_ADDR = 0x1C300000;
constexpr u64 INTC_SIZE = 0x1000;

constexpr u64 NUM_REGS = 3;

enum IoAddress {
    IO_ADDRESS_MASK_LO  = INTC_ADDR + 0x008,
    IO_ADDRESS_MASK_MID = INTC_ADDR + 0x018,
    IO_ADDRESS_MASK_HI  = INTC_ADDR + 0x028,
};

#define HW_INTC_MASK_LO  ctx.mask[0]
#define HW_INTC_MASK_MID ctx.mask[1]
#define HW_INTC_MASK_HI  ctx.mask[2]

static struct {
    u32 flags[NUM_REGS];
    u32 raw_flags[NUM_REGS];
    u32 mask[NUM_REGS];
} ctx;

static std::shared_ptr<spdlog::logger> logger;

static u32 read(const u32 addr) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_MASK_LO:
            logger->debug("MASK_LO read32");
            return HW_INTC_MASK_LO;
        case IoAddress::IO_ADDRESS_MASK_MID:
            logger->debug("MASK_MID read32");
            return HW_INTC_MASK_MID;
        default:
            logger->error("Unmapped read32 @ {:08X}", addr);
            exit(1);
    }
}

static void write(const u32 addr, const u32 data) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_MASK_LO:
            logger->debug("MASK_LO write32 = {:08X}", data);

            HW_INTC_MASK_LO = data;
            break;
        case IoAddress::IO_ADDRESS_MASK_MID:
            logger->debug("MASK_MID write32 = {:08X}", data);

            HW_INTC_MASK_MID = data;
            break;
        default:
            logger->error("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            exit(1);
    }
}

void initialize() {
    logger = spdlog::stdout_color_st("INTC");

    std::memset(&ctx, 0, sizeof(ctx));
}

void soft_reset() {
    
}

void hard_reset() {
    const bus::PageDescriptor page_desc {
        // To my knowledge, INTC I/O is never not read/written using 32-bit accesses
        .read32_func  = read,
        .write32_func = write,
    };

    bus::map(INTC_ADDR, INTC_SIZE, page_desc);
}

void shutdown() {

}

};
