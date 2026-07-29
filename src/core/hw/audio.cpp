/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/audio.cpp - Audio interface */

#include <core/hw/audio.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <queue>
#include <vector>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <core/kanacore.hpp>
#include <core/scheduler.hpp>
#include <core/hw/bus.hpp>
#include <core/hw/dmac.hpp>
#include <core/hw/intc.hpp>

namespace kanacore::hw::audio {

using namespace common;

constexpr u64 AUDIO_ADDR = 0x1E000000;
constexpr u64 AUDIO_SIZE = 0x1000;

// Audio is sent to the FIFOs in chunks of 0x40 stereo samples
constexpr u64 FIFO_SIZE = 0x40;

enum IoAddress {
    IO_ADDRESS_ENABLE   = AUDIO_ADDR + 0x000,
    IO_ADDRESS_CHANEN   = AUDIO_ADDR + 0x004,
    IO_ADDRESS_INTRMASK = AUDIO_ADDR + 0x008,
    IO_ADDRESS_CHANSTAT = AUDIO_ADDR + 0x00C,
    IO_ADDRESS_FIFOCLR  = AUDIO_ADDR + 0x010,
    IO_ADDRESS_INTRSTAT = AUDIO_ADDR + 0x01C,
    IO_ADDRESS_INTRCLR  = AUDIO_ADDR + 0x024,
    IO_ADDRESS_FIFOSTAT = AUDIO_ADDR + 0x028,
    IO_ADDRESS_OUTFREQ  = AUDIO_ADDR + 0x038,
    IO_ADDRESS_FREQCTRL = AUDIO_ADDR + 0x040,
    IO_ADDRESS_SRCVOL   = AUDIO_ADDR + 0x050,
    IO_ADDRESS_OUTDATA  = AUDIO_ADDR + 0x060,
};

#define HW_AUDIO_ENABLE   ctx.enable
#define HW_AUDIO_CHANEN   ctx.channel_enable
#define HW_AUDIO_INTRMASK ctx.interrupt_mask
#define HW_AUDIO_INTRSTAT ctx.interrupt_status
#define HW_AUDIO_FIFOSTAT ctx.fifo_status
#define HW_AUDIO_OUTFREQ  ctx.out_frequency
#define HW_AUDIO_SRCVOL   ctx.src_volume

enum AudioChannel {
    AUDIO_CHANNEL_OUT = 0,
    AUDIO_CHANNEL_SRC = 1,
    AUDIO_CHANNEL_IN  = 2,
};

static struct {
    bool enable;
    u32 channel_enable;
    u32 interrupt_mask;
    u32 interrupt_status;
    
    union {
        u32 raw;

        struct {
            u32              : 4;
            u32 out_not_full : 1;
            u32 src_not_full : 1;
            u32              : 26;
        };
    } fifo_status;

    u16 out_frequency;
    u16 src_volume;

    int out_stall_count;
} ctx;

struct FifoSample {
    u32 stereo_sample;
    bool is_stalled;
};

static std::shared_ptr<spdlog::logger> logger;

static std::queue<FifoSample> out_fifo;
static std::queue<FifoSample> src_fifo;

static void check_pending_interrupts() {
    if ((HW_AUDIO_INTRSTAT) != 0) {
        intc::assert_sc_interrupt(10);
    } else {
        intc::clear_sc_interrupt(10);
    }
}

static void assert_interrupt(const int chan_id) {
    HW_AUDIO_INTRSTAT |= 1 << chan_id;

    check_pending_interrupts();
}

static void update_fifo_status() {
    HW_AUDIO_FIFOSTAT.out_not_full = out_fifo.size() < FIFO_SIZE;
    HW_AUDIO_FIFOSTAT.src_not_full = src_fifo.size() < FIFO_SIZE;
}

static void clear_fifos() {
    while (!out_fifo.empty()) {
        out_fifo.pop();
    }

    while (!src_fifo.empty()) {
        src_fifo.pop();
    }

    update_fifo_status();
}

static void check_audio_dma_request() {
    if ((ctx.out_stall_count == 0) && (out_fifo.size() <= (FIFO_SIZE - 4))) {
        dmac::assert_audio_dma_request();
    } else {
        dmac::clear_audio_dma_request();
    }
}

static void disable_out_channel() {
    ctx.out_stall_count = 24;

    scheduler::cancel_event(scheduler::EventType::AUDIO);

    check_audio_dma_request();

    // Does this invalidate the FIFO..?
}

static void drain_out_fifo(const int) {
    assert(!out_fifo.empty());

    // TODO: push audio samples to out

    out_fifo.pop();

    if (out_fifo.size() == (FIFO_SIZE / 2)) {
        // I assume the FIFO draining to a certain capacity triggers the interrupt.
        // Will need to write some tests...
        assert_interrupt(AUDIO_CHANNEL_OUT);
    }

    update_fifo_status();

    if (!out_fifo.empty()) {
        scheduler::schedule_event(
            scheduler::EventType::AUDIO,
            drain_out_fifo,
            0,
            scheduler::to_scheduler_cycles(44100, 1),
            true
        );
    }

    check_audio_dma_request();
}

static void write_out_fifo(const u32 data) {
    assert(out_fifo.size() < FIFO_SIZE);

    out_fifo.push({data, ctx.out_stall_count > 0});

    if (ctx.out_stall_count > 0) {
        ctx.out_stall_count--;
    }

    if (out_fifo.size() == 1) {
        // Kick off draining the FIFO
        scheduler::schedule_event(
            scheduler::EventType::AUDIO,
            drain_out_fifo,
            0,
            scheduler::to_scheduler_cycles(44100, 1),
            true
        );
    }

    update_fifo_status();
    check_audio_dma_request();
}

static u32 read(const u32 addr) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_ENABLE:
            logger->debug("ENABLE read32");
            return HW_AUDIO_ENABLE;
        case IoAddress::IO_ADDRESS_CHANSTAT:
            logger->debug("CHANSTAT read32");
            return HW_AUDIO_CHANEN;
        case IoAddress::IO_ADDRESS_INTRSTAT:
            logger->debug("INTRSTAT read32");
            return HW_AUDIO_INTRSTAT;
        case IoAddress::IO_ADDRESS_FIFOSTAT:
            // logger->debug("FIFOSTAT read32");
            return HW_AUDIO_FIFOSTAT.raw;
        case IoAddress::IO_ADDRESS_FREQCTRL:
            logger->debug("FREQCTRL read32");
            return 0;
        case IoAddress::IO_ADDRESS_SRCVOL:
            logger->debug("SRCVOL read32");
            return HW_AUDIO_SRCVOL;
        default:
            logger->error("Unmapped read32 @ {:08X}", addr);
            exit(1);
    }
}

static void write(const u32 addr, const u32 data) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_ENABLE:
            logger->debug("ENABLE write32 = {:08X}", data);

            HW_AUDIO_ENABLE = (data & 1) != 0;
            break;
        case IoAddress::IO_ADDRESS_CHANEN:
            logger->debug("CHANEN write32 = {:08X}", data);

            HW_AUDIO_CHANEN = data & 7;

            if ((HW_AUDIO_CHANEN & 1) == 0) {
                disable_out_channel();
            }
            break;
        case IoAddress::IO_ADDRESS_INTRMASK:
            logger->debug("INTRMASK write32 = {:08X}", data);

            HW_AUDIO_INTRMASK = data;

            check_pending_interrupts();
            break;
        case IoAddress::IO_ADDRESS_INTRCLR:
            logger->debug("INTRCLR write32 = {:08X}", data);

            HW_AUDIO_INTRSTAT &= data;

            check_pending_interrupts();
            break;
        case IoAddress::IO_ADDRESS_OUTFREQ:
            logger->debug("OUTFREQ write32 = {:08X}", data);

            HW_AUDIO_OUTFREQ = data & 0x1FF;
            break;
        case IoAddress::IO_ADDRESS_FREQCTRL:
            logger->debug("FREQCTRL write32 = {:08X}", data);
            break;
        case IoAddress::IO_ADDRESS_SRCVOL:
            logger->debug("SRCVOL write32 = {:08X}", data);

            HW_AUDIO_SRCVOL = data;
            break;
        case IoAddress::IO_ADDRESS_OUTDATA:
            logger->debug("OUTDATA write32 = {:08X}", data);
            write_out_fifo(data);
            break;
        case AUDIO_ADDR + 0x010:
        case AUDIO_ADDR + 0x014:
        case AUDIO_ADDR + 0x018:
        case AUDIO_ADDR + 0x020:
        case AUDIO_ADDR + 0x02C:
            logger->warn("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            break;
        default:
            logger->error("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            exit(1);
    }
}

void initialize() {
    logger = spdlog::stdout_color_st("Audio");

    std::memset(&ctx, 0, sizeof(ctx));
}

void soft_reset() {
    clear_fifos();
}

void hard_reset() {
    const bus::PageDescriptor page_desc {
        // To my knowledge, audio I/F I/O is never not read/written using 32-bit accesses
        .read32_func  = read,
        .write32_func = write,
    };

    kanacore::get_sc_bus_ptr()->map(AUDIO_ADDR, AUDIO_SIZE, page_desc);

    soft_reset();
}

void shutdown() {

}

};
