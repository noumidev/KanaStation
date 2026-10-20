/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/dmac.cpp - ARM PrimeCell PL080 DMA controllers */

#include <core/hw/dmac.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <core/kanacore.hpp>
#include <core/scheduler.hpp>
#include <core/hw/bus.hpp>
#include <core/hw/intc.hpp>

namespace kanacore::hw::dmac {

using namespace common;

constexpr u64 NUM_DMACS = 2;
constexpr u64 NUM_CHANNELS = 8;

constexpr u32 DMAC_ADDRS[] = { 0x1C900000, 0x1CA00000 };
constexpr u64 DMAC_SIZE = 0x1000;

constexpr int DMAC_INTERRUPT = 22;

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
    IO_OFFSET_CONFIG         = 0x030,
    IO_OFFSET_SYNC           = 0x034,

    IO_OFFSET_CHAN_START = 0x100,
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

enum Peripheral {
    PERIPHERAL_MS        = 1,
    PERIPHERAL_AUDIO_OUT = 5,
    PERIPHERAL_AUDIO_SRC = 6,
};
 
enum FlowControl {
    FLOW_CONTROL_MEM_TO_MEM               = 0,
    FLOW_CONTROL_MEM_TO_PERIPHERAL        = 1,
    FLOW_CONTROL_PERIPHERAL_TO_MEM        = 2,
    FLOW_CONTROL_PERIPHERAL_TO_PERIPHERAL = 3,
};

enum RequestLine {
    REQUEST_LINE_MS,
    REQUEST_LINE_AUDIO_OUT,
    REQUEST_LINE_AUDIO_SRC,
    NUM_LINES,
};

constexpr bool REQUEST_ALWAYS = true;

struct Request {
    bool asserted;

    // Currently bound DMAC/channel
    int dmac = -1;
    int chan = -1;

    bool is_bound() const {
        return dmac != -1;
    }
};

static inline int line_from_peripheral(const u32 peripheral) {
    switch (peripheral) {
        case Peripheral::PERIPHERAL_MS:
            return RequestLine::REQUEST_LINE_MS;
        case Peripheral::PERIPHERAL_AUDIO_OUT:
            return RequestLine::REQUEST_LINE_AUDIO_OUT;
        case Peripheral::PERIPHERAL_AUDIO_SRC:
            return RequestLine::REQUEST_LINE_AUDIO_SRC;
        default:
            return -1;
    }
}

static std::array<Request, NUM_LINES> request_lines;

class Dmac {
private:
    int idx;
    std::shared_ptr<spdlog::logger> logger;

    void (*event_callback)(int) = nullptr;
    u32 event_id = scheduler::NO_EVENT_ID;

    u32 raw_tc_interrupt_status;
    u32 raw_error_interrupt_status;
    u32 sync;

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
 
        const bool* source_request = &REQUEST_ALWAYS;
        const bool* destination_request = &REQUEST_ALWAYS;
 
        bool end_pending = false;
        bool running = false;
    } channels[NUM_CHANNELS];

    u32 get_masked_status(const u32 raw, const bool is_terminal_count) const {
        u32 unmasked_channels = 0;
 
        for (u32 i = 0; i < NUM_CHANNELS; i++) {
            const auto config = channels[i].configuration;
 
            if (is_terminal_count ? config.interrupt_mask : config.error_mask) {
                unmasked_channels |= 1 << i;
            }
        }
 
        return raw & unmasked_channels;
    }

    u32 get_interrupt_status() const {
        return get_masked_status(raw_tc_interrupt_status, true) | get_masked_status(raw_error_interrupt_status, false);
    }

    u32 get_enabled_channels() const {
        u32 enabled = 0;
 
        for (u32 i = 0; i < NUM_CHANNELS; i++) {
            enabled |= (u32)channels[i].configuration.channel_enable << i;
        }
 
        return enabled;
    }
 
    void update_interrupts() const {
        if (get_interrupt_status() != 0) {
            intc::assert_sc_interrupt(DMAC_INTERRUPT + idx);
        } else {
            intc::clear_sc_interrupt(DMAC_INTERRUPT + idx);
        }
    }

    const bool* bind_peripheral(const int chan_idx, const u32 peripheral) {
        const int line_idx = line_from_peripheral(peripheral);
 
        if (line_idx == -1) {
            logger->error("Unimplemented peripheral request {}", peripheral);
            exit(1);
        }
 
        Request& line = request_lines[line_idx];
 
        if (line.is_bound() && ((line.dmac != idx) || (line.chan != chan_idx))) {
            logger->warn("Peripheral {} was bound to DMAC{} CHAN{}, rebinding", peripheral, line.dmac, line.chan);
        }
 
        line.dmac = idx;
        line.chan = chan_idx;
 
        return &line.asserted;
    }

    void unbind_requests(const int chan_idx) {
        for (auto& line : request_lines) {
            if ((line.dmac == idx) && (line.chan == chan_idx)) {
                line.dmac = -1;
                line.chan = -1;
            }
        }
    }

    void bind_requests(const int chan_idx) {
        auto& chan = channels[chan_idx];
 
        // Drop whatever the channel was bound to before
        unbind_requests(chan_idx);
 
        const u32 flow_control = chan.configuration.flow_control;
 
        switch (flow_control) {
            case FLOW_CONTROL_MEM_TO_MEM:
                chan.source_request = &REQUEST_ALWAYS;
                chan.destination_request = &REQUEST_ALWAYS;
                break;
            case FLOW_CONTROL_MEM_TO_PERIPHERAL:
                chan.source_request = &REQUEST_ALWAYS;
                chan.destination_request = bind_peripheral(chan_idx, chan.configuration.destination_peripheral);
                break;
            case FLOW_CONTROL_PERIPHERAL_TO_MEM:
                chan.source_request = bind_peripheral(chan_idx, chan.configuration.source_peripheral);
                chan.destination_request = &REQUEST_ALWAYS;
                break;
            case FLOW_CONTROL_PERIPHERAL_TO_PERIPHERAL:
                chan.source_request = bind_peripheral(chan_idx, chan.configuration.source_peripheral);
                chan.destination_request = bind_peripheral(chan_idx, chan.configuration.destination_peripheral);
                break;
            default:
                logger->error("Unimplemented flow control {}", flow_control);
                exit(1);
        }
    }

    u32 read_channel(const u32 chan_idx, const u32 offset) {
        const auto& chan = channels[chan_idx];
 
        switch (offset) {
            case ChanOffset::CHAN_OFFSET_SRCADDR:
                logger->debug("CHAN{}_SRCADDR read32", chan_idx);
                return chan.source_addr;
            case ChanOffset::CHAN_OFFSET_DSTADDR:
                logger->debug("CHAN{}_DSTADDR read32", chan_idx);
                return chan.destination_addr;
            case ChanOffset::CHAN_OFFSET_LINKADDR:
                logger->debug("CHAN{}_LINKADDR read32", chan_idx);
                return chan.link_addr;
            case ChanOffset::CHAN_OFFSET_CONTROL:
                logger->debug("CHAN{}_CONTROL read32", chan_idx);
                return chan.control.raw;
            case ChanOffset::CHAN_OFFSET_CONFIG:
                logger->debug("CHAN{}_CONFIG read32", chan_idx);
                return chan.configuration.raw;
            default:
                logger->error("Unmapped CHAN{} read32 @ {:02X}", chan_idx, offset);
                exit(1);
        }
    }

    void write_channel_config(const u32 chan_idx, const u32 data) {
        auto& chan = channels[chan_idx];
 
        const bool was_enabled = chan.configuration.channel_enable;
        const u32 active = chan.configuration.active;
 
        chan.configuration.raw = data;
        chan.configuration.active = active; // Read-only
 
        if (chan.configuration.channel_enable) {
            bind_requests(chan_idx);
            run_channel(chan_idx);
        } else if (was_enabled) {
            disable_channel(chan_idx);
        }
 
        update_interrupts();
    }
 
    void write_channel(const u32 chan_idx, const u32 offset, const u32 data) {
        auto& chan = channels[chan_idx];
 
        switch (offset) {
            case ChanOffset::CHAN_OFFSET_SRCADDR:
                logger->debug("CHAN{}_SRCADDR write32 = {:08X}", chan_idx, data);
                chan.source_addr = data;
                break;
            case ChanOffset::CHAN_OFFSET_DSTADDR:
                logger->debug("CHAN{}_DSTADDR write32 = {:08X}", chan_idx, data);
                chan.destination_addr = data;
                break;
            case ChanOffset::CHAN_OFFSET_LINKADDR:
                logger->debug("CHAN{}_LINKADDR write32 = {:08X}", chan_idx, data);
                chan.link_addr = data;
                break;
            case ChanOffset::CHAN_OFFSET_CONTROL:
                logger->debug("CHAN{}_CONTROL write32 = {:08X}", chan_idx, data);
                chan.control.raw = data;
                break;
            case ChanOffset::CHAN_OFFSET_CONFIG:
                logger->debug("CHAN{}_CONFIG write32 = {:08X}", chan_idx, data);
                write_channel_config(chan_idx, data);
                break;
            default:
                logger->error("Unmapped CHAN{} write32 @ {:02X} = {:08X}", chan_idx, offset, data);
                exit(1);
        }
    }

    void disable_channel(const u32 chan_idx) {
        auto& chan = channels[chan_idx];
 
        chan.end_pending = false;
        chan.configuration.active = 0;
 
        unbind_requests(chan_idx);
    }

public:
    void initialize(const int dmac_idx, void (*callback)(int)) {
        idx = dmac_idx;
        event_callback = callback;

        logger = spdlog::stdout_color_st("DMAC" + std::to_string(idx));
    }

    void reset() {
        for (u32 i = 0; i < NUM_CHANNELS; i++) {
            unbind_requests(i);
        }

        for (auto& chan : channels) {
            // This invalidates in-flight events, too
            chan = {};
        }

        configuration = {};

        raw_tc_interrupt_status = 0;
        raw_error_interrupt_status = 0;
        sync = 0;

        update_interrupts();
    }

    u32 read(const u32 addr) {
        const u32 offset = addr & (DMAC_SIZE - 1);
 
        if ((offset >= IoOffset::IO_OFFSET_CHAN_START) && (offset < IoOffset::IO_OFFSET_CHAN_END)) {
            return read_channel(
                (offset - IoOffset::IO_OFFSET_CHAN_START) / IoOffset::IO_OFFSET_CHAN_SIZE,
                (offset - IoOffset::IO_OFFSET_CHAN_START) % IoOffset::IO_OFFSET_CHAN_SIZE
            );
        }
 
        switch (offset) {
            case IoOffset::IO_OFFSET_INTRSTAT:
                logger->debug("INTRSTAT read32");
                return get_interrupt_status();
            case IoOffset::IO_OFFSET_TC_INTRSTAT:
                logger->debug("TC_INTRSTAT read32");
                return get_masked_status(raw_tc_interrupt_status, true);
            case IoOffset::IO_OFFSET_ERROR_INTRSTAT:
                logger->debug("ERROR_INTRSTAT read32");
                return get_masked_status(raw_error_interrupt_status, false);
            case IoOffset::IO_OFFSET_TC_RAWISTAT:
                logger->debug("TC_RAWISTAT read32");
                return raw_tc_interrupt_status;
            case IoOffset::IO_OFFSET_ERROR_RAWISTAT:
                logger->debug("ERROR_RAWISTAT read32");
                return raw_error_interrupt_status;
            case IoOffset::IO_OFFSET_CONFIG:
                logger->debug("CONFIG read32");
                return configuration.raw;
            case IoOffset::IO_OFFSET_SYNC:
                logger->debug("SYNC read32");
                return sync;
            default:
                logger->error("Unmapped read32 @ {:08X}", addr);
                exit(1);
        }
    }
 
    void write(const u32 addr, const u32 data) {
        const u32 offset = addr & (DMAC_SIZE - 1);
 
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
 
                raw_tc_interrupt_status &= ~data;
                update_interrupts();
                break;
            case IoOffset::IO_OFFSET_ERROR_INTRCLR:
                logger->debug("ERROR_INTRCLR write32 = {:08X}", data);
 
                raw_error_interrupt_status &= ~data;
                update_interrupts();
                break;
            case IoOffset::IO_OFFSET_CONFIG: {
                logger->debug("CONFIG write32 = {:08X}", data);
 
                const bool was_enabled = configuration.dmac_enable;
 
                configuration.raw = data;
 
                if (!was_enabled && configuration.dmac_enable) {
                    // Channels that were waiting for the DMAC to be enabled can start now
                    for (u32 i = 0; i < NUM_CHANNELS; i++) {
                        run_channel(i);
                    }
                }
                break;
            }
            case IoOffset::IO_OFFSET_SYNC:
                logger->debug("SYNC write32 = {:08X}", data);
 
                sync = data;
                break;
            default:
                logger->error("Unmapped write32 @ {:08X} = {:08X}", addr, data);
                exit(1);
        }
    }

    void run_channel(const int chan_idx) {
        auto& chan = channels[chan_idx];
 
        if (!configuration.dmac_enable || !chan.configuration.channel_enable || chan.configuration.halt) {
            return;
        }
 
        if (chan.end_pending || chan.running) {
            return;
        }
 
        if ((chan.control.source_width != 2) || (chan.control.destination_width != 2)) {
            logger->error(
                "CHAN{} unimplemented transfer width (source: {}, destination: {})",
                chan_idx,
                (u32)chan.control.source_width,
                (u32)chan.control.destination_width
            );

            exit(1);
        }
 
        const u32 total = chan.control.transfer_length;
 
        if (total == 0) {
            logger->warn("CHAN{} started with a transfer length of zero", chan_idx);
        }
 
        logger->debug(
            "CHAN{} transfer (source address: {:08X}, destination address: {:08X}, length: {:03X})",
            chan_idx,
            chan.source_addr,
            chan.destination_addr,
            total
        );
 
        bus::Bus* bus = kanacore::get_sc_bus_ptr();
 
        const u32 source_step = chan.control.source_increment ? 4 : 0;
        const u32 destination_step = chan.control.destination_increment ? 4 : 0;
 
        u32 remaining = total;
 
        chan.running = true;
 
        // Single 32-bit beat per iteration. Later on, AHB bursts could be emulated
        while ((remaining > 0) && *chan.source_request && *chan.destination_request) {
            bus->write<u32>(chan.destination_addr & ADDR_MASK, bus->read<u32>(chan.source_addr & ADDR_MASK));
 
            chan.source_addr += source_step;
            chan.destination_addr += destination_step;
 
            remaining--;
        }
 
        chan.running = false;
        chan.control.transfer_length = remaining;
 
        if (remaining != 0) {
            // Request line went low before the end of transfer
            chan.configuration.active = 0;
            return;
        }
 
        if (event_id == scheduler::NO_EVENT_ID) {
            event_id = scheduler::register_event((idx == 0) ? "DMAC0" : "DMAC1");
        }
 
        chan.end_pending = true;
 
        // There are no proper DMA FIFOs, so raising this bit while the completion event is
        // in flight is a good-enough approximation
        chan.configuration.active = 1;
 
        scheduler::schedule_event(event_id, event_callback, chan_idx, std::max<u64>(1, CYCLES_PER_BEAT * total));
    }

    void end_transfer(const int chan_idx) {
        auto& chan = channels[chan_idx];
 
        // The channel was disabled or reset after this event was scheduled
        if (!chan.end_pending) {
            return;
        }
 
        chan.end_pending = false;
        chan.configuration.active = 0;
 
        if (chan.control.interrupt_enable) {
            raw_tc_interrupt_status |= 1 << chan_idx;
            update_interrupts();
        }
 
        const u32 lli_addr = chan.link_addr & ~3;
 
        if (lli_addr == 0) {
            // End of the chain, the channel disables itself
            chan.configuration.channel_enable = 0;
            unbind_requests(chan_idx);
            return;
        }
 
        // Scatter/gather
        bus::Bus* bus = kanacore::get_sc_bus_ptr();
 
        const u32 masked_lli = lli_addr & ADDR_MASK;
 
        chan.source_addr = bus->read<u32>(masked_lli + 0x0);
        chan.destination_addr = bus->read<u32>(masked_lli + 0x4);
        chan.link_addr = bus->read<u32>(masked_lli + 0x8);
        chan.control.raw = bus->read<u32>(masked_lli + 0xC);
 
        run_channel(chan_idx);
    }
};

static std::array<Dmac, NUM_DMACS> dmacs;

template<int dmac_num>
static u32 read(const u32 addr) {
    static_assert(dmac_num < NUM_DMACS);
    
    return dmacs[dmac_num].read(addr);
}

template<int dmac_num>
static void write(const u32 addr, const u32 data) {
    static_assert(dmac_num < NUM_DMACS);
    
    dmacs[dmac_num].write(addr, data);
}

template<int dmac_num>
static void end_transfer_event(const int chan_idx) {
    static_assert(dmac_num < NUM_DMACS);

    dmacs[dmac_num].end_transfer(chan_idx);
}

template<int dmac_num>
static void map() {
    static_assert(dmac_num < NUM_DMACS);

    // To my knowledge, DMAC I/O is never not read/written using 32-bit accesses
    const bus::PageDescriptor page_desc {
        .read32_func  = read<dmac_num>,
        .write32_func = write<dmac_num>,
    };

    kanacore::get_sc_bus_ptr()->map(DMAC_ADDRS[dmac_num], DMAC_SIZE, page_desc);
}

void initialize() {
    request_lines.fill(Request{});
    
    dmacs[0].initialize(0, end_transfer_event<0>);
    dmacs[1].initialize(1, end_transfer_event<1>);
}

void soft_reset() {
    for (auto& dmac : dmacs) {
        dmac.reset();
    }
}

void hard_reset() {
    map<0>();
    map<1>();

    soft_reset();
}

void shutdown() {

}

static void assert_request(const int line_idx) {
    Request& line = request_lines[line_idx];
 
    const bool was_asserted = line.asserted;
 
    line.asserted = true;
 
    if (!was_asserted && line.is_bound()) {
        dmacs[line.dmac].run_channel(line.chan);
    }
}
 
static void clear_request(const int line_idx) {
    request_lines[line_idx].asserted = false;
}

void assert_audio_dma_request(const int chan_id) {
    assert((chan_id >= 0) && (chan_id < 2));
 
    assert_request(RequestLine::REQUEST_LINE_AUDIO_OUT + chan_id);
}

void clear_audio_dma_request(const int chan_id) {
    assert((chan_id >= 0) && (chan_id < 2));
 
    clear_request(RequestLine::REQUEST_LINE_AUDIO_OUT + chan_id);
}

void assert_ms_dma_request() {
    assert_request(RequestLine::REQUEST_LINE_MS);
}

void clear_ms_dma_request() {
    clear_request(RequestLine::REQUEST_LINE_MS);
}

}
