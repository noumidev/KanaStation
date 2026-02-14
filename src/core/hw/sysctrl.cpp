/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/sysctrl.cpp - System control registers */

#include <core/hw/sysctrl.hpp>

#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <common/types.hpp>
#include <core/kanacore.hpp>
#include <core/hw/boot_rom.hpp>
#include <core/hw/bus.hpp>
#include <core/hw/shared_ram.hpp>
#include <core/hw/allegrex/allegrex.hpp>

namespace kanacore::hw::sysctrl {

using namespace common;

constexpr u64 SYSCTRL_ADDR = 0x1C100000;
constexpr u64 SYSCTRL_SIZE = 0x1000;

enum IoAddress {
    IO_ADDRESS_NMIEN      = SYSCTRL_ADDR + 0x000,
    IO_ADDRESS_RESETEN    = SYSCTRL_ADDR + 0x04C,
    IO_ADDRESS_BUSCLKEN   = SYSCTRL_ADDR + 0x050,
    IO_ADDRESS_CLOCKEN_LO = SYSCTRL_ADDR + 0x054,
    IO_ADDRESS_CLOCKEN_HI = SYSCTRL_ADDR + 0x058,
    IO_ADDRESS_PLLFREQ    = SYSCTRL_ADDR + 0x068,
    IO_ADDRESS_IOEN       = SYSCTRL_ADDR + 0x078,
    IO_ADDRESS_GPIOEN     = SYSCTRL_ADDR + 0x07C,
};

#define HW_SYSCTRL_NMIEN      ctx.nmi.enable
#define HW_SYSCTRL_RESETEN    ctx.reset_enable
#define HW_SYSCTRL_BUSCLKEN   ctx.busclock_enable
#define HW_SYSCTRL_CLOCKEN_LO ctx.clock_enable[0]
#define HW_SYSCTRL_CLOCKEN_HI ctx.clock_enable[1]
#define HW_SYSCTRL_PLLFREQ    ctx.pll_frequency
#define HW_SYSCTRL_IOEN       ctx.io_enable
#define HW_SYSCTRL_GPIOEN     ctx.gpio_enable

enum ResetDevice {
    RESET_DEVICE_SC  = 1,
    RESET_DEVICE_NUM = 17,
    RESET_DEVICE_ALL = (1 << ResetDevice::RESET_DEVICE_NUM) - 1,
};

static std::shared_ptr<spdlog::logger> logger;

// Installed reset handlers
static std::array<void (*)(void), ResetDevice::RESET_DEVICE_NUM> reset_funcs;

static struct {
    struct {
        u32 enable;
    } nmi;

    u32 reset_enable;
    u32 busclock_enable;
    u32 clock_enable[2];
    u32 pll_frequency;
    u32 io_enable;
    u32 gpio_enable;
} ctx;

static void reset_sc() {
    // This might just be the same as a hard reset, but whatever
    get_sc_ptr()->soft_reset();

    // This unmaps the 4 KB scratchpad and boot ROM and maps shared RAM to the reset vector
    boot_rom::soft_reset();
    shared_ram::soft_reset();
}

static u32 read(const u32 addr) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_NMIEN:
            // For some reason, the boot ROM reads this and thinks an NMI
            // occurred if this is not zero
            logger->debug("NMIEN read32");
            return HW_SYSCTRL_NMIEN;
        case IoAddress::IO_ADDRESS_RESETEN:
            logger->debug("RESETEN read32");
            return HW_SYSCTRL_RESETEN;
        case IoAddress::IO_ADDRESS_BUSCLKEN:
            logger->debug("BUSCLKEN read32");
            return HW_SYSCTRL_BUSCLKEN;
        case IoAddress::IO_ADDRESS_CLOCKEN_LO:
            logger->debug("CLOCKEN_LO read32");
            return HW_SYSCTRL_CLOCKEN_LO;
        case IoAddress::IO_ADDRESS_CLOCKEN_HI:
            logger->debug("CLOCKEN_HI read32");
            return HW_SYSCTRL_CLOCKEN_HI;
        case IoAddress::IO_ADDRESS_PLLFREQ:
            logger->debug("PLLFREQ read32");

            // The boot ROM reads the upper 16 bits of this register and
            // enables the audio clock(?) if non-zero. To my knowledge, it's unknown
            // what these bits mean.
            // TODO: figure out what value to initialize the PLL freq to
            return HW_SYSCTRL_PLLFREQ;
        case IoAddress::IO_ADDRESS_IOEN:
            logger->debug("IOEN read32");
            return HW_SYSCTRL_IOEN;
        case IoAddress::IO_ADDRESS_GPIOEN:
            // Potentially move this to future GPIO emulation
            logger->debug("GPIOEN read32");
            return HW_SYSCTRL_GPIOEN;
        default:
            logger->error("Unmapped read32 @ {:08X}", addr);
            exit(1);
    }
}

static void write_reset_enable(const u32 data) {
    constexpr const char* RESET_DEVICE_NAMES[] = {
        "TOP?" , "SC"   , "ME"  , "AW?", "VME", "AVC"     , "USB" , "ATA HDD?",
        "MSIF0", "MSIF1", "KIRK", "N/A", "???", "USB Host", "MS0?", "MS1?"    ,
        "???" ,
    };

    // This puts peripherals into reset state when the corresponding bit is 1.
    // I'm unsure if this is edge or level triggered, but I will assume the former for now
    // (only way to make SC reset work here)

    for (u32 i = 0; i < ResetDevice::RESET_DEVICE_NUM; i++) {
        if (((HW_SYSCTRL_RESETEN & (1 << i)) == 0) && ((data & (1 << i)) != 0)) {
            logger->debug("Reset asserted for {}", RESET_DEVICE_NAMES[i]);

            if (reset_funcs[i] != nullptr) {
                reset_funcs[i]();
            }
        }
    }

    HW_SYSCTRL_RESETEN = data;
}

static void write(const u32 addr, const u32 data) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_RESETEN:
            logger->debug("RESETEN write32 = {:08X}", data);
            write_reset_enable(data);
            break;
        case IoAddress::IO_ADDRESS_BUSCLKEN:
            logger->debug("BUSCLKEN write32 = {:08X}", data);

            // This should en-/disable clocking for certain peripherals
            HW_SYSCTRL_BUSCLKEN = data;
            break;
        case IoAddress::IO_ADDRESS_CLOCKEN_LO:
            logger->debug("CLOCKEN_LO write32 = {:08X}", data);

            // See above
            HW_SYSCTRL_CLOCKEN_LO = data;
            break;
        case IoAddress::IO_ADDRESS_CLOCKEN_HI:
            logger->debug("CLOCKEN_HI write32 = {:08X}", data);

            // See above
            HW_SYSCTRL_CLOCKEN_HI = data;
            break;
        case IoAddress::IO_ADDRESS_IOEN:
            logger->debug("IOEN write32 = {:08X}", data);

            // See above
            HW_SYSCTRL_IOEN = data;
            break;
        case IoAddress::IO_ADDRESS_GPIOEN:
            logger->debug("GPIOEN write32 = {:08X}", data);
            HW_SYSCTRL_GPIOEN = data;
            break;
        default:
            logger->error("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            exit(1);
    }
}

void initialize() {
    logger = spdlog::stdout_color_st("SysCtrl");

    reset_funcs.fill(nullptr);

    reset_funcs[ResetDevice::RESET_DEVICE_SC] = reset_sc;

    std::memset(&ctx, 0, sizeof(ctx));
}

void soft_reset() {
    
}

void hard_reset() {
    const bus::PageDescriptor page_desc {
        // To my knowledge, SYSCTRL I/O is never not read/written using 32-bit accesses
        .read32_func  = read,
        .write32_func = write,
    };

    bus::map(SYSCTRL_ADDR, SYSCTRL_SIZE, page_desc);

    // Things to consider:
    // Media Engine has its own set of SYSCTRL registers here, how do we deal with this?
}

void shutdown() {

}

};
