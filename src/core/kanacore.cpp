/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/kanacore.cpp - Emulator core */

#include <core/kanacore.hpp>

#include <cstdlib>
#include <memory>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace kanacore {

static std::shared_ptr<spdlog::logger> logger;

void initialize(const Configuration config) {
    static bool IS_INITIALIZED = false;

    // Ensure we never initialize this twice
    if (IS_INITIALIZED) {
        logger->warn("Emulator already initialized");
        return;
    }

    logger = spdlog::stdout_color_st("KanaCore");

    if (config.boot_path == nullptr) {
        logger->error("No boot ROM was supplied");
        exit(1);
    }

    IS_INITIALIZED = true;
}

void soft_reset() {
    // This should soft reset all components (preserves RAM contents, ...)
}

void hard_reset() {
    // This should hard reset all components (including memory)
}

void shutdown() {
    // This shuts down all components
}

void run() {
    // This runs all components
}

};
