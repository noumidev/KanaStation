/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/dmacplus.cpp - DMACplus */

#include <core/hw/dmacplus.hpp>

#include <algorithm>
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

#include <core/hw/ge/rasterizer.hpp>

namespace kanacore::hw::dmacplus {

using namespace common;

constexpr u64 DMACPLUS_ADDR = 0x1C800000;
constexpr u64 DMACPLUS_SIZE = 0x1000;

constexpr int DMACPLUS_INTERRUPT = 21;

constexpr u64 NUM_CHANNELS = 3;

constexpr u32 ADDR_MASK = 0x1FFFFFFF;

constexpr i64 CYCLES_PER_BEAT = 4;

enum IoOffset {
    IO_OFFSET_INTRSTAT       = 0x000,
    IO_OFFSET_TC_INTRSTAT    = 0x004,
    IO_OFFSET_TC_INTRCLR     = 0x008,
    IO_OFFSET_ERROR_INTRSTAT = 0x00C,
    IO_OFFSET_ERROR_INTRCLR  = 0x010,
    IO_OFFSET_TC_RAWISTAT    = 0x014,
    IO_OFFSET_ERROR_RAWISTAT = 0x018,
    IO_OFFSET_LCDC_FBADDR    = 0x100,
    IO_OFFSET_LCDC_FBFORMAT  = 0x104,
    IO_OFFSET_LCDC_FBWIDTH   = 0x108,
    IO_OFFSET_LCDC_FBSTRIDE  = 0x10C,
    IO_OFFSET_LCDC_FBCTRL    = 0x110,

    IO_OFFSET_CSC_START = 0x120,
    IO_OFFSET_CSC_END   = 0x160,

    IO_OFFSET_CHAN_START = 0x180,
    IO_OFFSET_CHAN_SIZE  = 0x020,
    IO_OFFSET_CHAN_END   = IO_OFFSET_CHAN_START + NUM_CHANNELS * IO_OFFSET_CHAN_SIZE,
};

enum ChanOffset {
    CHAN_OFFSET_SRCADDR  = 0x00,
    CHAN_OFFSET_DSTADDR  = 0x04,
    CHAN_OFFSET_LINKADDR = 0x08,
    CHAN_OFFSET_CONTROL  = 0x0C,
    CHAN_OFFSET_CONFIG   = 0x10,
};

// Each channel uses a different combination of source and destination AHBs
enum class BusId {
    BUS_ID_SC,
    BUS_ID_ME,
};
 
struct ChannelInfo {
    const char* name;
    BusId source_bus;
    BusId destination_bus;
};
 
constexpr ChannelInfo CHANNEL_INFO[NUM_CHANNELS] = {
    { "SC2ME", BusId::BUS_ID_SC, BusId::BUS_ID_ME },
    { "ME2SC", BusId::BUS_ID_ME, BusId::BUS_ID_SC },
    { "SC128", BusId::BUS_ID_SC, BusId::BUS_ID_SC },
};

#define HW_DMACPLUS_TC_RAWISTAT    ctx.raw_tc_interrupt_status
#define HW_DMACPLUS_ERROR_RAWISTAT ctx.raw_error_interrupt_status
#define HW_DMACPLUS_LCDC_FBADDR    ctx.framebuffer_addr
#define HW_DMACPLUS_LCDC_FBFORMAT  ctx.framebuffer_format
#define HW_DMACPLUS_LCDC_FBWIDTH   ctx.framebuffer_width
#define HW_DMACPLUS_LCDC_FBSTRIDE  ctx.framebuffer_stride
#define HW_DMACPLUS_LCDC_FBCTRL    ctx.framebuffer_control

// Just standard PrimeCell PL080 channels (mostly)
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
    
    bool end_pending = false;
} channels[NUM_CHANNELS];

static struct {
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

    u32 event_id = scheduler::NO_EVENT_ID;
} ctx;

static std::shared_ptr<spdlog::logger> logger;

static std::array<u32, 480 * 272> framebuffer;

static bool dmacplus_hacks;

static inline bus::Bus* get_bus(const BusId id) {
    return (id == BusId::BUS_ID_SC) ? kanacore::get_sc_bus_ptr() : kanacore::get_me_bus_ptr();
}

static u32 get_masked_status(const u32 raw, const bool is_terminal_count) {
    u32 unmasked_bits = 0;
 
    for (u32 i = 0; i < NUM_CHANNELS; i++) {
        const auto config = channels[i].configuration;
 
        if (is_terminal_count ? config.interrupt_mask : config.error_mask) {
            unmasked_bits |= 1 << (i + 2);
        }
    }
 
    return raw & unmasked_bits;
}

static u32 get_interrupt_status() {
    return get_masked_status(HW_DMACPLUS_TC_RAWISTAT, true) | get_masked_status(HW_DMACPLUS_ERROR_RAWISTAT, false);
}

static void update_interrupts() {
    if (get_interrupt_status() != 0) {
        intc::assert_sc_interrupt(DMACPLUS_INTERRUPT);
    } else {
        intc::clear_sc_interrupt(DMACPLUS_INTERRUPT);
    }
}

static void start_transfer(const int chan_id);

static void end_transfer(const int chan_id) {
    auto& chan = channels[chan_id];

    // The channel was disabled or reset after this event was scheduled
    if (!chan.end_pending) {
        return;
    }

    chan.end_pending = false;
    chan.configuration.active = 0;

    if (chan.control.interrupt_enable) {
        HW_DMACPLUS_TC_RAWISTAT |= 1 << (chan_id + 2);

        update_interrupts();
    }

    u32 lli_addr = chan.link_addr & ~3;

    if (lli_addr == 0) {
        chan.configuration.channel_enable = 0;
        return;
    }

    lli_addr &= ADDR_MASK;

    bus::Bus* src_bus = get_bus(CHANNEL_INFO[chan_id].source_bus);

    chan.source_addr = src_bus->read<u32>(lli_addr + 0x0);
    chan.destination_addr = src_bus->read<u32>(lli_addr + 0x4);
    chan.link_addr   = src_bus->read<u32>(lli_addr + 0x8);
    chan.control.raw = src_bus->read<u32>(lli_addr + 0xC);

    start_transfer(chan_id);
}

static void start_transfer(const int chan_id) {
    // A 128-bit beat consists of four words. DMACplus transfers seem to
    // swap the words in the two 64-bit halves when bit 2 of the addr is set
    static constexpr u32 WORD_OFFSETS[2][4] = {
        { 0, 1, 2, 3 },
        { 1, 0, 3, 2 },
    };

    auto& chan = channels[chan_id];
    const ChannelInfo& info = CHANNEL_INFO[chan_id];

    // Memory to memory only(?)
    if (chan.configuration.flow_control != 0) {
        logger->error("{} unimplemented flow control {}", info.name, (u32)chan.configuration.flow_control);
        exit(1);
    }

    if ((chan.control.source_width != 4) || (chan.control.destination_width != 4)) {
        logger->error(
            "{} unimplemented transfer width (source: {}, destination: {})",
            info.name,
            (u32)chan.control.source_width,
            (u32)chan.control.destination_width
        );

        exit(1);
    }

    const u32 length = chan.control.transfer_length;
 
    if (length == 0) {
        logger->warn("{} started with a transfer length of zero", info.name);
    }

    logger->debug(
        "{} transfer (source address: {:08X}, destination address: {:08X}, length: {:03X})",
        info.name,
        chan.source_addr,
        chan.destination_addr,
        length
    );

    bus::Bus* src_bus = get_bus(info.source_bus);
    bus::Bus* dst_bus = get_bus(info.destination_bus);

    const u32 src_offset = (chan_id != 0) ? 16 : (chan.control.source_increment ? 16 : 0);
    const u32 dst_offset = (chan_id != 0) ? 16 : (chan.control.destination_increment ? 16 : 0);

    const u32* src_word_offsets = WORD_OFFSETS[(chan.source_addr & 4) != 0];
    const u32* dst_word_offsets = WORD_OFFSETS[(chan.destination_addr & 4) != 0];

    for (u32 i = 0; i < length; i++) {
        const u32 src_addr = chan.source_addr & ADDR_MASK & ~7;
        const u32 dst_addr = chan.destination_addr & ADDR_MASK & ~7;

        const bool skip = dmacplus_hacks && (chan_id == 0) && ((dst_addr & 0x08000000) != 0);

        if (!skip) {
            for (u32 word = 0; word < 4; word++) {
                dst_bus->write<u32>(
                    dst_addr + sizeof(u32) * dst_word_offsets[word],
                    src_bus->read<u32>(src_addr + sizeof(u32) * src_word_offsets[word])
                );
            }
        }

        chan.source_addr += src_offset;
        chan.destination_addr += dst_offset;
    }

    chan.control.transfer_length = 0;

    if (ctx.event_id == scheduler::NO_EVENT_ID) {
        ctx.event_id = scheduler::register_event("DMACPLUS");
    }

    chan.end_pending = true;
    chan.configuration.active = 1;

    scheduler::schedule_event(
        ctx.event_id,
        end_transfer,
        chan_id,
        std::max<i64>(CYCLES_PER_BEAT * length, 1)
    );
}

static u32 read_channel(const u32 chan_idx, const u32 offset) {
    const auto& chan = channels[chan_idx];
    const char* name = CHANNEL_INFO[chan_idx].name;
 
    switch (offset) {
        case ChanOffset::CHAN_OFFSET_SRCADDR:
            logger->debug("{}_SRCADDR read32", name);
            return chan.source_addr;
        case ChanOffset::CHAN_OFFSET_DSTADDR:
            logger->debug("{}_DSTADDR read32", name);
            return chan.destination_addr;
        case ChanOffset::CHAN_OFFSET_LINKADDR:
            logger->debug("{}_LINKADDR read32", name);
            return chan.link_addr;
        case ChanOffset::CHAN_OFFSET_CONTROL:
            logger->debug("{}_CONTROL read32", name);
            return chan.control.raw;
        case ChanOffset::CHAN_OFFSET_CONFIG:
            logger->debug("{}_CONFIG read32", name);
            return chan.configuration.raw;
        default:
            logger->error("Unmapped {} read32 @ {:02X}", name, offset);
            exit(1);
    }
}

static void write_channel_config(const u32 chan_idx, const u32 data) {
    auto& chan = channels[chan_idx];

    const bool was_enabled = chan.configuration.channel_enable;
    const u32 active = chan.configuration.active;

    chan.configuration.raw = data;
    chan.configuration.active = active; // Read-only

    if (chan.configuration.channel_enable) {
        if (!was_enabled) {
            start_transfer(chan_idx);
        }
    } else if (was_enabled) {
        chan.end_pending = false;
        chan.configuration.active = 0;
    }

    update_interrupts();
}

static void write_channel(const u32 chan_idx, const u32 offset, const u32 data) {
    auto& chan = channels[chan_idx];
    const char* name = CHANNEL_INFO[chan_idx].name;

    switch (offset) {
        case ChanOffset::CHAN_OFFSET_SRCADDR:
            logger->debug("{}_SRCADDR write32 = {:08X}", name, data);
            chan.source_addr = data;
            break;
        case ChanOffset::CHAN_OFFSET_DSTADDR:
            logger->debug("{}_DSTADDR write32 = {:08X}", name, data);
            chan.destination_addr = data;
            break;
        case ChanOffset::CHAN_OFFSET_LINKADDR:
            logger->debug("{}_LINKADDR write32 = {:08X}", name, data);
            chan.link_addr = data;
            break;
        case ChanOffset::CHAN_OFFSET_CONTROL:
            logger->debug("{}_CONTROL write32 = {:08X}", name, data);
            chan.control.raw = data;
            break;
        case ChanOffset::CHAN_OFFSET_CONFIG:
            logger->debug("{}_CONFIG write32 = {:08X}", name, data);
            write_channel_config(chan_idx, data);
            break;
        default:
            logger->error("Unmapped {} write32 @ {:02X} = {:08X}", name, offset, data);
            exit(1);
    }
}

static u32 read(const u32 addr) {
    const u32 offset = addr & (DMACPLUS_SIZE - 1);

    if ((offset >= IoOffset::IO_OFFSET_CSC_START) && (offset <= IoOffset::IO_OFFSET_CSC_END)) {
        logger->warn("Unmapped CSC read32 @ {:08X}", addr);
        return 0;
    }

    if ((offset >= IoOffset::IO_OFFSET_CHAN_START) && (offset < IoOffset::IO_OFFSET_CHAN_END)) {
        return read_channel(
            (offset - IoOffset::IO_OFFSET_CHAN_START) / IoOffset::IO_OFFSET_CHAN_SIZE,
            (offset - IoOffset::IO_OFFSET_CHAN_START) % IoOffset::IO_OFFSET_CHAN_SIZE
        );
    }

    switch (offset) {
        case IoOffset::IO_OFFSET_TC_INTRSTAT:
            logger->debug("TC_INTRSTAT read32");
            return get_masked_status(HW_DMACPLUS_TC_RAWISTAT, true);
        case IoOffset::IO_OFFSET_ERROR_INTRSTAT:
            logger->debug("ERROR_INTRSTAT read32");
            return get_masked_status(HW_DMACPLUS_ERROR_RAWISTAT, false);
        case IoOffset::IO_OFFSET_LCDC_FBADDR:
            logger->debug("LCDC_FBADDR read32");
            return HW_DMACPLUS_LCDC_FBADDR;
        case IoOffset::IO_OFFSET_LCDC_FBFORMAT:
            logger->debug("LCDC_FBFORMAT read32");
            return HW_DMACPLUS_LCDC_FBFORMAT;
        case IoOffset::IO_OFFSET_LCDC_FBWIDTH:
            logger->debug("LCDC_FBWIDTH read32");
            return HW_DMACPLUS_LCDC_FBWIDTH;
        case IoOffset::IO_OFFSET_LCDC_FBSTRIDE:
            logger->debug("LCDC_FBSTRIDE read32");
            return HW_DMACPLUS_LCDC_FBSTRIDE;
        case IoOffset::IO_OFFSET_LCDC_FBCTRL:
            logger->debug("LCDC_FBCTRL read32");
            return HW_DMACPLUS_LCDC_FBCTRL.raw;
        default:
            logger->error("Unmapped read32 @ {:08X}", addr);
            exit(1);
    }
}

static void write(const u32 addr, const u32 data) {
    const u32 offset = addr & (DMACPLUS_SIZE - 1);

    if ((offset >= IoOffset::IO_OFFSET_CSC_START) && (offset <= IoOffset::IO_OFFSET_CSC_END)) {
        logger->warn("Unmapped CSC write32 @ {:08X} = {:08X}", addr, data);
        return;
    }

    if ((offset >= IoOffset::IO_OFFSET_CHAN_START) && (offset < IoOffset::IO_OFFSET_CHAN_END)) {
        write_channel(
            (offset - IoOffset::IO_OFFSET_CHAN_START) / IoOffset::IO_OFFSET_CHAN_SIZE,
            (offset - IoOffset::IO_OFFSET_CHAN_START) % IoOffset::IO_OFFSET_CHAN_SIZE,
            data
        );

        return;
    }

    switch (offset) {
        case IoOffset::IO_OFFSET_TC_INTRCLR:
            logger->debug("TC_INTRCLR write32 = {:08X}", data);

            HW_DMACPLUS_TC_RAWISTAT &= ~data;

            update_interrupts();
            break;
        case IoOffset::IO_OFFSET_LCDC_FBADDR:
            logger->debug("LCDC_FBADDR write32 = {:08X}", data);

            HW_DMACPLUS_LCDC_FBADDR = data;
            break;
        case IoOffset::IO_OFFSET_LCDC_FBFORMAT:
            logger->debug("LCDC_FBFORMAT write32 = {:08X}", data);

            HW_DMACPLUS_LCDC_FBFORMAT = data;
            break;
        case IoOffset::IO_OFFSET_LCDC_FBWIDTH:
            logger->debug("LCDC_FBWIDTH write32 = {:08X}", data);

            HW_DMACPLUS_LCDC_FBWIDTH = data;
            break;
        case IoOffset::IO_OFFSET_LCDC_FBSTRIDE:
            logger->debug("LCDC_FBSTRIDE write32 = {:08X}", data);

            HW_DMACPLUS_LCDC_FBSTRIDE = data;
            break;
        case IoOffset::IO_OFFSET_LCDC_FBCTRL:
            logger->debug("LCDC_FBCTRL write32 = {:08X}", data);

            HW_DMACPLUS_LCDC_FBCTRL.raw = data;
            break;
        default:
            logger->error("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            exit(1);
    }
}

void initialize(const Configuration config) {
    logger = spdlog::stdout_color_st("DMACplus");

    ctx = {};

    dmacplus_hacks = config.dmacplus_hacks;
}

void soft_reset() {
    for (auto& chan : channels) {
        chan = {};
    }

    HW_DMACPLUS_TC_RAWISTAT = 0;
    HW_DMACPLUS_ERROR_RAWISTAT = 0;

    update_interrupts();
}

void hard_reset() {
    const bus::PageDescriptor page_desc {
        // To my knowledge, DMACplus I/O is never not read/written using 32-bit accesses
        .read32_func  = read,
        .write32_func = write,
    };

    kanacore::get_sc_bus_ptr()->map(DMACPLUS_ADDR, DMACPLUS_SIZE, page_desc);

    soft_reset();
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

    if (!HW_DMACPLUS_LCDC_FBCTRL.enable || (!dmacplus_hacks && (HW_DMACPLUS_LCDC_FBSTRIDE == 0))) {
        framebuffer.fill(0);
        return;
    }

    assert(HW_DMACPLUS_LCDC_FBFORMAT == 0);

    if (!dmacplus_hacks) {
        assert(HW_DMACPLUS_LCDC_FBWIDTH == 480);
    }

    const u32 addr   = dmacplus_hacks ? ge::rasterizer::get_fb_addr()  : HW_DMACPLUS_LCDC_FBADDR;
    const u32 stride = dmacplus_hacks ? ge::rasterizer::get_fb_width() : HW_DMACPLUS_LCDC_FBSTRIDE;

    for (u32 y = 0; y < 272; y++) {
        for (u32 x = 0; x < 480; x++) {
            framebuffer[y * 480 + x] = bus->read<u32>(addr + sizeof(u32) * (y * stride + x));
        }
    }
}

u8* get_framebuffer_ptr() {
    return (u8*)framebuffer.data();
}

};
