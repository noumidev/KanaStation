/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/dmacplus.cpp - DMACplus */

#include <core/hw/dmacplus.hpp>

#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <core/kanacore.hpp>
#include <core/scheduler.hpp>
#include <core/hw/bus.hpp>
#include <core/hw/intc.hpp>

namespace kanacore::hw::dmacplus {

using namespace common;

constexpr u64 DMACPLUS_ADDR = 0x1C800000;
constexpr u64 DMACPLUS_SIZE = 0x1000;

constexpr int DMACPLUS_INTERRUPT = 21;

constexpr u64 NUM_CHANNELS = 3;

constexpr u32 ADDR_MASK = 0x1FFFFFFF;

enum IoAddress {
    IO_ADDRESS_INTRSTAT       = DMACPLUS_ADDR + 0x000,
    IO_ADDRESS_TC_INTRSTAT    = DMACPLUS_ADDR + 0x004,
    IO_ADDRESS_TC_INTRCLR     = DMACPLUS_ADDR + 0x008,
    IO_ADDRESS_ERROR_INTRSTAT = DMACPLUS_ADDR + 0x00C,
    IO_ADDRESS_ERROR_INTRCLR  = DMACPLUS_ADDR + 0x010,
    IO_ADDRESS_TC_RAWISTAT    = DMACPLUS_ADDR + 0x014,
    IO_ADDRESS_ERROR_RAWISTAT = DMACPLUS_ADDR + 0x018,
    IO_ADDRESS_LCDC_FBADDR    = DMACPLUS_ADDR + 0x100,
    IO_ADDRESS_LCDC_FBFORMAT  = DMACPLUS_ADDR + 0x104,
    IO_ADDRESS_LCDC_FBWIDTH   = DMACPLUS_ADDR + 0x108,
    IO_ADDRESS_LCDC_FBSTRIDE  = DMACPLUS_ADDR + 0x10C,
    IO_ADDRESS_LCDC_FBCTRL    = DMACPLUS_ADDR + 0x110,
    IO_ADDRESS_CSC_Y0ADDR     = DMACPLUS_ADDR + 0x120,
    IO_ADDRESS_CSC_START      = DMACPLUS_ADDR + 0x160,
    IO_ADDRESS_SC128_SRCADDR  = DMACPLUS_ADDR + 0x1C0,
    IO_ADDRESS_SC128_DSTADDR  = DMACPLUS_ADDR + 0x1C4,
    IO_ADDRESS_SC128_LINKADDR = DMACPLUS_ADDR + 0x1C8,
    IO_ADDRESS_SC128_CONTROL  = DMACPLUS_ADDR + 0x1CC,
    IO_ADDRESS_SC128_CONFIG   = DMACPLUS_ADDR + 0x1D0,
};

#define HW_DMACPLUS_INTRSTAT       ctx.interrupt_status
#define HW_DMACPLUS_TC_INTRSTAT    ctx.tc_interrupt_status
#define HW_DMACPLUS_ERROR_INTRSTAT ctx.error_interrupt_status
#define HW_DMACPLUS_TC_RAWISTAT    ctx.raw_tc_interrupt_status
#define HW_DMACPLUS_ERROR_RAWISTAT ctx.raw_error_interrupt_status
#define HW_DMACPLUS_LCDC_FBADDR    ctx.framebuffer_addr
#define HW_DMACPLUS_LCDC_FBFORMAT  ctx.framebuffer_format
#define HW_DMACPLUS_LCDC_FBWIDTH   ctx.framebuffer_width
#define HW_DMACPLUS_LCDC_FBSTRIDE  ctx.framebuffer_stride
#define HW_DMACPLUS_LCDC_FBCTRL    ctx.framebuffer_control
#define HW_DMACPLUS_SC128_SRCADDR  channels[2].source_addr
#define HW_DMACPLUS_SC128_DSTADDR  channels[2].destination_addr
#define HW_DMACPLUS_SC128_LINKADDR channels[2].link_addr
#define HW_DMACPLUS_SC128_CONTROL  channels[2].control
#define HW_DMACPLUS_SC128_CONFIG   channels[2].configuration

std::array<u32, 480 * 272> framebuffer;

// Just standard PrimeCell PL080 channels
static struct {
    u32 source_addr;
    u32 destination_addr;
    u32 link_addr;

    union {
        u32 raw;

        struct {
            u32 transfer_length       : 12;
            u32 source_burst          : 3;
            u32 destination_burst     : 3;
            u32 source_width          : 3;
            u32 destination_width     : 3;
            u32 source_select         : 1;
            u32 destination_select    : 1;
            u32 source_increment      : 1;
            u32 destination_increment : 1;
            u32 protection            : 3;
            u32 interrupt_enable      : 1;
        };
    } control;

    union {
        u32 raw;

        struct {
            u32 channel_enable         : 1;
            u32 source_peripheral      : 4;
            u32                        : 1;
            u32 destination_peripheral : 4;
            u32                        : 1;
            u32 flow_control           : 3;
            u32 error_mask             : 1;
            u32 interrupt_mask         : 1;
            u32 lock_enable            : 1;
            u32 active                 : 1;
            u32 halt                   : 1;
            u32                        : 13;
        };
    } configuration;
} channels[NUM_CHANNELS];

static struct {
    u32 interrupt_status;
    u32 tc_interrupt_status;
    u32 error_interrupt_status;
    u32 raw_tc_interrupt_status;
    u32 raw_error_interrupt_status;

    u32 framebuffer_addr;
    u32 framebuffer_format;
    u32 framebuffer_width;
    u32 framebuffer_stride;

    union {
        u32 raw;

        struct {
            u32 enable : 1;
            u32        : 31;
        };
    } framebuffer_control;
} ctx;

static std::shared_ptr<spdlog::logger> logger;

static void check_pending_interrupts() {
    HW_DMACPLUS_INTRSTAT = HW_DMACPLUS_TC_INTRSTAT | HW_DMACPLUS_ERROR_INTRSTAT;

    if (HW_DMACPLUS_INTRSTAT != 0) {
        intc::assert_sc_interrupt(DMACPLUS_INTERRUPT);
    } else {
        intc::clear_sc_interrupt(DMACPLUS_INTERRUPT);
    }
}

static void assert_terminal_count_interrupt(const u32 channel, const bool mask) {
    HW_DMACPLUS_TC_RAWISTAT |= 1 << channel;

    if (mask) {
        HW_DMACPLUS_TC_INTRSTAT |= 1 << channel;
    }

    check_pending_interrupts();
}

static void end_sc128_transfer(const int) {
    HW_DMACPLUS_SC128_CONTROL.transfer_length = 0;
    HW_DMACPLUS_SC128_CONFIG.active = 0;
    HW_DMACPLUS_SC128_CONFIG.channel_enable = 0;

    if (HW_DMACPLUS_SC128_CONTROL.interrupt_enable) {
        assert_terminal_count_interrupt(4, HW_DMACPLUS_SC128_CONFIG.interrupt_mask);
    }
}

static void start_sc128_transfer() {
    bus::Bus* bus = kanacore::get_sc_bus_ptr();

    const u32 length = HW_DMACPLUS_SC128_CONTROL.transfer_length;

    logger->debug(
        "SC128 transfer (source address: {:08X}, destination address: {:08X}, length: {:03X})",
        HW_DMACPLUS_SC128_SRCADDR,
        HW_DMACPLUS_SC128_DSTADDR,
        length
    );

    // Sanity checks

    // No link?
    assert(HW_DMACPLUS_SC128_LINKADDR == 0);
    // Memory to memory
    assert(HW_DMACPLUS_SC128_CONFIG.flow_control == 0);
    // Transfer width is 128-bit
    assert(HW_DMACPLUS_SC128_CONTROL.source_width == 4);
    assert(HW_DMACPLUS_SC128_CONTROL.destination_width == 4);
    // Increment addresses
    assert(HW_DMACPLUS_SC128_CONTROL.source_increment);
    assert(HW_DMACPLUS_SC128_CONTROL.destination_increment);

    for (u32 i = 0; i < length; i++) {
        // One 128-bit beat
        const u32 source_addr = HW_DMACPLUS_SC128_SRCADDR & ADDR_MASK;
        const u32 destination_addr = HW_DMACPLUS_SC128_DSTADDR & ADDR_MASK;

        bus->write<u32>(destination_addr + 0x0, bus->read<u32>(source_addr + 0x0));
        bus->write<u32>(destination_addr + 0x4, bus->read<u32>(source_addr + 0x4));
        bus->write<u32>(destination_addr + 0x8, bus->read<u32>(source_addr + 0x8));
        bus->write<u32>(destination_addr + 0xC, bus->read<u32>(source_addr + 0xC));

        HW_DMACPLUS_SC128_SRCADDR += 16;
        HW_DMACPLUS_SC128_DSTADDR += 16;
    }

    scheduler::schedule_event(
        scheduler::EventType::DMACPLUS_DMA,
        end_sc128_transfer,
        0,
        32 * length
    );
}

static u32 read(const u32 addr) {
    if ((addr >= IoAddress::IO_ADDRESS_CSC_Y0ADDR) && (addr <= IoAddress::IO_ADDRESS_CSC_START)) {
        logger->warn("Unmapped CSC read32 @ {:08X}", addr);
        return 0;
    }

    switch (addr) {
        case IoAddress::IO_ADDRESS_TC_INTRSTAT:
            logger->debug("TC_INTRSTAT read32");
            return HW_DMACPLUS_TC_INTRSTAT;
        case IoAddress::IO_ADDRESS_ERROR_INTRSTAT:
            logger->debug("ERROR_INTRSTAT read32");
            return HW_DMACPLUS_ERROR_INTRSTAT;
        case IoAddress::IO_ADDRESS_LCDC_FBADDR:
            logger->debug("LCDC_FBADDR read32");
            return HW_DMACPLUS_LCDC_FBADDR;
        case IoAddress::IO_ADDRESS_LCDC_FBFORMAT:
            logger->debug("LCDC_FBFORMAT read32");
            return HW_DMACPLUS_LCDC_FBFORMAT;
        case IoAddress::IO_ADDRESS_LCDC_FBWIDTH:
            logger->debug("LCDC_FBWIDTH read32");
            return HW_DMACPLUS_LCDC_FBWIDTH;
        case IoAddress::IO_ADDRESS_LCDC_FBSTRIDE:
            logger->debug("LCDC_FBSTRIDE read32");
            return HW_DMACPLUS_LCDC_FBSTRIDE;
        case IoAddress::IO_ADDRESS_LCDC_FBCTRL:
            logger->debug("LCDC_FBCTRL read32");
            return HW_DMACPLUS_LCDC_FBCTRL.raw;
        default:
            logger->error("Unmapped read32 @ {:08X}", addr);
            exit(1);
    }
}

static void set_sc128_config(const u32 data) {
    const bool is_enabled = HW_DMACPLUS_SC128_CONFIG.channel_enable;

    assert(!is_enabled);

    HW_DMACPLUS_SC128_CONFIG.raw = data;

    if (!is_enabled && HW_DMACPLUS_SC128_CONFIG.channel_enable) {
        start_sc128_transfer();
    }
}

static void write(const u32 addr, const u32 data) {
    if ((addr >= IoAddress::IO_ADDRESS_CSC_Y0ADDR) && (addr <= IoAddress::IO_ADDRESS_CSC_START)) {
        logger->warn("Unmapped CSC write32 @ {:08X} = {:08X}", addr, data);
        return;
    }

    switch (addr) {
        case IoAddress::IO_ADDRESS_TC_INTRCLR:
            logger->debug("TC_INTRCLR write32 = {:08X}", data);

            HW_DMACPLUS_TC_INTRSTAT &= ~data;
            HW_DMACPLUS_TC_RAWISTAT &= ~data;

            check_pending_interrupts();
            break;
        case IoAddress::IO_ADDRESS_LCDC_FBADDR:
            logger->debug("LCDC_FBADDR write32 = {:08X}", data);

            HW_DMACPLUS_LCDC_FBADDR = data;
            break;
        case IoAddress::IO_ADDRESS_LCDC_FBFORMAT:
            logger->debug("LCDC_FBFORMAT write32 = {:08X}", data);

            HW_DMACPLUS_LCDC_FBFORMAT = data;
            break;
        case IoAddress::IO_ADDRESS_LCDC_FBWIDTH:
            logger->debug("LCDC_FBWIDTH write32 = {:08X}", data);

            HW_DMACPLUS_LCDC_FBWIDTH = data;
            break;
        case IoAddress::IO_ADDRESS_LCDC_FBSTRIDE:
            logger->debug("LCDC_FBSTRIDE write32 = {:08X}", data);

            HW_DMACPLUS_LCDC_FBSTRIDE = data;
            break;
        case IoAddress::IO_ADDRESS_LCDC_FBCTRL:
            logger->debug("LCDC_FBCTRL write32 = {:08X}", data);

            HW_DMACPLUS_LCDC_FBCTRL.raw = data;
            break;
        case IoAddress::IO_ADDRESS_SC128_SRCADDR:
            logger->debug("SC128_SRCADDR write32 = {:08X}", data);

            HW_DMACPLUS_SC128_SRCADDR = data;
            break;
        case IoAddress::IO_ADDRESS_SC128_DSTADDR:
            logger->debug("SC128_DSTADDR write32 = {:08X}", data);

            HW_DMACPLUS_SC128_DSTADDR = data;
            break;
        case IoAddress::IO_ADDRESS_SC128_LINKADDR:
            logger->debug("SC128_LINKADDR write32 = {:08X}", data);

            HW_DMACPLUS_SC128_LINKADDR = data;
            break;
        case IoAddress::IO_ADDRESS_SC128_CONTROL:
            logger->debug("SC128_CONTROL write32 = {:08X}", data);

            HW_DMACPLUS_SC128_CONTROL.raw = data;
            break;
        case IoAddress::IO_ADDRESS_SC128_CONFIG:
            logger->debug("SC128_CONFIG write32 = {:08X}", data);
            set_sc128_config(data);
            break;
        default:
            logger->error("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            exit(1);
    }
}

void initialize() {
    logger = spdlog::stdout_color_st("DMACplus");

    std::memset(&ctx, 0, sizeof(ctx));
}

void soft_reset() {
    
}

void hard_reset() {
    const bus::PageDescriptor page_desc {
        // To my knowledge, DMACplus I/O is never not read/written using 32-bit accesses
        .read32_func  = read,
        .write32_func = write,
    };

    kanacore::get_sc_bus_ptr()->map(DMACPLUS_ADDR, DMACPLUS_SIZE, page_desc);
}

void shutdown() {

}

void scanout() {
    bus::Bus* bus = kanacore::get_sc_bus_ptr();

    logger->debug(
        "Scanout (address: {:08X}, width: {}, stride: {}, format: {}, control: {})",
        HW_DMACPLUS_LCDC_FBADDR,
        HW_DMACPLUS_LCDC_FBWIDTH,
        HW_DMACPLUS_LCDC_FBSTRIDE,
        HW_DMACPLUS_LCDC_FBFORMAT,
        HW_DMACPLUS_LCDC_FBCTRL.raw
    );

    if (!HW_DMACPLUS_LCDC_FBCTRL.enable || (HW_DMACPLUS_LCDC_FBSTRIDE == 0)) {
        framebuffer.fill(0);
        return;
    }

    assert(HW_DMACPLUS_LCDC_FBFORMAT == 0);
    assert(HW_DMACPLUS_LCDC_FBWIDTH  == 480);

    for (u32 y = 0; y < 272; y++) {
        for (u32 x = 0; x < 480; x++) {
            framebuffer[y * 480 + x] = bus->read<u32>(
                HW_DMACPLUS_LCDC_FBADDR + sizeof(u32) * (y * HW_DMACPLUS_LCDC_FBSTRIDE + x)
            );
        }
    }
}

u8* get_framebuffer_ptr() {
    return (u8*)framebuffer.data();
}

};
