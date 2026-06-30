/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/dmac.cpp - ARM PrimeCell PL080 DMA controllers */

#include <core/hw/dmac.hpp>

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

namespace kanacore::hw::dmac {

using namespace common;

constexpr u64 DMAC0_ADDR = 0x1C900000;
constexpr u64 DMAC1_ADDR = 0x1CA00000;
constexpr u64 DMAC_SIZE = 0x1000;

// Not an actual DMAC base address
constexpr u64 IO_ADDR = 0x1C000000;

constexpr int DMAC_INTERRUPT = 22;

constexpr u64 NUM_DMACS = 2;
constexpr u64 NUM_CHANNELS = 8;

constexpr u32 ADDR_MASK = 0x1FFFFFFF;

enum IoAddress {
    IO_ADDRESS_INTRSTAT       = IO_ADDR + 0x000,
    IO_ADDRESS_TC_INTRSTAT    = IO_ADDR + 0x004,
    IO_ADDRESS_TC_INTRCLR     = IO_ADDR + 0x008,
    IO_ADDRESS_ERROR_INTRSTAT = IO_ADDR + 0x00C,
    IO_ADDRESS_ERROR_INTRCLR  = IO_ADDR + 0x010,
    IO_ADDRESS_TC_RAWISTAT    = IO_ADDR + 0x014,
    IO_ADDRESS_ERROR_RAWISTAT = IO_ADDR + 0x018,
    IO_ADDRESS_CONFIG         = IO_ADDR + 0x030,
    IO_ADDRESS_SYNC           = IO_ADDR + 0x034,
    IO_ADDRESS_CHAN0_SRCADDR  = IO_ADDR + 0x100,
    IO_ADDRESS_CHAN0_DSTADDR  = IO_ADDR + 0x104,
    IO_ADDRESS_CHAN0_LINKADDR = IO_ADDR + 0x108,
    IO_ADDRESS_CHAN0_CONTROL  = IO_ADDR + 0x10C,
    IO_ADDRESS_CHAN0_CONFIG   = IO_ADDR + 0x110,
    IO_ADDRESS_CHAN7_CONFIG   = IO_ADDR + 0x1F0,
};

#define HW_DMAC_INTRSTAT       dmac->interrupt_status
#define HW_DMAC_TC_INTRSTAT    dmac->tc_interrupt_status
#define HW_DMAC_ERROR_INTRSTAT dmac->error_interrupt_status
#define HW_DMAC_TC_RAWISTAT    dmac->raw_tc_interrupt_status
#define HW_DMAC_ERROR_RAWISTAT dmac->raw_error_interrupt_status
#define HW_DMAC_CONFIG         dmac->configuration
#define HW_DMAC_CHAN_SRCADDR   chan->source_addr
#define HW_DMAC_CHAN_DSTADDR   chan->destination_addr
#define HW_DMAC_CHAN_LINKADDR  chan->link_addr
#define HW_DMAC_CHAN_CONTROL   chan->control
#define HW_DMAC_CHAN_CONFIG    chan->configuration

struct Dmac {
    std::shared_ptr<spdlog::logger> logger;

    u32 interrupt_status;
    u32 tc_interrupt_status;
    u32 error_interrupt_status;
    u32 raw_tc_interrupt_status;
    u32 raw_error_interrupt_status;

    union {
        u32 raw;

        struct {
            u32 dmac_enable    : 1;
            u32 ahb_endianness : 2;
            u32                : 29;
        };
    } configuration;

    struct {
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
};

static struct {} ctx;

std::array<Dmac, NUM_DMACS> dmacs;

template<int dmac_num>
static void check_pending_interrupts() {
    static_assert(dmac_num< NUM_DMACS);
    
    Dmac* dmac = &dmacs[dmac_num];

    HW_DMAC_INTRSTAT = HW_DMAC_TC_INTRSTAT | HW_DMAC_ERROR_INTRSTAT;

    if (HW_DMAC_INTRSTAT != 0) {
        intc::assert_sc_interrupt(DMAC_INTERRUPT + dmac_num);
    } else {
        intc::clear_sc_interrupt(DMAC_INTERRUPT + dmac_num);
    }
}

template<int dmac_num>
static void assert_terminal_count_interrupt(const u32 channel, const bool mask) {
    static_assert(dmac_num< NUM_DMACS);
    
    Dmac* dmac = &dmacs[dmac_num];

    HW_DMAC_TC_RAWISTAT |= 1 << channel;

    if (mask) {
        HW_DMAC_TC_INTRSTAT |= 1 << channel;
    }

    check_pending_interrupts<dmac_num>();
}

template<int dmac_num>
static void start_transfer(const int chan_idx);

template<int dmac_num>
static void end_transfer(const int chan_idx) {
    static_assert(dmac_num< NUM_DMACS);

    bus::Bus* bus = kanacore::get_sc_bus_ptr();
    
    Dmac* dmac = &dmacs[dmac_num];
    auto* chan = &dmac->channels[chan_idx];

    if (HW_DMAC_CHAN_CONTROL.interrupt_enable) {
        assert_terminal_count_interrupt<dmac_num>(chan_idx, HW_DMAC_CHAN_CONFIG.interrupt_mask);
    }

    if (HW_DMAC_CHAN_LINKADDR != 0) {
        // Scatter/gather
        const u32 link_addr = HW_DMAC_CHAN_LINKADDR;

        HW_DMAC_CHAN_SRCADDR  = bus->read<u32>(link_addr + 0x0);
        HW_DMAC_CHAN_DSTADDR  = bus->read<u32>(link_addr + 0x4);
        HW_DMAC_CHAN_LINKADDR = bus->read<u32>(link_addr + 0x8);
        HW_DMAC_CHAN_CONTROL.raw = bus->read<u32>(link_addr + 0xC);

        if (HW_DMAC_CHAN_CONFIG.channel_enable) {
            start_transfer<dmac_num>(chan_idx);
        }
    } else {
        HW_DMAC_CHAN_CONTROL.transfer_length = 0;
        HW_DMAC_CHAN_CONFIG.active = 0;
        HW_DMAC_CHAN_CONFIG.channel_enable = 0;
    }
}

template<int dmac_num>
static void start_transfer(const int chan_idx) {
    static_assert(dmac_num< NUM_DMACS);

    bus::Bus* bus = kanacore::get_sc_bus_ptr();
    
    Dmac* dmac = &dmacs[dmac_num];
    auto* chan = &dmac->channels[chan_idx];

    const u32 length = HW_DMAC_CHAN_CONTROL.transfer_length;

    dmac->logger->debug(
        "CHAN{} transfer (source address: {:08X}, destination address: {:08X}, length: {:03X})",
        chan_idx,
        HW_DMAC_CHAN_SRCADDR,
        HW_DMAC_CHAN_DSTADDR,
        length
    );

    // Sanity checks

    // Flow controller is DMAC
    assert(HW_DMAC_CHAN_CONFIG.flow_control < 4);
    // Transfer width is 32-bit
    assert(HW_DMAC_CHAN_CONTROL.source_width == 2);
    assert(HW_DMAC_CHAN_CONTROL.destination_width == 2);

    const u32 source_offset = HW_DMAC_CHAN_CONTROL.source_increment ? 4 : 0;
    const u32 destination_offset = HW_DMAC_CHAN_CONTROL.destination_increment ? 4 : 0;

    for (u32 i = 0; i < length; i++) {
        // Single 32-bit beat
        // Later on, we could emulate AHB bursts
        const u32 source_addr = HW_DMAC_CHAN_SRCADDR & ADDR_MASK;
        const u32 destination_addr = HW_DMAC_CHAN_DSTADDR & ADDR_MASK;

        bus->write<u32>(destination_addr, bus->read<u32>(source_addr));

        HW_DMAC_CHAN_SRCADDR += source_offset;
        HW_DMAC_CHAN_DSTADDR += destination_offset;
    }

    scheduler::schedule_event(
        scheduler::EventType::DMAC_DMA,
        end_transfer<dmac_num>,
        chan_idx,
        32 * length
    );
}

template<int dmac_num>
static u32 read(const u32 addr) {
    static_assert(dmac_num< NUM_DMACS);
    
    Dmac* dmac = &dmacs[dmac_num];

    const u32 masked_addr = addr & ~0xF00000;

    if ((masked_addr >= IoAddress::IO_ADDRESS_CHAN0_SRCADDR) && (masked_addr <= IoAddress::IO_ADDRESS_CHAN7_CONFIG)) {
        const int chan_idx = (addr >> 5) & 7;

        auto* chan = &dmac->channels[chan_idx];

        switch (masked_addr & ~0xE0) {
            case IoAddress::IO_ADDRESS_CHAN0_SRCADDR:
                dmac->logger->debug("CHAN{}_SRCADDR read32", chan_idx);
                return HW_DMAC_CHAN_SRCADDR;
            case IoAddress::IO_ADDRESS_CHAN0_DSTADDR:
                dmac->logger->debug("CHAN{}_DSTADDR read32", chan_idx);
                return HW_DMAC_CHAN_DSTADDR;
            case IoAddress::IO_ADDRESS_CHAN0_LINKADDR:
                dmac->logger->debug("CHAN{}_LINKADDR read32", chan_idx);
                return HW_DMAC_CHAN_LINKADDR;
            case IoAddress::IO_ADDRESS_CHAN0_CONTROL:
                dmac->logger->debug("CHAN{}_CONTROL read32", chan_idx);
                return HW_DMAC_CHAN_CONTROL.raw;
            case IoAddress::IO_ADDRESS_CHAN0_CONFIG:
                dmac->logger->debug("CHAN{}_CONFIG read32", chan_idx);
                return HW_DMAC_CHAN_CONFIG.raw;
            default:
                dmac->logger->error("Unmapped CHAN{} read32 @ {:08X}", chan_idx, addr);
                exit(1);
        }
    }

    switch (masked_addr) {
        case IoAddress::IO_ADDRESS_TC_INTRSTAT:
            dmac->logger->debug("TC_INTRSTAT read32");
            return HW_DMAC_TC_INTRSTAT;
        case IoAddress::IO_ADDRESS_ERROR_INTRSTAT:
            dmac->logger->debug("ERROR_INTRSTAT read32");
            return HW_DMAC_ERROR_INTRSTAT;
        case IoAddress::IO_ADDRESS_CONFIG:
            dmac->logger->debug("CONFIG read32");
            return HW_DMAC_CONFIG.raw;
        default:
            dmac->logger->error("Unmapped read32 @ {:08X}", addr);
            exit(1);
    }
}

template<int dmac_num>
static void set_chan_config(const int chan_idx, const u32 data) {
    static_assert(dmac_num< NUM_DMACS);

    auto* chan = &dmacs[dmac_num].channels[chan_idx];

    const bool is_enabled = HW_DMAC_CHAN_CONFIG.channel_enable;

    assert(!is_enabled);

    HW_DMAC_CHAN_CONFIG.raw = data;

    if (!is_enabled && HW_DMAC_CHAN_CONFIG.channel_enable) {
        start_transfer<dmac_num>(chan_idx);
    }
}

template<int dmac_num>
static void write(const u32 addr, const u32 data) {
    static_assert(dmac_num< NUM_DMACS);
    
    Dmac* dmac = &dmacs[dmac_num];

    const u32 masked_addr = addr & ~0xF00000;

    if ((masked_addr >= IoAddress::IO_ADDRESS_CHAN0_SRCADDR) && (masked_addr <= IoAddress::IO_ADDRESS_CHAN7_CONFIG)) {
        const int chan_idx = (addr >> 5) & 7;

        auto* chan = &dmac->channels[chan_idx];

        switch (masked_addr & ~0xE0) {
            case IoAddress::IO_ADDRESS_CHAN0_SRCADDR:
                dmac->logger->debug("CHAN{}_SRCADDR write32 = {:08X}", chan_idx, data);

                HW_DMAC_CHAN_SRCADDR = data;
                break;
            case IoAddress::IO_ADDRESS_CHAN0_DSTADDR:
                dmac->logger->debug("CHAN{}_DSTADDR write32 = {:08X}", chan_idx, data);

                HW_DMAC_CHAN_DSTADDR = data;
                break;
            case IoAddress::IO_ADDRESS_CHAN0_LINKADDR:
                dmac->logger->debug("CHAN{}_LINKADDR write32 = {:08X}", chan_idx, data);

                HW_DMAC_CHAN_LINKADDR = data;
                break;
            case IoAddress::IO_ADDRESS_CHAN0_CONTROL:
                dmac->logger->debug("CHAN{}_CONTROL write32 = {:08X}", chan_idx, data);

                HW_DMAC_CHAN_CONTROL.raw = data;
                break;
            case IoAddress::IO_ADDRESS_CHAN0_CONFIG:
                dmac->logger->debug("CHAN{}_CONFIG write32 = {:08X}", chan_idx, data);
                set_chan_config<dmac_num>(chan_idx, data);
                break;
            default:
                dmac->logger->error("Unmapped CHAN{} write32 @ {:08X} = {:08X}", chan_idx, addr, data);
                exit(1);
        }

        return;
    }

    switch (masked_addr) {
        case IoAddress::IO_ADDRESS_TC_INTRCLR:
            dmac->logger->debug("TC_INTRCLR write32 = {:08X}", data);

            HW_DMAC_TC_INTRSTAT &= ~data;
            HW_DMAC_TC_RAWISTAT &= ~data;

            check_pending_interrupts<dmac_num>();
            break;
        case IoAddress::IO_ADDRESS_ERROR_INTRCLR:
            dmac->logger->debug("ERROR_INTRCLR write32 = {:08X}", data);

            HW_DMAC_ERROR_INTRSTAT &= ~data;
            HW_DMAC_ERROR_RAWISTAT &= ~data;

            check_pending_interrupts<dmac_num>();
            break;
        case IoAddress::IO_ADDRESS_CONFIG:
            dmac->logger->debug("CONFIG write32 = {:08X}", data);

            HW_DMAC_CONFIG.raw = data;
            break;
        case IoAddress::IO_ADDRESS_SYNC:
            dmac->logger->debug("SYNC write32 = {:08X}", data);
            break;
        default:
            dmac->logger->warn("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            exit(1);
    }
}

template<int dmac_num>
static void map(const u32 addr) {
    static_assert(dmac_num < NUM_DMACS);

    // To my knowledge, DMAC I/O is never not read/written using 32-bit accesses
    const bus::PageDescriptor page_desc {
        .read32_func  = read<dmac_num>,
        .write32_func = write<dmac_num>,
    };

    kanacore::get_sc_bus_ptr()->map(addr, DMAC_SIZE, page_desc);
}

void initialize() {
    dmacs[0].logger = spdlog::stdout_color_st("DMAC0");
    dmacs[1].logger = spdlog::stdout_color_st("DMAC1");

    std::memset(&ctx, 0, sizeof(ctx));
}

void soft_reset() {
    
}

void hard_reset() {
    map<0>(DMAC0_ADDR);
    map<1>(DMAC1_ADDR);
}

void shutdown() {

}

};
