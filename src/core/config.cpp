/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/config.cpp - Emulator configuration */

#include <core/config.hpp>

namespace kanacore {

Configuration parse_args(const int argc, const char** argv) {
    constexpr int NUM_ARGS = 2;

    Configuration config {
        .boot_path = nullptr,
        .nand_path = nullptr,
    };

    if (argc > NUM_ARGS) {
        config.boot_path = argv[1];
        config.nand_path = argv[2];
    }

    return config;
}

}
