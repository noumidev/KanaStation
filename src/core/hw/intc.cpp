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

#include <core/kanacore.hpp>
#include <core/hw/allegrex/allegrex.hpp>
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

#define HW_INTC_FLAGS        ctx.flags
#define HW_INTC_FLAGS_LO     ctx.flags[0]
#define HW_INTC_FLAGS_MID    ctx.flags[1]
#define HW_INTC_FLAGS_HI     ctx.flags[2]
#define HW_INTC_RAWFLAGS     ctx.raw_flags
#define HW_INTC_RAWFLAGS_LO  ctx.raw_flags[0]
#define HW_INTC_RAWFLAGS_MID ctx.raw_flags[1]
#define HW_INTC_RAWFLAGS_HI  ctx.raw_flags[2]
#define HW_INTC_MASK         ctx.mask
#define HW_INTC_MASK_LO      ctx.mask[0]
#define HW_INTC_MASK_MID     ctx.mask[1]
#define HW_INTC_MASK_HI      ctx.mask[2]

static struct {
    u32 flags[NUM_REGS];
    u32 raw_flags[NUM_REGS];
    u32 mask[NUM_REGS];
} ctx;

static std::shared_ptr<spdlog::logger> logger;

static void check_pending_interrupts() {
    hw::allegrex::Allegrex* sc = kanacore::get_sc_ptr();

    if (
        ((HW_INTC_FLAGS[0] & HW_INTC_MASK[0]) != 0) ||
        ((HW_INTC_FLAGS[1] & HW_INTC_MASK[1]) != 0) ||
        ((HW_INTC_FLAGS[2] & HW_INTC_MASK[2]) != 0)
    ) {
        sc->assert_interrupt();
    } else {
        sc->clear_interrupt();
    }
}

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
            HW_INTC_FLAGS_LO |= (HW_INTC_MASK_LO & HW_INTC_RAWFLAGS_LO);
            break;
        case IoAddress::IO_ADDRESS_MASK_MID:
            logger->debug("MASK_MID write32 = {:08X}", data);

            HW_INTC_MASK_MID = data;
            HW_INTC_FLAGS_MID |= (HW_INTC_MASK_MID & HW_INTC_RAWFLAGS_MID);
            break;
        default:
            logger->error("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            exit(1);
    }

    check_pending_interrupts();
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

void assert_interrupt(const int intr_num) {
    const int reg_idx = intr_num >> 5;
    const int intr_bit = 1 << (intr_num & 31);

    assert((u64)reg_idx < NUM_REGS);

    u32* flags = &HW_INTC_FLAGS[reg_idx];
    u32* raw_flags = &HW_INTC_RAWFLAGS[reg_idx];
    u32* mask = &HW_INTC_MASK[reg_idx];

    *raw_flags |= intr_bit;

    if ((*mask & intr_bit) != 0) {
        *flags |= intr_bit;

        kanacore::get_sc_ptr()->assert_interrupt();
    }
}

};
