/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/allegrex/interpreter.cpp - ALLEGREX interpreter */

#include <core/hw/allegrex/interpreter.hpp>

#include <array>
#include <cstdlib>

#include <common/types.hpp>

#define OPCODE (instr >> 26) & 0x3F

namespace kanacore::hw::allegrex::interpreter {

using namespace common;

using Instruction = i64 (*)(Allegrex*, const u32);

constexpr u64 PRIMARY_TABLE_SIZE = 0x40;

static std::array<Instruction, PRIMARY_TABLE_SIZE> primary_table;

// Dummy instruction handler
static i64 i_undefined(Allegrex* cpu, const u32 instr) {
    cpu->get_logger()->error("Undefined primary instruction {:02X} ({:08X}) @ {:08X}", OPCODE, instr, cpu->get_instr_addr());
    exit(1);
}

void initialize() {
    primary_table.fill(i_undefined);
}

void soft_reset() {

}

void hard_reset() {

}

void shutdown() {

}

void run(Allegrex* cpu) {
    if (cpu->get_cpu_id() == CpuId::CPU_ID_ME) {
        cpu->get_logger()->error("Media Engine not implemented");
        exit(1);
    }

    while (*cpu->get_cycles() > 0) {
        const u32 instr = cpu->fetch_instr();

        primary_table[OPCODE](cpu, instr);
    }
};

}
