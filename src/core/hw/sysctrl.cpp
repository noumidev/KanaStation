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

#include <core/kanacore.hpp>
#include <core/hw/boot_rom.hpp>
#include <core/hw/bus.hpp>
#include <core/hw/intc.hpp>
#include <core/hw/shared_ram.hpp>
#include <core/hw/allegrex/allegrex.hpp>

namespace kanacore::hw::sysctrl {

using namespace common;

constexpr u64 SYSCTRL_ADDR = 0x1C100000;
constexpr u64 SYSCTRL_SIZE = 0x1000;

constexpr u32 TACHYON_VERSION = 0x40000000;
constexpr u32 RAM_SIZE = 1;

constexpr u64 FUSEID = 0x0000590BB2A18793;

enum IoAddress {
    IO_ADDRESS_NMIEN      = SYSCTRL_ADDR + 0x000,
    IO_ADDRESS_NMIFLAGS   = SYSCTRL_ADDR + 0x004,
    IO_ADDRESS_RAMSIZE    = SYSCTRL_ADDR + 0x040,
    IO_ADDRESS_POSTME     = SYSCTRL_ADDR + 0x044,
    IO_ADDRESS_RESETEN    = SYSCTRL_ADDR + 0x04C,
    IO_ADDRESS_BUSCLKEN   = SYSCTRL_ADDR + 0x050,
    IO_ADDRESS_CLOCKEN_LO = SYSCTRL_ADDR + 0x054,
    IO_ADDRESS_CLOCKEN_HI = SYSCTRL_ADDR + 0x058,
    IO_ADDRESS_CLKSEL_LO  = SYSCTRL_ADDR + 0x05C,
    IO_ADDRESS_CLKSEL_HI  = SYSCTRL_ADDR + 0x060,
    IO_ADDRESS_SPICLKSEL  = SYSCTRL_ADDR + 0x064,
    IO_ADDRESS_PLLCTRL    = SYSCTRL_ADDR + 0x068,
    IO_ADDRESS_AVCPOWER   = SYSCTRL_ADDR + 0x070,
    IO_ADDRESS_IOEN       = SYSCTRL_ADDR + 0x078,
    IO_ADDRESS_GPIOEN     = SYSCTRL_ADDR + 0x07C,
    IO_ADDRESS_CONNSTAT   = SYSCTRL_ADDR + 0x080,
    IO_ADDRESS_FUSEID_LO  = SYSCTRL_ADDR + 0x090,
    IO_ADDRESS_FUSEID_HI  = SYSCTRL_ADDR + 0x094,
    IO_ADDRESS_FUSECONFIG = SYSCTRL_ADDR + 0x098,
    IO_ADDRESS_PLLMULT    = SYSCTRL_ADDR + 0x0FC,
};

#define HW_SYSCTRL_NMIEN      ctx.nmi.enable
#define HW_SYSCTRL_NMIFLAGS   ctx.nmi.flags
#define HW_SYSCTRL_RAMSIZE    ctx.ram_size
#define HW_SYSCTRL_RESETEN    ctx.reset_enable
#define HW_SYSCTRL_BUSCLKEN   ctx.busclock_enable
#define HW_SYSCTRL_CLOCKEN_LO ctx.clock_enable[0]
#define HW_SYSCTRL_CLOCKEN_HI ctx.clock_enable[1]
#define HW_SYSCTRL_CLKSEL_LO  ctx.clock_select[0]
#define HW_SYSCTRL_CLKSEL_HI  ctx.clock_select[1]
#define HW_SYSCTRL_SPICLKSEL  ctx.spi_clock_select
#define HW_SYSCTRL_PLLCTRL    ctx.pll_control
#define HW_SYSCTRL_AVCPOWER   ctx.avc_power
#define HW_SYSCTRL_IOEN       ctx.io_enable
#define HW_SYSCTRL_GPIOEN     ctx.gpio_enable
#define HW_SYSCTRL_CONNSTAT   ctx.connection_status
#define HW_SYSCTRL_PLLMULT    ctx.pll_multiplier

enum ResetDevice {
    RESET_DEVICE_SC  = 1,
    RESET_DEVICE_ME  = 2,
    RESET_DEVICE_NUM = 17,
    RESET_DEVICE_ALL = (1 << ResetDevice::RESET_DEVICE_NUM) - 1,
};

static std::shared_ptr<spdlog::logger> logger;

// Installed reset handlers
static std::array<void (*)(void), ResetDevice::RESET_DEVICE_NUM> reset_funcs;

static struct {
    struct {
        u32 enable;
        u32 flags;
    } nmi;

    u32 ram_size;
    u32 reset_enable;
    u32 busclock_enable;
    u32 clock_enable[2];
    u32 clock_select[2];
    u32 spi_clock_select;
    u32 pll_control;
    u32 avc_power;
    u32 io_enable;
    u32 gpio_enable;
    u32 connection_status;
    u32 pll_multiplier;
} ctx;

static void reset_sc() {
    get_sc_ptr()->soft_reset();

    // This unmaps the 4 KB scratchpad and boot ROM and maps shared RAM to the reset vector
    boot_rom::soft_reset();
    shared_ram::soft_reset();
}

static void reset_me() {
    get_me_ptr()->soft_reset();
}

static u32 read(const u32 addr) {
    constexpr u32 FUSECONFIG = 0x0000590B;

    switch (addr) {
        case IoAddress::IO_ADDRESS_NMIEN:
            // For some reason, the boot ROM reads this and thinks an NMI
            // occurred if this is not zero
            logger->debug("NMIEN read32");
            return HW_SYSCTRL_NMIEN;
        case IoAddress::IO_ADDRESS_RAMSIZE:
            logger->debug("RAMSIZE read32");
            return HW_SYSCTRL_RAMSIZE;
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
        case IoAddress::IO_ADDRESS_CLKSEL_LO:
            logger->debug("CLKSEL_LO read32");
            return HW_SYSCTRL_CLKSEL_LO;
        case IoAddress::IO_ADDRESS_CLKSEL_HI:
            logger->debug("CLKSEL_HI read32");
            return HW_SYSCTRL_CLKSEL_HI;
        case IoAddress::IO_ADDRESS_SPICLKSEL:
            logger->debug("SPICLKSEL read32");
            return HW_SYSCTRL_SPICLKSEL;
        case IoAddress::IO_ADDRESS_PLLCTRL:
            logger->debug("PLLCTRL read32");
            return HW_SYSCTRL_PLLCTRL;
        case IoAddress::IO_ADDRESS_AVCPOWER:
            logger->debug("AVCPOWER read32");
            return HW_SYSCTRL_AVCPOWER;
        case IoAddress::IO_ADDRESS_IOEN:
            logger->debug("IOEN read32");
            return HW_SYSCTRL_IOEN;
        case IoAddress::IO_ADDRESS_GPIOEN:
            // Potentially move this to future GPIO emulation
            logger->debug("GPIOEN read32");
            return HW_SYSCTRL_GPIOEN;
        case IoAddress::IO_ADDRESS_CONNSTAT:
            logger->debug("CONNSTAT read32");
            return HW_SYSCTRL_CONNSTAT;
        case IoAddress::IO_ADDRESS_FUSEID_LO:
            logger->debug("FUSEID_LO read32");
            return (u32)FUSEID;
        case IoAddress::IO_ADDRESS_FUSEID_HI:
            logger->debug("FUSEID_HI read32");
            return FUSEID >> 32;
        case IoAddress::IO_ADDRESS_FUSECONFIG:
            logger->debug("FUSECONFIG read32");
            return FUSECONFIG;
        case IoAddress::IO_ADDRESS_PLLMULT:
            logger->debug("PLLMULT read32");
            return HW_SYSCTRL_PLLMULT;
        case SYSCTRL_ADDR + 0x03C:
        case SYSCTRL_ADDR + 0x074:
            logger->warn("Unmapped read32 @ {:08X}", addr);
            return 0;
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
    // Devices are reset when the bit is cleared to 0 again

    for (u32 i = 0; i < ResetDevice::RESET_DEVICE_NUM; i++) {
        if (((HW_SYSCTRL_RESETEN & (1 << i)) == 0) && ((data & (1 << i)) != 0)) {
            logger->debug("Reset asserted for {}", RESET_DEVICE_NAMES[i]);

            if ((reset_funcs[i] != nullptr) && (i == ResetDevice::RESET_DEVICE_SC)) {
                // This needs to reset here
                reset_funcs[i]();
            }
        } else if (((HW_SYSCTRL_RESETEN & (1 << i)) != 0) && ((data & (1 << i)) == 0)) {
            logger->debug("Reset cleared for {}", RESET_DEVICE_NAMES[i]);

            if ((reset_funcs[i] != nullptr) && (i != ResetDevice::RESET_DEVICE_SC)) {
                reset_funcs[i]();
            }
        }
    }

    HW_SYSCTRL_RESETEN = data;
}

static void write(const u32 addr, const u32 data) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_NMIFLAGS:
            logger->debug("NMIFLAGS write32 = {:08X}", data);
            
            // IPL writes all 1s after reset, so this probably clears bits when
            // 1 is written to them
            HW_SYSCTRL_NMIFLAGS &= ~data;
            break;
        case IoAddress::IO_ADDRESS_RAMSIZE:
            logger->debug("RAMSIZE write32 = {:08X}", data);
            
            // Tachyon version is read-only
            HW_SYSCTRL_RAMSIZE = (HW_SYSCTRL_RAMSIZE & 0xFF000800) | (data & 0xFFF7FF);
            break;
        case IoAddress::IO_ADDRESS_POSTME:
            logger->debug("POSTME write32 = {:08X}", data);
            
            if ((data & 1) != 0) {
                intc::assert_me_interrupt(31);
            }
            break;
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
        case IoAddress::IO_ADDRESS_CLKSEL_LO:
            logger->debug("CLKSEL_LO write32 = {:08X}", data);
            HW_SYSCTRL_CLKSEL_LO = data;
            break;
        case IoAddress::IO_ADDRESS_CLKSEL_HI:
            logger->debug("CLKSEL_HI write32 = {:08X}", data);
            HW_SYSCTRL_CLKSEL_HI = data;
            break;
        case IoAddress::IO_ADDRESS_SPICLKSEL:
            logger->debug("SPICLKSEL write32 = {:08X}", data);

            HW_SYSCTRL_SPICLKSEL = data;
            break;
        case IoAddress::IO_ADDRESS_PLLCTRL:
            logger->debug("PLLCTRL write32 = {:08X}", data);

            HW_SYSCTRL_PLLCTRL = data;
            break;
        case IoAddress::IO_ADDRESS_AVCPOWER:
            logger->debug("AVCPOWER write32 = {:08X}", data);

            HW_SYSCTRL_AVCPOWER = data;
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
        case IoAddress::IO_ADDRESS_CONNSTAT:
            logger->debug("CONNSTAT write32 = {:08X}", data);
            break;
        case SYSCTRL_ADDR + 0x03C:
        case SYSCTRL_ADDR + 0x074:
            logger->warn("Unmapped write32 @ {:08X} = {:08X}", addr, data);
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
    reset_funcs[ResetDevice::RESET_DEVICE_ME] = reset_me;

    std::memset(&ctx, 0, sizeof(ctx));

    HW_SYSCTRL_RAMSIZE = TACHYON_VERSION | RAM_SIZE;
}

void soft_reset() {
    
}

void hard_reset() {
    const bus::PageDescriptor page_desc {
        // To my knowledge, SYSCTRL I/O is never not read/written using 32-bit accesses
        .read32_func  = read,
        .write32_func = write,
    };

    kanacore::get_sc_bus_ptr()->map(SYSCTRL_ADDR, SYSCTRL_SIZE, page_desc);

    // Things to consider:
    // Media Engine has its own set of SYSCTRL registers here, how do we deal with this?
}

void shutdown() {

}

u32 get_gpio_enable() {
    return HW_SYSCTRL_GPIOEN;
}

u64 get_fuseid() {
    return FUSEID;
}

void set_ms0_connected() {
    HW_SYSCTRL_CONNSTAT |= 0x100;
}

void clear_ms0_connected() {
    HW_SYSCTRL_CONNSTAT &= ~0x100;
}

};
