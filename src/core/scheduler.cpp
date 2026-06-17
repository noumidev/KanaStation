/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/scheduler.cpp - Event scheduler */

#include <core/scheduler.hpp>

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>
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

static constexpr const char* EVENT_TYPE_NAMES[] = {
    "KIRK 1st phase",
    "SPI TX",
    "SYSCON TX",
    "NAND DMA",
    "SysTime"
};

struct Event {
    EventType type;
    Callback callback;

    int arg;
    i64 timestamp;

    bool operator>(const Event &other) const {
        return timestamp > other.timestamp;
    }
};

typedef std::vector<Event> EventQueue;

static EventQueue event_queue;

static i64 global_timestamp;

void initialize() {
    logger = spdlog::stdout_color_st("Scheduler");
}

void soft_reset() {
    hard_reset();
}

void hard_reset() {
    event_queue.clear();

    global_timestamp = 0;
}

void shutdown() {

}

void schedule_event(const EventType type, Callback callback, const int arg, const i64 cycles) {
    hw::allegrex::Allegrex* sc = kanacore::get_sc_ptr();

    logger->debug("Scheduling event {} with arg: {} in {} cycles", EVENT_TYPE_NAMES[type], arg, cycles);

    assert(type < EventType::NUM_EVENT_TYPES);

    cancel_event(type);

    const i64 event_timestamp = *sc->get_cycles() + cycles;

    event_queue.emplace_back(
        Event{
            type,
            callback,
            arg,
            event_timestamp
        }
    );

    std::sort(event_queue.begin(), event_queue.end(),
        [](const Event& a, const Event& b) {
            return a > b; 
        }
    );

    // If the new event expires before the current event, we make it the new closest event
    if (event_timestamp < global_timestamp) {
        global_timestamp = event_timestamp;

        *sc->get_target_timestamp() = event_timestamp;
    }
}

void cancel_event(const EventType type) {
    assert(type < EventType::NUM_EVENT_TYPES);

    event_queue.erase(
        std::remove_if(event_queue.begin(), event_queue.end(),
            [type](const Event& e) {
                return e.type == type;
            }
        ),
        event_queue.end()
    );
}

bool run() {
    hw::allegrex::Allegrex* sc = kanacore::get_sc_ptr();

    if (event_queue.empty()) {
        global_timestamp = *sc->get_cycles() + MAX_CYCLES;
    } else {
        global_timestamp = event_queue.back().timestamp;
    }

    hw::allegrex::interpreter::run(sc, global_timestamp);

    // Process all events with an expired timestamp
    while (!event_queue.empty() && (event_queue.back().timestamp <= *sc->get_cycles())) {
        const Event event = event_queue.back();
        event_queue.pop_back();

        event.callback(event.arg);
    }

    return true;
}

}
