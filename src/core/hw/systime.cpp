/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/systime.cpp - System timer (48 MHz) registers */

#include <core/hw/systime.hpp>

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <core/kanacore.hpp>
#include <core/scheduler.hpp>
#include <core/hw/allegrex/allegrex.hpp>
#include <core/hw/bus.hpp>
#include <core/hw/intc.hpp>

namespace kanacore::hw::systime {

using namespace common;

constexpr u64 SYSTIME_ADDR = 0x1C600000;
constexpr u64 SYSTIME_SIZE = 0x1000;

constexpr int SYSTIME_INTERRUPT = 19;

enum IoAddress {
    IO_ADDRESS_COUNTER   = SYSTIME_ADDR + 0x000,
    IO_ADDRESS_ALARM     = SYSTIME_ADDR + 0x004,
    IO_ADDRESS_PRESCALER = SYSTIME_ADDR + 0x008,
    IO_ADDRESS_ENABLE    = SYSTIME_ADDR + 0x00C,
};

#define HW_SYSCTRL_COUNTER   ctx.counter
#define HW_SYSCTRL_ALARM     ctx.alarm
#define HW_SYSCTRL_PRESCALER ctx.prescaler
#define HW_SYSCTRL_ENABLE    ctx.enable

static std::shared_ptr<spdlog::logger> logger;

static struct {
    u32 counter;
    u32 alarm;
    u32 prescaler;
    bool enable;

    i64 start_timestamp;
} ctx;

static void alarm(const int) {
    intc::assert_interrupt(SYSTIME_INTERRUPT);
}

static u32 get_counter() {
    return HW_SYSCTRL_COUNTER + (*kanacore::get_sc_ptr()->get_cycles() - ctx.start_timestamp) / scheduler::ONE_MICROSECOND;
}

static void reschedule_alarm() {
    if (HW_SYSCTRL_ALARM > get_counter()) {
        scheduler::schedule_event(
            scheduler::EventType::SYSTIME,
            alarm,
            0,
            scheduler::from_microseconds(HW_SYSCTRL_ALARM - HW_SYSCTRL_COUNTER)
        );
    }
}

static void enable_systime(const u32 data) {
    const bool new_enable = (data & 1) != 0;

    if (!HW_SYSCTRL_ENABLE && new_enable) {
        ctx.start_timestamp = *kanacore::get_sc_ptr()->get_cycles();

        reschedule_alarm();
    } else if (HW_SYSCTRL_ENABLE && !new_enable) {
        // The kernel never turns the timer off, but it *is* possible
        scheduler::cancel_event(scheduler::EventType::SYSTIME);
    }
}

static void set_counter(const u32 data) {
    HW_SYSCTRL_COUNTER = data;

    ctx.start_timestamp = *kanacore::get_sc_ptr()->get_cycles();

    reschedule_alarm();
}

static void set_alarm(const u32 data) {
    intc::clear_interrupt(SYSTIME_INTERRUPT);

    HW_SYSCTRL_ALARM = data;

    reschedule_alarm();
}

static u32 read(const u32 addr) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_COUNTER:
            logger->debug("COUNTER read32");
            return get_counter();
        default:
            logger->error("Unmapped read32 @ {:08X}", addr);
            exit(1);
    }
}

static void write(const u32 addr, const u32 data) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_COUNTER:
            logger->debug("COUNTER write32 = {:08X}", data);
            set_counter(data);
            break;
        case IoAddress::IO_ADDRESS_ALARM:
            logger->debug("ALARM write32 = {:08X}", data);
            set_alarm(data);
            break;
        case IoAddress::IO_ADDRESS_PRESCALER:
            // The timer runs at 48 MHz, but the kernel sets a prescaler of
            // 48 and never changes it. TODO: handle this
            logger->debug("PRESCALER write32 = {:08X}", data);

            HW_SYSCTRL_PRESCALER = data;
            break;
        case IoAddress::IO_ADDRESS_ENABLE:
            logger->debug("ENABLE write32 = {:08X}", data);
            enable_systime(data);
            break;
        case SYSTIME_ADDR + 0x010:
            // Unknown (interrupt disable?)
            logger->warn("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            assert(data == 0);
            break;
        default:
            logger->error("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            exit(1);
    }
}

void initialize() {
    logger = spdlog::stdout_color_st("SysTime");

    std::memset(&ctx, 0, sizeof(ctx));
}

void soft_reset() {
    
}

void hard_reset() {
    const bus::PageDescriptor page_desc {
        // To my knowledge, SYSCTRL I/O is never not read/written using 32-bit accesses
        .read32_func  = read,
        .write32_func = write,
    };

    bus::map(SYSTIME_ADDR, SYSTIME_SIZE, page_desc);
}

void shutdown() {

}

};
