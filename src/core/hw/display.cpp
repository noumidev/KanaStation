/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/display.cpp - Display timer? */

#include <core/hw/display.hpp>

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <common/types.hpp>
#include <core/kanacore.hpp>
#include <core/hw/bus.hpp>
#include <core/hw/intc.hpp>

namespace kanacore::hw::display {

using namespace common;

constexpr u64 DISPLAY_ADDR = 0x1E740000;
constexpr u64 DISPLAY_SIZE = 0x1000;

constexpr int DISPLAY_INTERRUPT = 30;

enum IoAddress {
    IO_ADDRESS_COUNTER  = DISPLAY_ADDR + 0x004,
    IO_ADDRESS_CTRLATCH = DISPLAY_ADDR + 0x008,
    IO_ADDRESS_INTRMASK = DISPLAY_ADDR + 0x024,
};

#define HW_DISPLAY_COUNTER  ctx.counter
#define HW_DISPLAY_CTRLATCH ctx.latched_counter
#define HW_DISPLAY_INTRMASK ctx.interrupt_mask

static std::shared_ptr<spdlog::logger> logger;

static struct {
    u32 counter;
    u32 latched_counter;
    u32 interrupt_mask;
} ctx;

static u32 read(const u32 addr) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_COUNTER:
            logger->debug("COUNTER read32");
            return HW_DISPLAY_COUNTER;
        case IoAddress::IO_ADDRESS_CTRLATCH:
            // This register seems to "record" the counter after a timer "overflow"
            logger->debug("CTRLATCH read32");
            return HW_DISPLAY_CTRLATCH;
        case DISPLAY_ADDR + 0x020:
            logger->warn("Unmapped read32 @ {:08X}", addr);
            return 0;
        default:
            logger->error("Unmapped read32 @ {:08X}", addr);
            exit(1);
    }
}

static void write(const u32 addr, const u32 data) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_COUNTER:
            logger->debug("COUNTER write32 = {:08X}", data);

            HW_DISPLAY_COUNTER = data;
            break;
        case IoAddress::IO_ADDRESS_INTRMASK:
            logger->debug("INTRMASK write32 = {:08X}", data);

            HW_DISPLAY_INTRMASK = data;
            break;
        case DISPLAY_ADDR + 0x00C:
            // This could be a prescaler of some sorts? Or maybe this clears
            // the latched counter? Firmware writes all 1s to this register
            HW_DISPLAY_CTRLATCH = data;
        case DISPLAY_ADDR + 0x000:
            // This might be a control register
        case DISPLAY_ADDR + 0x010:
        case DISPLAY_ADDR + 0x014:
        case DISPLAY_ADDR + 0x020:
            // This might contain flags that indicate when certain register
            // updates are done?? The firmware checks it every time it needs
            // to write new register values, checking different bits each time
            logger->warn("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            break;
        default:
            logger->error("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            exit(1);
    }
}

void initialize() {
    logger = spdlog::stdout_color_st("Display");

    std::memset(&ctx, 0, sizeof(ctx));
}

void soft_reset() {
    
}

void hard_reset() {
    const bus::PageDescriptor page_desc {
        // To my knowledge, display I/O is never not read/written using 32-bit accesses
        .read32_func  = read,
        .write32_func = write,
    };

    kanacore::get_sc_bus_ptr()->map(DISPLAY_ADDR, DISPLAY_SIZE, page_desc);
}

void shutdown() {

}

// HSYNC and VSYNC are currently driven by the LCDC... once we figure out
// how this timer works, we can change this

void hsync() {
    HW_DISPLAY_COUNTER++;
}

void vsync() {
    HW_DISPLAY_CTRLATCH = HW_DISPLAY_COUNTER;

    if ((HW_DISPLAY_INTRMASK & 1) != 0) {
        hw::intc::assert_sc_interrupt(DISPLAY_INTERRUPT);
    }
}

};
