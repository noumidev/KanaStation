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

#include <common/types.hpp>
#include <core/hw/boot_rom.hpp>
#include <core/hw/bus.hpp>
#include <core/hw/edram.hpp>
#include <core/hw/kirk.hpp>
#include <core/hw/nand.hpp>
#include <core/hw/shared_ram.hpp>
#include <core/hw/spi.hpp>
#include <core/hw/sysctrl.hpp>
#include <core/hw/allegrex/interpreter.hpp>
#include <core/hw/allegrex/scratchpad.hpp>

namespace kanacore {

static std::shared_ptr<spdlog::logger> logger;

static hw::allegrex::Allegrex sc(hw::allegrex::CpuId::CPU_ID_SC);

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

    if (config.nand_path == nullptr) {
        logger->error("No NAND was supplied");
        exit(1);
    }

    hw::bus::initialize();
    hw::boot_rom::initialize(config.boot_path);
    hw::edram::initialize();
    hw::kirk::initialize();
    hw::nand::initialize(config.nand_path);
    hw::shared_ram::initialize();
    hw::spi::initialize();
    hw::sysctrl::initialize();
    hw::allegrex::interpreter::initialize();
    hw::allegrex::scratchpad::initialize();

    // Set up system core memory handlers
    sc.read8   = hw::bus::read<common::u8>;
    sc.read16  = hw::bus::read<common::u16>;
    sc.read32  = hw::bus::read<common::u32>;
    sc.write8  = hw::bus::write<common::u8>;
    sc.write16 = hw::bus::write<common::u16>;
    sc.write32 = hw::bus::write<common::u32>;

    IS_INITIALIZED = true;
}

void soft_reset() {
    // This should soft reset all components (preserves RAM contents, ...)
    hw::bus::soft_reset();
    hw::boot_rom::soft_reset();
    hw::edram::soft_reset();
    hw::kirk::soft_reset();
    hw::nand::soft_reset();
    hw::shared_ram::soft_reset();
    hw::spi::soft_reset();
    hw::sysctrl::soft_reset();
    hw::allegrex::interpreter::soft_reset();
    hw::allegrex::scratchpad::soft_reset();

    sc.soft_reset();
}

void hard_reset() {
    // This should hard reset all components (including memory)
    hw::bus::hard_reset();
    hw::boot_rom::hard_reset();
    hw::edram::hard_reset();
    hw::kirk::hard_reset();
    hw::nand::hard_reset();
    hw::shared_ram::hard_reset();
    hw::spi::hard_reset();
    hw::sysctrl::hard_reset();
    hw::allegrex::interpreter::hard_reset();
    hw::allegrex::scratchpad::hard_reset();

    sc.hard_reset();
}

void shutdown() {
    // This shuts down all components
    hw::bus::shutdown();
    hw::boot_rom::shutdown();
    hw::edram::shutdown();
    hw::kirk::shutdown();
    hw::nand::shutdown();
    hw::shared_ram::shutdown();
    hw::spi::shutdown();
    hw::sysctrl::shutdown();
    hw::allegrex::interpreter::shutdown();
    hw::allegrex::scratchpad::shutdown();
}

hw::allegrex::Allegrex* get_sc_ptr() {
    return &sc;
}

void run() {
    while (true) {
        // This will be set by a scheduler later on
        *sc.get_cycles() = 128;

        hw::allegrex::interpreter::run(&sc);
    }
}

};
