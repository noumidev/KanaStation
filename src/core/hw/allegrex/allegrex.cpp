/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/allegrex/allegrex.cpp - ALLEGREX CPU */

#include <core/hw/allegrex/allegrex.hpp>

#include <type_traits>

#include <spdlog/sinks/stdout_color_sinks.h>

namespace kanacore::hw::allegrex {
    
using namespace common;

constexpr u32 BOOT_EXCEPTION_ADDR = 0xBFC00000;

Allegrex::Allegrex(const CpuId cpu_id) : cpu_id(cpu_id) {
    logger = spdlog::stdout_color_st(cpu_id == CPU_ID_SC ? "SC" : "ME");
}

Allegrex::~Allegrex() {

}

template<typename T>
T Allegrex::read(const u32 addr) {
    if ((addr & (sizeof(T) - 1)) != 0) {
        logger->error("Unaligned read{} address {:08X}", 8 * sizeof(T), addr);
        exit(1);
    }

    // TODO: properly handle memory segments
    const u32 masked_addr = addr & 0x1FFFFFFF;

    if (std::is_same_v<T, u8>) {
        return read8(masked_addr);
    } else if (std::is_same_v<T, u16>) {
        return read16(masked_addr);
    } else {
        return read32(masked_addr);
    }
}

template u8  Allegrex::read(const u32);
template u16 Allegrex::read(const u32);
template u32 Allegrex::read(const u32);

template<typename T>
void Allegrex::write(const u32 addr, const T data) {
    if ((addr & (sizeof(T) - 1)) != 0) {
        logger->error("Unaligned write{} address {:08X}", 8 * sizeof(T), addr);
        exit(1);
    }

    // TODO: properly handle memory segments
    const u32 masked_addr = addr & 0x1FFFFFFF;

    if (std::is_same_v<T, u8>) {
        write8(masked_addr, data);
    } else if (std::is_same_v<T, u16>) {
        write16(masked_addr, data);
    } else {
        write32(masked_addr, data);
    }
}

template void Allegrex::write(const u32, const u8);
template void Allegrex::write(const u32, const u16);
template void Allegrex::write(const u32, const u32);

void Allegrex::soft_reset() {

}

void Allegrex::hard_reset() {
    regfile = RegisterFile{};

    jump(BOOT_EXCEPTION_ADDR);
}

bool Allegrex::in_delay_slot() const {
    // If next_pc is not 8 bytes ahead of the address of the
    // current instruction, we are in a delay slot.
    // This should work in most situations (we will get back to this when it doesn't work)
    return regfile.next_pc != (instr_addr + 2 * sizeof(u32));
}

void Allegrex::jump(const u32 target) {
    regfile.pc = target;
    regfile.next_pc = target + sizeof(u32);
}

void Allegrex::delayed_jump(const u32 target) {
    regfile.next_pc = target;
}

u32 Allegrex::fetch_instr() {
    // Update current instruction address
    instr_addr = regfile.pc;

    const u32 instr = read<u32>(regfile.pc);

    regfile.pc = regfile.next_pc;
    regfile.next_pc += sizeof(u32);

    return instr;
}

};
