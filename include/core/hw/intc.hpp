/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/intc.hpp - Interrupt controller registers */

#pragma once

#include <memory>

#include <spdlog/spdlog.h>

#include <common/types.hpp>
#include <core/hw/allegrex/allegrex.hpp>

namespace kanacore::hw::intc {

class Intc {
private:
    constexpr static common::u64 NUM_REGS = 3;

    std::shared_ptr<spdlog::logger> logger;

    common::u32 flags[NUM_REGS];
    common::u32 raw_flags[NUM_REGS];
    common::u32 mask[NUM_REGS];

    void check_pending_interrupts();

public:
    Intc(const char* intc_name, hw::allegrex::Allegrex* cpu);
    ~Intc();

    hw::allegrex::Allegrex* cpu;

    common::u32 read(const common::u32 addr) const;
    void write(const common::u32 addr, const common::u32 data);

    void assert_interrupt(const int intr_num);
    void clear_interrupt(const int intr_num);
};

void initialize();
void soft_reset();
void hard_reset();
void shutdown();

void assert_sc_interrupt(const int intr_num);
void assert_me_interrupt(const int intr_num);
void clear_sc_interrupt(const int intr_num);
void clear_me_interrupt(const int intr_num);

};
