/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/config.hpp - Emulator configuration */

#pragma once

#include <common/types.hpp>

namespace kanacore {

enum MotherboardType {
    MOTHERBOARD_TYPE_TA082,
    MOTHERBOARD_TYPE_TA088,
};

struct Configuration {
    // 48-bit console unique identifier
    common::u64 fuse_id;
    // Motherboard type/revision
    MotherboardType mobo_type;
    // Enables service mode
    bool service_mode;

    // Enables spline rendering (slow!)
    bool render_splines;

    // Path to a PSP boot ROM image
    const char* boot_path;

    // Path to a NAND image (32 MB)
    const char* nand_path;

    // Path to a Memory Stick image (variable size)
    const char* ms_path;

    // Path to a UMD image
    const char* umd_path;
};

Configuration parse_args();

}
