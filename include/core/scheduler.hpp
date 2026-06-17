/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/scheduler.hpp - Event scheduler */

#pragma once

#include <common/types.hpp>

namespace kanacore::scheduler {

typedef void (*Callback)(const int);

constexpr common::i64 BUS_CLOCKRATE       = 166666666;
constexpr common::i64 SCHEDULER_CLOCKRATE = 2 * BUS_CLOCKRATE + 1;

constexpr common::i64 ONE_MICROSECOND = SCHEDULER_CLOCKRATE / 1000 / 1000;

constexpr common::i64 SPI_CLOCKRATE = 2 * ONE_MICROSECOND;

enum EventType {
    KIRK_1ST_PHASE,
    SPI_TX,
    SYSCON_TX,
    I2C,
    NAND_DMA,
    SYSTIME,
    NUM_EVENT_TYPES,
};

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

void schedule_event(const EventType type, Callback callback, const int arg, const common::i64 cycles);
void cancel_event(const EventType type);

bool run();

}
