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

void initialize();
void soft_reset();
void hard_reset();
void shutdown();

template<common::i64 clockrate>
common::i64 to_scheduler_cycles(const common::i64 cycles) {
    return (SCHEDULER_CLOCKRATE * cycles) / clockrate;
}

void schedule_event(const char* name, Callback callback, const int arg, const common::i64 cycles);

bool run();

}
