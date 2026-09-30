/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/audio.cpp - Audio interface */

#include <core/hw/audio.hpp>

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstdio>
#include <cstdint>
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

constexpr u32 FREQ_TABLE[] = { 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000 };
constexpr u32 MIXER_RATE = 48000;

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
    IO_ADDRESS_SRCFREQ  = AUDIO_ADDR + 0x044,
    IO_ADDRESS_SRCVOL   = AUDIO_ADDR + 0x050,
    IO_ADDRESS_OUTDATA  = AUDIO_ADDR + 0x060,
    IO_ADDRESS_SRCDATA  = AUDIO_ADDR + 0x070,
};

#define HW_AUDIO_ENABLE   ctx.enable
#define HW_AUDIO_CHANEN   ctx.channel_enable
#define HW_AUDIO_INTRMASK ctx.interrupt_mask
#define HW_AUDIO_INTRSTAT ctx.interrupt_status
#define HW_AUDIO_FIFOSTAT ctx.fifo_status
#define HW_AUDIO_SRCVOL   ctx.src_volume

enum AudioChannel {
    AUDIO_CHANNEL_OUT = 0,
    AUDIO_CHANNEL_SRC = 1,
    AUDIO_CHANNEL_IN  = 2,
    NUM_AUDIO_CHANNELS = 3,
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

    u16 src_volume;

    u32 mixer_event_id;
    i64 mixer_curr_timestamp;
    u64 mixer_ticks;
} ctx;

struct StereoSample {
    i16 left, right;
};

struct FifoSample {
    StereoSample sample;
    bool is_stalled;
};

static struct Channel {
    u32 event_id;
    u64 ticks;
    i64 curr_timestamp;
    i64 period;

    std::queue<FifoSample> fifo;

    u32 freq_idx;
    int stall_count;

    bool active;

    StereoSample curr_sample, prev_sample;
} channels[AudioChannel::NUM_AUDIO_CHANNELS];

static std::shared_ptr<spdlog::logger> logger;

static std::queue<i16> samples;

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

static void latch_sample(Channel& chan, const StereoSample sample) {
    chan.prev_sample = chan.curr_sample;
    chan.curr_sample = sample;

    chan.curr_timestamp = scheduler::get_global_timestamp();
}

static void update_fifo_status() {
    HW_AUDIO_FIFOSTAT.out_not_full = channels[AudioChannel::AUDIO_CHANNEL_OUT].fifo.size() < FIFO_SIZE;
    HW_AUDIO_FIFOSTAT.src_not_full = channels[AudioChannel::AUDIO_CHANNEL_SRC].fifo.size() < FIFO_SIZE;
}

static void clear_fifos() {
    auto& out_fifo = channels[AudioChannel::AUDIO_CHANNEL_OUT].fifo;
    auto& src_fifo = channels[AudioChannel::AUDIO_CHANNEL_OUT].fifo;

    while (!out_fifo.empty()) {
        out_fifo.pop();
    }

    while (!src_fifo.empty()) {
        src_fifo.pop();
    }

    update_fifo_status();
}

static void check_audio_dma_request(const int chan_id) {
    assert(chan_id < AudioChannel::NUM_AUDIO_CHANNELS);

    auto& chan = channels[chan_id];

    if ((chan.stall_count == 0) && (chan.fifo.size() <= (FIFO_SIZE - 4))) {
        dmac::assert_audio_dma_request(chan_id);
    } else {
        dmac::clear_audio_dma_request(chan_id);
    }
}

static void disable_channel(const int chan_id) {
    assert(chan_id < AudioChannel::NUM_AUDIO_CHANNELS);

    auto& chan = channels[chan_id];

    chan.stall_count = 24;

    scheduler::cancel_event(chan.event_id);

    check_audio_dma_request(chan_id);

    // Does this invalidate the FIFO..?
}

static void drain_fifo(const int);

static void schedule_drain_fifo(const int chan_id) {
    assert(chan_id < AudioChannel::NUM_AUDIO_CHANNELS);

    auto& chan = channels[chan_id];

    const u32 freq  = FREQ_TABLE[chan.freq_idx];
    const i64 delay = scheduler::to_scheduler_cycles(freq, chan.ticks + 1) - scheduler::to_scheduler_cycles(freq, chan.ticks);

    scheduler::schedule_event(chan.event_id, drain_fifo, chan_id, delay, true);
}

static void drain_fifo(const int chan_id) {
    assert(chan_id < AudioChannel::NUM_AUDIO_CHANNELS);

    auto& chan = channels[chan_id];
    auto& fifo = chan.fifo;

    if (fifo.empty()) {
        latch_sample(chan, { 0, 0 });

        chan.active = false;
        return;
    }

    const FifoSample fifo_sample = fifo.front(); fifo.pop();

    if (fifo_sample.is_stalled) {
        latch_sample(chan, { 0, 0 });
    } else {
        latch_sample(chan, fifo_sample.sample);
    }

    if (fifo.size() == (FIFO_SIZE / 2)) {
        // I assume the FIFO draining to a certain capacity triggers the interrupt.
        // Will need to write some tests...
        assert_interrupt(chan_id);
    }

    update_fifo_status();
    check_audio_dma_request(chan_id);
    schedule_drain_fifo(chan_id);
}

static void write_fifo(const int chan_id, const u32 data) {
    assert(chan_id < AudioChannel::NUM_AUDIO_CHANNELS);

    auto& chan = channels[chan_id];
    auto& fifo = chan.fifo;

    if (fifo.size() >= FIFO_SIZE) {
        // Not sure what happens on hardware, but it probably just discards the new samples...
        logger->warn("Channel {} FIFO overflow", chan_id);
        return;
    }

    fifo.push({ { (i16)data, (i16)(data >> 16) }, chan.stall_count > 0 });

    if (chan.stall_count > 0) {
        chan.stall_count--;
    }

    if (!chan.active) {
        chan.active = true;
        chan.ticks  = 0;

        schedule_drain_fifo(chan_id);
    }

    update_fifo_status();
    check_audio_dma_request(chan_id);
}

static void set_frequency(const int chan_id, const u32 data) {
    assert(chan_id < AudioChannel::NUM_AUDIO_CHANNELS);

    auto& chan = channels[chan_id];

    if (std::popcount(data) != 1) {
        logger->error("Invalid frequency setting for channel {}", chan_id);
    }

    chan.freq_idx = std::countr_zero(data);
    chan.period   = scheduler::to_scheduler_cycles(FREQ_TABLE[chan.freq_idx], 1);

    if (chan.active) {
        // Reschedule FIFO drain event to respect the new sample rate...
        chan.ticks = 0;

        scheduler::cancel_event(chan.event_id);
        schedule_drain_fifo(chan_id);
    }
}

static inline i16 interpolate(const i16 a, const i16 b, const i64 t) {
    return (i16)(a + ((((i64)b - (i64)a) * t) >> 16));
}

static StereoSample sample_channel(const Channel& chan, const i64 timestamp) {
    const i64 t = std::clamp<i64>(((timestamp - chan.curr_timestamp) << 16) / chan.period, 0, 0x10000);

    return {
        interpolate(chan.prev_sample.left, chan.curr_sample.left, t),
        interpolate(chan.prev_sample.right, chan.curr_sample.right, t)
    };
}

static void mixer_exec(const int);

static void schedule_mixer() {
    ctx.mixer_ticks++;

    const i64 target_timestamp = ctx.mixer_curr_timestamp + scheduler::to_scheduler_cycles(MIXER_RATE, ctx.mixer_ticks);
    const i64 delay = std::max<i64>(target_timestamp - scheduler::get_global_timestamp(), 1);

    scheduler::schedule_event(ctx.mixer_event_id, mixer_exec, 0, delay, true);
}

static void start_mixer() {
    scheduler::cancel_event(ctx.mixer_event_id);

    ctx.mixer_curr_timestamp = scheduler::get_global_timestamp();
    ctx.mixer_ticks = 0;

    schedule_mixer();
}

static void mixer_exec(const int) {
    const i64 timestamp = ctx.mixer_curr_timestamp + scheduler::to_scheduler_cycles(MIXER_RATE, ctx.mixer_ticks);

    schedule_mixer();

    if (!HW_AUDIO_ENABLE) {
        samples.push(0);
        samples.push(0);
        return;
    }

    const StereoSample out_sample = sample_channel(channels[AudioChannel::AUDIO_CHANNEL_OUT], timestamp);
    const StereoSample src_sample = sample_channel(channels[AudioChannel::AUDIO_CHANNEL_SRC], timestamp);

    // We should figure out how to apply the SRC volume, but for now this is probably fine
    const i32 left  = (i32)out_sample.left  + (i32)src_sample.left;
    const i32 right = (i32)out_sample.right + (i32)src_sample.right;

    samples.push((i16)std::clamp(left , INT16_MIN, INT16_MAX));
    samples.push((i16)std::clamp(right, INT16_MIN, INT16_MAX));
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
                disable_channel(AudioChannel::AUDIO_CHANNEL_OUT);
            }

            if ((HW_AUDIO_CHANEN & 2) == 0) {
                disable_channel(AudioChannel::AUDIO_CHANNEL_SRC);
            }
            break;
        case IoAddress::IO_ADDRESS_INTRMASK:
            logger->debug("INTRMASK write32 = {:08X}", data);

            HW_AUDIO_INTRMASK = data;

            check_pending_interrupts();
            break;
        case IoAddress::IO_ADDRESS_FIFOCLR:
            logger->error("FIFOCLR write32 = {:08X}", data);
            break;
        case IoAddress::IO_ADDRESS_INTRCLR:
            logger->debug("INTRCLR write32 = {:08X}", data);

            HW_AUDIO_INTRSTAT &= data;

            check_pending_interrupts();
            break;
        case IoAddress::IO_ADDRESS_OUTFREQ:
            logger->debug("OUTFREQ write32 = {:08X}", data);
            set_frequency(AudioChannel::AUDIO_CHANNEL_OUT, data & 0x1FF);
            break;
        case IoAddress::IO_ADDRESS_FREQCTRL:
            logger->debug("FREQCTRL write32 = {:08X}", data);
            break;
        case IoAddress::IO_ADDRESS_SRCFREQ:
            logger->debug("SRCFREQ write32 = {:08X}", data);
            set_frequency(AudioChannel::AUDIO_CHANNEL_SRC, data & 0x1FF);
            break;
        case IoAddress::IO_ADDRESS_SRCVOL:
            logger->debug("SRCVOL write32 = {:08X}", data);

            HW_AUDIO_SRCVOL = data;
            break;
        case IoAddress::IO_ADDRESS_OUTDATA:
            logger->debug("OUTDATA write32 = {:08X}", data);
            write_fifo(AudioChannel::AUDIO_CHANNEL_OUT, data);
            break;
        case IoAddress::IO_ADDRESS_SRCDATA:
            logger->debug("SRCDATA write32 = {:08X}", data);
            write_fifo(AudioChannel::AUDIO_CHANNEL_SRC, data);
            break;
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

    channels[AudioChannel::AUDIO_CHANNEL_OUT].event_id = scheduler::register_event("OUT_AUDIO");
    channels[AudioChannel::AUDIO_CHANNEL_SRC].event_id = scheduler::register_event("SRC_AUDIO");

    ctx.mixer_event_id = scheduler::register_event("MIXER");
}

void soft_reset() {
    clear_fifos();
    start_mixer();
}

void hard_reset() {
    const bus::PageDescriptor page_desc {
        // To my knowledge, audio I/F I/O is never not read/written using 32-bit accesses
        .read32_func  = read,
        .write32_func = write,
    };

    kanacore::get_sc_bus_ptr()->map(AUDIO_ADDR, AUDIO_SIZE, page_desc);
    kanacore::get_me_bus_ptr()->map(AUDIO_ADDR, AUDIO_SIZE, page_desc);

    soft_reset();
}

void shutdown() {

}

std::vector<i16> get_samples() {
    std::vector<i16> sample_data;

    while (!samples.empty()) {
        sample_data.push_back(samples.front()); samples.pop();
    }

    return sample_data;
}

};
