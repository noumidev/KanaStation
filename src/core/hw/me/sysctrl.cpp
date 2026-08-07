/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/me/sysctrl.cpp - MediaEngine system control registers */

#include <core/hw/me/sysctrl.hpp>

#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <core/kanacore.hpp>
#include <core/hw/bus.hpp>
#include <core/hw/intc.hpp>

namespace kanacore::hw::me::sysctrl {

using namespace common;

constexpr u64 SYSCTRL_ADDR = 0x1C100000;
constexpr u64 SYSCTRL_SIZE = 0x1000;

// Unsure if the ME has access to the TACHYON version...
constexpr u32 TACHYON_VERSION = 0x40000000;
constexpr u32 RAM_SIZE = 1;

// All of this is assuming this has the same registers as SC SYSCTRL...
enum IoAddress {
    IO_ADDRESS_NMIEN      = SYSCTRL_ADDR + 0x000,
    IO_ADDRESS_NMIFLAGS   = SYSCTRL_ADDR + 0x004,
    IO_ADDRESS_RAMSIZE    = SYSCTRL_ADDR + 0x040,
    IO_ADDRESS_POSTSC     = SYSCTRL_ADDR + 0x044,
    IO_ADDRESS_RESETEN    = SYSCTRL_ADDR + 0x04C,
    IO_ADDRESS_BUSCLKEN   = SYSCTRL_ADDR + 0x050,
    IO_ADDRESS_CLOCKEN_LO = SYSCTRL_ADDR + 0x054,
    IO_ADDRESS_CLKSEL_LO  = SYSCTRL_ADDR + 0x05C,
};

#define HW_SYSCTRL_NMIEN      ctx.nmi.enable
#define HW_SYSCTRL_NMIFLAGS   ctx.nmi.flags
#define HW_SYSCTRL_RAMSIZE    ctx.ram_size
#define HW_SYSCTRL_RESETEN    ctx.reset_enable
#define HW_SYSCTRL_BUSCLKEN   ctx.busclock_enable
#define HW_SYSCTRL_CLOCKEN_LO ctx.clock_enable[0]
#define HW_SYSCTRL_CLKSEL_LO  ctx.clock_select[0]

static std::shared_ptr<spdlog::logger> logger;

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
} ctx;

static u32 read(const u32 addr) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_NMIEN:
            logger->debug("NMIEN read32");
            return HW_SYSCTRL_NMIEN;
        case IoAddress::IO_ADDRESS_BUSCLKEN:
            logger->debug("BUSCLKEN read32");
            return HW_SYSCTRL_BUSCLKEN;
        case IoAddress::IO_ADDRESS_CLOCKEN_LO:
            logger->debug("CLOCKEN_LO read32");
            return HW_SYSCTRL_CLOCKEN_LO;
        case IoAddress::IO_ADDRESS_CLKSEL_LO:
            logger->debug("CLKSEL_LO read32");
            return HW_SYSCTRL_CLKSEL_LO;
        default:
            logger->error("Unmapped read32 @ {:08X}", addr);
            exit(1);
    }
}

static void write(const u32 addr, const u32 data) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_NMIFLAGS:
            logger->debug("NMIFLAGS write32 = {:08X}", data);
            
            // The ME firmware writes all 1s after reset, so this probably clears bits when
            // 1 is written to them
            HW_SYSCTRL_NMIFLAGS &= ~data;
            break;
        case IoAddress::IO_ADDRESS_RAMSIZE:
            logger->debug("RAMSIZE write32 = {:08X}", data);
            
            // Tachyon version is read-only
            HW_SYSCTRL_RAMSIZE = (HW_SYSCTRL_RAMSIZE & 0xFF000800) | (data & 0xFFF7FF);
            break;
        case IoAddress::IO_ADDRESS_POSTSC:
            logger->debug("POSTSC write32 = {:08X}", data);
            
            if ((data & 1) != 0) {
                intc::assert_sc_interrupt(31);
            }
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
        case IoAddress::IO_ADDRESS_CLKSEL_LO:
            logger->debug("CLKSEL_LO write32 = {:08X}", data);

            HW_SYSCTRL_CLKSEL_LO = data;
            break;
        default:
            logger->error("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            exit(1);
    }
}

void initialize() {
    logger = spdlog::stdout_color_st("ME SysCtrl");
}

void soft_reset() {
    HW_SYSCTRL_RAMSIZE = TACHYON_VERSION | RAM_SIZE;
}

void hard_reset() {
    const bus::PageDescriptor page_desc {
        // To my knowledge, SYSCTRL I/O is never not read/written using 32-bit accesses
        .read32_func  = read,
        .write32_func = write,
    };

    kanacore::get_me_bus_ptr()->map(SYSCTRL_ADDR, SYSCTRL_SIZE, page_desc);

    soft_reset();
}

void shutdown() {

}

};
