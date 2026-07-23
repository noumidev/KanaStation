/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/clockdiv.cpp - CPU and bus clock divider */

#include <core/hw/clockdiv.hpp>

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <common/types.hpp>
#include <core/kanacore.hpp>
#include <core/hw/bus.hpp>

namespace kanacore::hw::clockdiv {

using namespace common;

constexpr u64 CLOCKDIV_ADDR = 0x1C200000;
constexpr u64 CLOCKDIV_SIZE = 0x1000;

enum IoAddress {
    IO_ADDRESS_CPUDIV = CLOCKDIV_ADDR + 0x000,
    IO_ADDRESS_BUSDIV = CLOCKDIV_ADDR + 0x004,
};

#define HW_CLOCKDIV_CPUDIV ctx.cpu_divider
#define HW_CLOCKDIV_BUSDIV ctx.bus_divider

static struct {
    union {
        u32 raw;

        struct {
            u16 denominator;
            u16 numerator;
        };
    } cpu_divider, bus_divider;
} ctx;

static std::shared_ptr<spdlog::logger> logger;

static u32 read(const u32 addr) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_CPUDIV:
            logger->debug("CPUDIV read32");
            return HW_CLOCKDIV_CPUDIV.raw;
        case IoAddress::IO_ADDRESS_BUSDIV:
            logger->debug("BUSDIV read32");
            return HW_CLOCKDIV_BUSDIV.raw;
        default:
            logger->error("Unmapped read32 @ {:08X}", addr);
            exit(1);
    }
}

static void write(const u32 addr, const u32 data) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_CPUDIV:
            logger->debug("CPUDIV write32 = {:08X}", data);

            HW_CLOCKDIV_CPUDIV.raw = data & 0x01FF01FF;
            break;
        case IoAddress::IO_ADDRESS_BUSDIV:
            logger->debug("BUSDIV write32 = {:08X}", data);

            HW_CLOCKDIV_BUSDIV.raw = data & 0x01FF01FF;
            break;
        default:
            logger->error("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            exit(1);
    }
}

void initialize() {
    logger = spdlog::stdout_color_st("CLOCKDIV");

    std::memset(&ctx, 0, sizeof(ctx));
}

void soft_reset() {
    HW_CLOCKDIV_CPUDIV.denominator = 0x1FF;
    HW_CLOCKDIV_CPUDIV.numerator   = 0x1FF;
    HW_CLOCKDIV_BUSDIV.denominator = 0x1FF;
    HW_CLOCKDIV_BUSDIV.numerator   = 0x1FF;
}

void hard_reset() {
    const bus::PageDescriptor page_desc {
        .read32_func  = read,
        .write32_func = write,
    };

    // TODO: ME has clockdiv registers too
    kanacore::get_sc_bus_ptr()->map(CLOCKDIV_ADDR, CLOCKDIV_SIZE, page_desc);

    soft_reset();
}

void shutdown() {

}

};
