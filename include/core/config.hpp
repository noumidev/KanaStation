/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/config.hpp - Emulator configuration */

#pragma once

namespace kanacore {

struct Configuration {
    // Path to a PSP boot ROM image
    const char* boot_path;
};

Configuration parse_args(const int argc, const char** argv);

}
