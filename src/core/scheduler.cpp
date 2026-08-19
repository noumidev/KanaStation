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
#include <string>
#include <vector>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <core/kanacore.hpp>
#include <core/hw/allegrex/allegrex.hpp>
#include <core/hw/allegrex/interpreter.hpp>

namespace kanacore::scheduler {

using namespace common;

static std::shared_ptr<spdlog::logger> logger;

constexpr i64 MAX_CYCLES  = 512;
constexpr i64 SYNC_CYCLES = 128;

static std::vector<std::string> event_names;

struct Event {
    u32 id;
    Callback callback;

    int arg;
    i64 timestamp;
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

u32 register_event(const char* name) {
    static u32 id_pool = 0;

    event_names.push_back(name);
    
    return id_pool++;
}

void schedule_event(const u32 id, Callback callback, const int arg, const i64 cycles, const bool auto_cancel) {
    assert(id < event_names.size());

    hw::allegrex::Allegrex* sc = kanacore::get_sc_ptr();
    hw::allegrex::Allegrex* me = kanacore::get_me_ptr();

    logger->debug("Scheduling event {} with arg {} in {} cycles", event_names[id], arg, cycles);

    if (auto_cancel) {
        // Auto-canceling events would break a bunch, so don't force it
        cancel_event(id);
    }

    const i64 event_timestamp = std::max(*sc->get_cycles(), *me->get_cycles()) + cycles;

    const Event event{ id, callback, arg, event_timestamp };

    auto it = std::upper_bound(event_queue.begin(), event_queue.end(), event,
        [](const Event& a, const Event& b) {
            return a.timestamp > b.timestamp;
        }
    );
    
    event_queue.insert(it, event);

    // If the new event expires before the current event, we make it the new closest event
    if (event_timestamp < global_timestamp) {
        global_timestamp = event_timestamp;

        *sc->get_target_timestamp() = event_timestamp;
        *me->get_target_timestamp() = event_timestamp;
    }
}

void cancel_event(const u32 id) {
    assert(id < event_names.size());

    event_queue.erase(
        std::remove_if(event_queue.begin(), event_queue.end(),
            [id](const Event& e) {
                return e.id == id;
            }
        ),
        event_queue.end()
    );
}

bool run() {
    hw::allegrex::Allegrex* sc = kanacore::get_sc_ptr();
    hw::allegrex::Allegrex* me = kanacore::get_me_ptr();

    if (event_queue.empty()) {
        global_timestamp = std::max(*sc->get_cycles(), *me->get_cycles()) + MAX_CYCLES;
    } else {
        global_timestamp = event_queue.back().timestamp;
    }

    while ((*sc->get_cycles() < global_timestamp) || (*me->get_cycles() < global_timestamp)) {
        const i64 sc_cycles = *sc->get_cycles();
        const i64 me_cycles = *me->get_cycles();

        // Step the CPU that's behind
        if (sc_cycles <= me_cycles) {
            hw::allegrex::interpreter::run(sc, std::min(me_cycles + SYNC_CYCLES, global_timestamp));
        } else {
            hw::allegrex::interpreter::run(me, std::min(sc_cycles + SYNC_CYCLES, global_timestamp));
        }
    }

    const i64 new_timestamp = std::min(*sc->get_cycles(), *me->get_cycles());

    // Process all events with an expired timestamp
    while (!event_queue.empty() && (event_queue.back().timestamp <= new_timestamp)) {
        const Event event = event_queue.back();
        event_queue.pop_back();

        event.callback(event.arg);
    }

    return true;
}

}
