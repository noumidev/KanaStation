/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/scheduler.cpp - Event scheduler */

#include <core/scheduler.hpp>

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <queue>
#include <vector>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <core/kanacore.hpp>
#include <core/hw/allegrex/allegrex.hpp>
#include <core/hw/allegrex/interpreter.hpp>

namespace kanacore::scheduler {

using namespace common;

static std::shared_ptr<spdlog::logger> logger;

constexpr i64 MAX_CYCLES = 512;

struct Event {
    Callback callback;

    int arg;
    i64 timestamp;

    bool operator>(const Event &other) const {
        return timestamp > other.timestamp;
    }
};

typedef std::priority_queue<Event, std::vector<Event>, std::greater<Event>> EventQueue;

static EventQueue event_queue;

static i64 global_timestamp;

static void set_cpu_cycles_and_step(const i64 cycles) {
    hw::allegrex::Allegrex* sc = kanacore::get_sc_ptr();

    *sc->get_cycles() = cycles;
    
    hw::allegrex::interpreter::run(sc);
}

void initialize() {
    logger = spdlog::stdout_color_st("Scheduler");
}

void soft_reset() {
    hard_reset();
}

void hard_reset() {
    // Clear event queue
    EventQueue temp;

    event_queue.swap(temp);

    global_timestamp = 0;
}

void shutdown() {

}

void schedule_event(const char *name, Callback callback, const int arg, const i64 cycles) {
    logger->debug("Scheduling event {} with arg: {} in {} cycles", name, arg, cycles);

    event_queue.emplace(
        Event{
            callback,
            arg,
            global_timestamp + cycles
        }
    );
}

bool run() {
    i64 new_timestamp;

    if (event_queue.empty()) {
        new_timestamp = global_timestamp + MAX_CYCLES;
    } else {
        new_timestamp = event_queue.top().timestamp;
    }

    set_cpu_cycles_and_step(new_timestamp - global_timestamp);

    // Process all events with an expired timestamp
    while (!event_queue.empty() && (event_queue.top().timestamp <= new_timestamp)) {
        const Callback callback = event_queue.top().callback;

        const int arg = event_queue.top().arg;
        const i64 timestamp = event_queue.top().timestamp;

        event_queue.pop();

        assert(timestamp > global_timestamp);
        
        global_timestamp = timestamp;

        callback(arg);
    }

    return true;
}

}
