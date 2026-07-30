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

#define HW_CLOCKDIV_CPUDIV ctx[cpu_num].cpu_divider
#define HW_CLOCKDIV_BUSDIV ctx[cpu_num].bus_divider

static struct {
    union {
        u32 raw;

        struct {
            u16 denominator;
            u16 numerator;
        };
    } cpu_divider, bus_divider;

    std::shared_ptr<spdlog::logger> logger;
} ctx[2];

template<int cpu_num>
static u32 read(const u32 addr) {
    assert(cpu_num < 2);

    auto& clockdiv = ctx[cpu_num];

    switch (addr) {
        case IoAddress::IO_ADDRESS_CPUDIV:
            clockdiv.logger->debug("CPUDIV read32");
            return HW_CLOCKDIV_CPUDIV.raw;
        case IoAddress::IO_ADDRESS_BUSDIV:
            clockdiv.logger->debug("BUSDIV read32");
            return HW_CLOCKDIV_BUSDIV.raw;
        default:
            clockdiv.logger->error("Unmapped read32 @ {:08X}", addr);
            exit(1);
    }
}

template<int cpu_num>
static void write(const u32 addr, const u32 data) {
    assert(cpu_num < 2);

    auto& clockdiv = ctx[cpu_num];

    switch (addr) {
        case IoAddress::IO_ADDRESS_CPUDIV:
            clockdiv.logger->debug("CPUDIV write32 = {:08X}", data);

            HW_CLOCKDIV_CPUDIV.raw = data & 0x01FF01FF;
            break;
        case IoAddress::IO_ADDRESS_BUSDIV:
            clockdiv.logger->debug("BUSDIV write32 = {:08X}", data);

            HW_CLOCKDIV_BUSDIV.raw = data & 0x01FF01FF;
            break;
        default:
            clockdiv.logger->error("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            exit(1);
    }
}

void initialize() {
    ctx[0].logger = spdlog::stdout_color_st("CLOCKDIV");
    ctx[1].logger = spdlog::stdout_color_st("ME CLOCKDIV");
}

template<int cpu_num>
void soft_reset_impl() {
    assert(cpu_num < 2);

    HW_CLOCKDIV_CPUDIV.denominator = 0x1FF;
    HW_CLOCKDIV_CPUDIV.numerator   = 0x1FF;
    HW_CLOCKDIV_BUSDIV.denominator = 0x1FF;
    HW_CLOCKDIV_BUSDIV.numerator   = 0x1FF;
}

void soft_reset() {
    soft_reset_impl<0>();
    soft_reset_impl<1>();
}

void hard_reset() {
    const bus::PageDescriptor page_desc_sc {
        .read32_func  = read<0>,
        .write32_func = write<0>,
    };

    const bus::PageDescriptor page_desc_me {
        .read32_func  = read<1>,
        .write32_func = write<1>,
    };

    kanacore::get_sc_bus_ptr()->map(CLOCKDIV_ADDR, CLOCKDIV_SIZE, page_desc_sc);
    kanacore::get_me_bus_ptr()->map(CLOCKDIV_ADDR, CLOCKDIV_SIZE, page_desc_me);

    soft_reset();
}

void shutdown() {

}

};
