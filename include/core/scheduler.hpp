/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/scheduler.hpp - Event scheduler */

#pragma once

#include <common/types.hpp>

namespace kanacore::scheduler {

typedef void (*Callback)(const int);

constexpr common::i64 SCHEDULER_CLOCKRATE = 333000000;
constexpr common::i64 BUS_CLOCKRATE       = SCHEDULER_CLOCKRATE / 2;

constexpr common::i64 ONE_MICROSECOND = SCHEDULER_CLOCKRATE / 1000 / 1000;

constexpr common::i64 SPI_CLOCKRATE   = 2 * ONE_MICROSECOND;
constexpr common::i64 PIXEL_CLOCKRATE = 9000000;

constexpr common::u32 NO_EVENT_ID = -1;

void initialize();
void soft_reset();
void hard_reset();
void shutdown();

inline common::i64 to_scheduler_cycles(common::i64 clockrate, const common::i64 cycles) {
    return (SCHEDULER_CLOCKRATE * cycles) / clockrate;
}

inline common::i64 from_microseconds(const common::i64 ms) {
    return ms * ONE_MICROSECOND;
}

common::u32 register_event(const char* name);

void schedule_event(
    const common::u32 id,
    Callback callback,
    const int arg,
    const common::i64 cycles,
    const bool auto_cancel = false
);

void cancel_event(const common::u32 id);

bool run();

}
