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
#include <core/scheduler.hpp>
#include <core/hw/audio.hpp>
#include <core/hw/boot_rom.hpp>
#include <core/hw/bus.hpp>
#include <core/hw/ddr_ram.hpp>
#include <core/hw/dmacplus.hpp>
#include <core/hw/edram.hpp>
#include <core/hw/gpio.hpp>
#include <core/hw/i2c.hpp>
#include <core/hw/intc.hpp>
#include <core/hw/kirk.hpp>
#include <core/hw/ms.hpp>
#include <core/hw/nand.hpp>
#include <core/hw/shared_ram.hpp>
#include <core/hw/spi.hpp>
#include <core/hw/spock.hpp>
#include <core/hw/syscon.hpp>
#include <core/hw/sysctrl.hpp>
#include <core/hw/systime.hpp>
#include <core/hw/allegrex/interpreter.hpp>
#include <core/hw/allegrex/scratchpad.hpp>
#include <core/hw/me/scratchpad.hpp>
#include <core/hw/me/vme_dmac.hpp>
#include <core/hw/uart/uart.hpp>

namespace kanacore {

using namespace common;

static std::shared_ptr<spdlog::logger> logger;

static hw::allegrex::Allegrex sc(hw::allegrex::CpuId::CPU_ID_SC);
static hw::allegrex::Allegrex me(hw::allegrex::CpuId::CPU_ID_ME);

static bool frame_end = false;

// Move this to display code later on
static void vsync(const int) {
    hw::intc::assert_sc_interrupt(30);

    scheduler::schedule_event(
        scheduler::EventType::VSYNC,
        vsync,
        0,
        scheduler::from_microseconds(16666)
    );

    hw::dmacplus::scanout();

    frame_end = true;
}

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

    scheduler::initialize();
    hw::audio::initialize();
    hw::boot_rom::initialize(config.boot_path);
    hw::ddr_ram::initialize();
    hw::dmacplus::initialize();
    hw::edram::initialize();
    hw::gpio::initialize();
    hw::i2c::initialize();
    hw::intc::initialize();
    hw::kirk::initialize();
    hw::ms::initialize(config.ms_path);
    hw::nand::initialize(config.nand_path);
    hw::shared_ram::initialize();
    hw::spi::initialize();
    hw::spock::initialize();
    hw::syscon::initialize();
    hw::sysctrl::initialize();
    hw::systime::initialize();
    hw::allegrex::interpreter::initialize();
    hw::allegrex::scratchpad::initialize();
    hw::me::scratchpad::initialize();
    hw::me::vme_dmac::initialize();
    hw::uart::initialize();

    IS_INITIALIZED = true;
}

void soft_reset() {
    // This should soft reset all components (preserves RAM contents, ...)
    scheduler::soft_reset();
    hw::audio::soft_reset();
    hw::boot_rom::soft_reset();
    hw::ddr_ram::soft_reset();
    hw::dmacplus::soft_reset();
    hw::edram::soft_reset();
    hw::gpio::soft_reset();
    hw::i2c::soft_reset();
    hw::intc::soft_reset();
    hw::kirk::soft_reset();
    hw::ms::soft_reset();
    hw::nand::soft_reset();
    hw::shared_ram::soft_reset();
    hw::spi::soft_reset();
    hw::spock::soft_reset();
    hw::syscon::soft_reset();
    hw::sysctrl::soft_reset();
    hw::systime::soft_reset();
    hw::allegrex::interpreter::soft_reset();
    hw::allegrex::scratchpad::soft_reset();
    hw::me::scratchpad::soft_reset();
    hw::me::vme_dmac::soft_reset();
    hw::uart::soft_reset();

    sc.soft_reset();
    me.soft_reset();
}

void hard_reset() {
    // This should hard reset all components (including memory)
    scheduler::hard_reset();
    hw::audio::hard_reset();
    hw::boot_rom::hard_reset();
    hw::ddr_ram::hard_reset();
    hw::dmacplus::hard_reset();
    hw::edram::hard_reset();
    hw::gpio::hard_reset();
    hw::i2c::hard_reset();
    hw::intc::hard_reset();
    hw::kirk::hard_reset();
    hw::ms::hard_reset();
    hw::nand::hard_reset();
    hw::shared_ram::hard_reset();
    hw::spi::hard_reset();
    hw::spock::hard_reset();
    hw::syscon::hard_reset();
    hw::sysctrl::hard_reset();
    hw::systime::hard_reset();
    hw::allegrex::interpreter::hard_reset();
    hw::allegrex::scratchpad::hard_reset();
    hw::me::scratchpad::hard_reset();
    hw::me::vme_dmac::hard_reset();
    hw::uart::hard_reset();

    sc.hard_reset();
    me.hard_reset();

    // Halt ME until SC resets it
    me.wait_for_interrupt();

    scheduler::schedule_event(
        scheduler::EventType::VSYNC,
        vsync,
        0,
        scheduler::from_microseconds(16666)
    );
}

void shutdown() {
    // This shuts down all components
    scheduler::shutdown();
    hw::audio::shutdown();
    hw::boot_rom::shutdown();
    hw::ddr_ram::shutdown();
    hw::dmacplus::shutdown();
    hw::edram::shutdown();
    hw::gpio::shutdown();
    hw::i2c::shutdown();
    hw::intc::shutdown();
    hw::kirk::shutdown();
    hw::ms::shutdown();
    hw::nand::shutdown();
    hw::shared_ram::shutdown();
    hw::spi::shutdown();
    hw::spock::shutdown();
    hw::syscon::shutdown();
    hw::sysctrl::shutdown();
    hw::systime::shutdown();
    hw::allegrex::interpreter::shutdown();
    hw::allegrex::scratchpad::shutdown();
    hw::me::scratchpad::shutdown();
    hw::me::vme_dmac::shutdown();
    hw::uart::shutdown();
}

hw::allegrex::Allegrex* get_sc_ptr() {
    return &sc;
}

hw::allegrex::Allegrex* get_me_ptr() {
    return &me;
}

hw::bus::Bus* get_sc_bus_ptr() {
    return sc.get_bus_ptr();
}

hw::bus::Bus* get_me_bus_ptr() {
    return me.get_bus_ptr();
}

bool run() {
    // Dirty way to exit the main loop, but it'll suffice for now
    frame_end = false;

    while (!frame_end) {
        (void)scheduler::run();
    }

    return false;
}

};
