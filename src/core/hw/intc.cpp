/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/intc.cpp - Interrupt controller registers */

#include <core/hw/intc.hpp>

#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <core/kanacore.hpp>
#include <core/hw/allegrex/allegrex.hpp>
#include <core/hw/bus.hpp>

namespace kanacore::hw::intc {

using namespace common;

constexpr u64 INTC_ADDR = 0x1C300000;
constexpr u64 INTC_SIZE = 0x1000;

constexpr u64 NUM_INTCS = 2;

enum IoAddress {
    IO_ADDRESS_FLAGS_LO     = INTC_ADDR + 0x000,
    IO_ADDRESS_RAWFLAGS_LO  = INTC_ADDR + 0x004,
    IO_ADDRESS_MASK_LO      = INTC_ADDR + 0x008,
    IO_ADDRESS_FLAGS_MID    = INTC_ADDR + 0x010,
    IO_ADDRESS_RAWFLAGS_MID = INTC_ADDR + 0x014,
    IO_ADDRESS_MASK_MID     = INTC_ADDR + 0x018,
    IO_ADDRESS_FLAGS_HI     = INTC_ADDR + 0x020,
    IO_ADDRESS_RAWFLAGS_HI  = INTC_ADDR + 0x024,
    IO_ADDRESS_MASK_HI      = INTC_ADDR + 0x028,
};

#define HW_INTC_FLAGS        flags[reg_idx]
#define HW_INTC_FLAGS_LO     flags[0]
#define HW_INTC_FLAGS_MID    flags[1]
#define HW_INTC_FLAGS_HI     flags[2]
#define HW_INTC_RAWFLAGS     raw_flags[reg_idx]
#define HW_INTC_RAWFLAGS_LO  raw_flags[0]
#define HW_INTC_RAWFLAGS_MID raw_flags[1]
#define HW_INTC_RAWFLAGS_HI  raw_flags[2]
#define HW_INTC_MASK         mask[reg_idx]
#define HW_INTC_MASK_LO      mask[0]
#define HW_INTC_MASK_MID     mask[1]
#define HW_INTC_MASK_HI      mask[2]

Intc::Intc(const char* intc_name, hw::allegrex::Allegrex* cpu)
    : logger(spdlog::stdout_color_st(std::string(intc_name))), cpu(cpu) {

}

Intc::~Intc() {

}

void Intc::check_pending_interrupts() {
    if (
        ((HW_INTC_FLAGS_LO  & HW_INTC_MASK_LO ) != 0) ||
        ((HW_INTC_FLAGS_MID & HW_INTC_MASK_MID) != 0) ||
        ((HW_INTC_FLAGS_HI  & HW_INTC_MASK_HI ) != 0)
    ) {
        cpu->assert_interrupt();
    } else {
        cpu->clear_interrupt();
    }
}

u32 Intc::read(const u32 addr) const {
    switch (addr) {
        case IoAddress::IO_ADDRESS_FLAGS_LO:
            logger->debug("FLAGS_LO read32");
            return HW_INTC_FLAGS_LO;
        case IoAddress::IO_ADDRESS_RAWFLAGS_LO:
            logger->debug("RAWFLAGS_LO read32");
            return HW_INTC_RAWFLAGS_LO;
        case IoAddress::IO_ADDRESS_MASK_LO:
            logger->debug("MASK_LO read32");
            return HW_INTC_MASK_LO;
        case IoAddress::IO_ADDRESS_FLAGS_MID:
            logger->debug("FLAGS_MID read32");
            return HW_INTC_FLAGS_MID;
        case IoAddress::IO_ADDRESS_RAWFLAGS_MID:
            logger->debug("RAWFLAGS_MID read32");
            return HW_INTC_RAWFLAGS_MID;
        case IoAddress::IO_ADDRESS_MASK_MID:
            logger->debug("MASK_MID read32");
            return HW_INTC_MASK_MID;
        case IoAddress::IO_ADDRESS_FLAGS_HI:
            logger->debug("FLAGS_HI read32");
            return HW_INTC_FLAGS_HI;
        case IoAddress::IO_ADDRESS_RAWFLAGS_HI:
            logger->debug("RAWFLAGS_HI read32");
            return HW_INTC_RAWFLAGS_HI;
        case IoAddress::IO_ADDRESS_MASK_HI:
            logger->debug("MASK_HI read32");
            return HW_INTC_MASK_HI;
        default:
            logger->error("Unmapped read32 @ {:08X}", addr);
            exit(1);
    }
}

void Intc::write(const u32 addr, const u32 data) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_FLAGS_LO:
            logger->debug("FLAGS_LO write32 = {:08X}", data);

            HW_INTC_FLAGS_LO &= ~data;
            HW_INTC_RAWFLAGS_LO &= ~data;
            break;
        case IoAddress::IO_ADDRESS_MASK_LO:
            logger->debug("MASK_LO write32 = {:08X}", data);

            HW_INTC_MASK_LO = data;
            HW_INTC_FLAGS_LO |= (HW_INTC_MASK_LO & HW_INTC_RAWFLAGS_LO);
            break;
        case IoAddress::IO_ADDRESS_FLAGS_MID:
            logger->debug("FLAGS_MID write32 = {:08X}", data);

            HW_INTC_FLAGS_MID &= ~data;
            HW_INTC_RAWFLAGS_MID &= ~data;
            break;
        case IoAddress::IO_ADDRESS_MASK_MID:
            logger->debug("MASK_MID write32 = {:08X}", data);

            HW_INTC_MASK_MID = data;
            HW_INTC_FLAGS_MID |= (HW_INTC_MASK_MID & HW_INTC_RAWFLAGS_MID);
            break;
        case IoAddress::IO_ADDRESS_FLAGS_HI:
            logger->debug("FLAGS_HI write32 = {:08X}", data);

            HW_INTC_FLAGS_HI &= ~data;
            HW_INTC_RAWFLAGS_HI &= ~data;
            break;
        case IoAddress::IO_ADDRESS_MASK_HI:
            logger->debug("MASK_HI write32 = {:08X}", data);

            HW_INTC_MASK_HI = data;
            HW_INTC_FLAGS_HI |= (HW_INTC_MASK_HI & HW_INTC_RAWFLAGS_HI);
            break;
        default:
            logger->error("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            exit(1);
    }

    check_pending_interrupts();
}

void Intc::assert_interrupt(const int intr_num) {
    logger->debug("Interrupt {} asserted", intr_num);

    const int reg_idx = intr_num >> 5;
    const int intr_bit = 1 << (intr_num & 31);

    assert((u64)reg_idx < NUM_REGS);

    HW_INTC_RAWFLAGS |= intr_bit;

    if ((HW_INTC_MASK & intr_bit) != 0) {
        HW_INTC_FLAGS |= intr_bit;

        cpu->assert_interrupt();
    }
}

void Intc::clear_interrupt(const int intr_num) {
    logger->debug("Interrupt {} cleared", intr_num);

    const int reg_idx = intr_num >> 5;
    const int intr_bit = 1 << (intr_num & 31);

    assert((u64)reg_idx < NUM_REGS);

    HW_INTC_FLAGS &= ~intr_bit;
    HW_INTC_RAWFLAGS &= ~intr_bit;
    
    check_pending_interrupts();
}

static struct {} ctx;

static std::array<Intc, NUM_INTCS> intcs = {
    Intc("INTC", kanacore::get_sc_ptr()),
    Intc("ME INTC", kanacore::get_me_ptr()),
};

template<int intc_num>
static u32 read(const u32 addr) {
    static_assert(intc_num < NUM_INTCS);

    return intcs[intc_num].read(addr);
}

template<int intc_num>
static void write(const u32 addr, const u32 data) {
    static_assert(intc_num < NUM_INTCS);

    intcs[intc_num].write(addr, data);
}

template<int intc_num>
static void map() {
    static_assert(intc_num < NUM_INTCS);

    const bus::PageDescriptor page_desc {
        // To my knowledge, INTC I/O is never not read/written using 32-bit accesses
        .read32_func  = read<intc_num>,
        .write32_func = write<intc_num>,
    };

    intcs[intc_num].cpu->get_bus_ptr()->map(INTC_ADDR, INTC_SIZE, page_desc);
}

void initialize() {
    std::memset(&ctx, 0, sizeof(ctx));
}

void soft_reset() {
    
}

void hard_reset() {
    map<0>();
    map<1>();
}

void shutdown() {

}

void assert_sc_interrupt(const int intr_num) {
    intcs[0].assert_interrupt(intr_num);
}

void assert_me_interrupt(const int intr_num) {
    intcs[1].assert_interrupt(intr_num);
}

void clear_sc_interrupt(const int intr_num) {
    intcs[0].clear_interrupt(intr_num);
}

void clear_me_interrupt(const int intr_num) {
    intcs[1].clear_interrupt(intr_num);
}

};
