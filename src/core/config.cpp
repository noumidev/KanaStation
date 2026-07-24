/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/config.cpp - Emulator configuration */

#include <string>

#define TOML_EXCEPTIONS 0
#include <toml++/toml.hpp>

#include <core/config.hpp>

namespace kanacore {

using namespace common;

static std::string boot_path, nand_path, ms_path, umd_path;

Configuration parse_args() {
    Configuration config {
        .fuse_id   = 0,
        .mobo_type = MotherboardType::MOTHERBOARD_TYPE_TA082,
        .service_mode = false,
        .boot_path = nullptr,
        .nand_path = nullptr,
        .ms_path   = nullptr,
        .umd_path  = nullptr,
    };

    const toml::parse_result result = toml::parse_file("config.toml");

    toml::table table;

    if (result) {
        // Found the config file
        table = result.table();

        config.fuse_id = table.at_path("core.fuse_id").value_or<u64>(0);
        
        // We'll ignore parsing the motherboard type for now as we only support
        // one at the moment

        config.service_mode = table.at_path("core.service_mode").value_or<bool>(false);

        boot_path = table.at_path("boot_rom.path").value_or<std::string>("");
        nand_path = table.at_path("nand.path").value_or<std::string>("");
        ms_path   = table.at_path("memory_stick.path").value_or<std::string>("");
        umd_path  = table.at_path("umd.path").value_or<std::string>("");

        if (boot_path != "") {
            config.boot_path = boot_path.c_str();
        }

        if (nand_path != "") {
            config.nand_path = nand_path.c_str();
        }

        if (ms_path != "") {
            config.ms_path = ms_path.c_str();
        }

        if (umd_path != "") {
            config.umd_path = umd_path.c_str();
        }
    }

    return config;
}

}
