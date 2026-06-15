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

    hw::allegrex::Allegrex* sc = kanacore::get_sc_ptr();

    const i64 event_timestamp = *sc->get_cycles() + cycles;

    event_queue.emplace(
        Event{
            callback,
            arg,
            event_timestamp
        }
    );

    // If the new event expires before the current event, we make it the new closest event
    if (event_timestamp < global_timestamp) {
        global_timestamp = event_timestamp;

        *sc->get_target_timestamp() = event_timestamp;
    }
}

bool run() {
    hw::allegrex::Allegrex* sc = kanacore::get_sc_ptr();

    if (event_queue.empty()) {
        global_timestamp = *sc->get_cycles() + MAX_CYCLES;
    } else {
        global_timestamp = event_queue.top().timestamp;
    }

    hw::allegrex::interpreter::run(sc, global_timestamp);

    // Process all events with an expired timestamp
    while (!event_queue.empty() && (event_queue.top().timestamp <= *sc->get_cycles())) {
        const Event event = event_queue.top();
        event_queue.pop();

        event.callback(event.arg);
    }

    return true;
}

}
