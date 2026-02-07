/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/config.cpp - Emulator configuration */

#include <core/config.hpp>

namespace kanacore {

Configuration parse_args(const int argc, const char** argv) {
    constexpr int NUM_ARGS = 1;

    Configuration config {
        .boot_path = nullptr
    };

    if (argc > NUM_ARGS) {
        config.boot_path = argv[1];
    }

    return config;
}

}
