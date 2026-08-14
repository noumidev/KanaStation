/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/allegrex/allegrex.cpp - ALLEGREX CPU */

#include <core/hw/allegrex/allegrex.hpp>

#include <cassert>
#include <cstdlib>
#include <cmath>
#include <unordered_set>

#include <spdlog/sinks/stdout_color_sinks.h>

#include <core/kanacore.hpp>
#include <core/scheduler.hpp>
#include <core/hw/bus.hpp>
#include <core/hw/intc.hpp>

namespace kanacore::hw::allegrex {
    
using namespace common;

constexpr bool SILENT_JUMPS = true;

constexpr u32 BOOT_EXCEPTION_ADDR = 0xBFC00000;

// A little hacky, but it should be fine
static void assert_count_interrupt_impl(const int cpu_id) {
    Allegrex* cpu = (cpu_id == CpuId::CPU_ID_SC) ? kanacore::get_sc_ptr() : kanacore::get_me_ptr();

    cpu->assert_count_interrupt();
}

Allegrex::Allegrex(const CpuId cpu_id) : cpu_id(cpu_id), bus(cpu_id == CPU_ID_SC ? "Bus" : "ME Bus") {
    logger = spdlog::stdout_color_st(cpu_id == CPU_ID_SC ? "SC" : "ME");
}

Allegrex::~Allegrex() {

}

bool Allegrex::is_interrupt_pending() const {
    /* logger->debug("IE: {}, EXL: {}, ERL: {}, IM: {:08b}, IF: {:08b}",
        cp0.status.interrupt_enable,
        cp0.status.exception_level,
        cp0.status.error_level,
        cp0.status.interrupt_mask,
        cp0.cause.interrupt_flags
    ); */

    return
        cp0.status.interrupt_enable &&
        !cp0.status.exception_level &&
        !cp0.status.error_level &&
        ((cp0.status.interrupt_mask & cp0.cause.interrupt_flags) != 0);
}

u32 Allegrex::get_count() const {
    return cp0.count + (cycles - cp0.count_timestamp);
}

void Allegrex::assert_count_interrupt() {
    cp0.cause.interrupt_flags |= 1 << 7;

    if (is_interrupt_pending()) {
        raise_lv1_exception(Cp0::ExceptionCode::EXCEPTION_CODE_INTERRUPT);
    }
}

void Allegrex::reschedule_count() {
    if (cp0.compare > get_count()) {
        const int cpu_id = get_cpu_id();

        scheduler::schedule_event(
            (cpu_id == CpuId::CPU_ID_SC) ? scheduler::EventType::COUNT_SC : scheduler::EventType::COUNT_ME,
            assert_count_interrupt_impl,
            cpu_id,
            cp0.compare - get_count(),
            true
        );
    }
}

void Allegrex::clear_count_interrupt() {
    cp0.cause.interrupt_flags &= ~(1 << 7);
}

void reschedule_count() {

}

template<typename T>
T Allegrex::read(const u32 addr) {
    if ((addr & (sizeof(T) - 1)) != 0) {
        logger->error("Unaligned read{} address {:08X}", 8 * sizeof(T), addr);
        exit(1);
    }

    // TODO: properly handle memory segments
    const u32 masked_addr = addr & 0x1FFFFFFF;
    
    return bus.read<T>(masked_addr);
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

    bus.write<T>(masked_addr, data);
}

template void Allegrex::write(const u32, const u8);
template void Allegrex::write(const u32, const u16);
template void Allegrex::write(const u32, const u32);

void Allegrex::soft_reset() {
    // This has a bit more to it than this, but for now this should suffice
    state = CpuState::Run;

    cp0.status.bootstrap_vectors = 1;
    cp0.status.software_reset = 1;

    jump(BOOT_EXCEPTION_ADDR);

    scheduler::cancel_event(
        (get_cpu_id() == CpuId::CPU_ID_SC) ? scheduler::EventType::COUNT_SC : scheduler::EventType::COUNT_ME
    );
}

void Allegrex::hard_reset() {
    regfile = RegisterFile{};
    cp0 = Cp0{};

    state = CpuState::Run;

    cp0.status.bootstrap_vectors = 1;
    cp0.status.software_reset = 0;

    jump(BOOT_EXCEPTION_ADDR);

    scheduler::cancel_event(
        (get_cpu_id() == CpuId::CPU_ID_SC) ? scheduler::EventType::COUNT_SC : scheduler::EventType::COUNT_ME
    );
}

void Allegrex::dump_state() {
    constexpr const char *REGISTER_NAMES[34] = {
        "$r0", "$at", "$v0", "$v1", "$a0", "$a1", "$a2", "$a3",
        "$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7",
        "$s0", "$s1", "$s2", "$s3", "$s4", "$s5", "$s6", "$s7",
        "$t8", "$t9", "$k0", "$k1", "$gp", "$sp", "$s8", "$ra",
        "$lo", "$hi",
    };

    for (u64 i = 0; i < RegisterFile::NUM_GPRS; i += 4) {
        if (i < 32) {
            logger->info(
            "{}: {:08X}  {}: {:08X}  {}: {:08X}  {}: {:08X}",
                REGISTER_NAMES[i + 0], get_reg(i + 0),
                REGISTER_NAMES[i + 1], get_reg(i + 1),
                REGISTER_NAMES[i + 2], get_reg(i + 2),
                REGISTER_NAMES[i + 3], get_reg(i + 3)
            );
        } else {
            logger->info(
            "{}: {:08X}  {}: {:08X}",
                REGISTER_NAMES[i + 0], get_reg(i + 0),
                REGISTER_NAMES[i + 1], get_reg(i + 1)
            );
        }
    }
}

void Allegrex::jump(u32 target) {
    static std::unordered_set<u32> jump_targets;

    // ALLEGREX doesn't actually seem to throw exceptions on unaligned accesses.
    // The firmware deliberately sets the KIRK interrupt handler to an unaligned
    // address, so let's just align PC
    target &= ~3;

    if constexpr (!SILENT_JUMPS) {
        if (jump_targets.find(target) == jump_targets.end()) {
            logger->info("Jump @ {:08X} to {:08X}", instr_addr, target);

            jump_targets.insert(target);
        }
    }

    regfile.pc = target;
    regfile.next_pc = target + sizeof(u32);

    clear_delay_slot();
}

void Allegrex::delayed_jump( u32 target) {
    static std::unordered_set<u32> delayed_targets;

    // See above
    target &= ~3;

    if constexpr (!SILENT_JUMPS) {
        if (delayed_targets.find(target) == delayed_targets.end()) {
            logger->info("Delayed jump @ {:08X} to {:08X}", instr_addr, target);

            delayed_targets.insert(target);
        }
    }

    regfile.next_pc = target;
}

template<bool is_branch_likely>
void Allegrex::branch(const u32 target, const bool condition, const u32 link_idx) {
    assert(link_idx < RegisterFile::NUM_GPRS);

    if (in_delay_slot) {
        logger->error("Branch in delay slot");
        exit(1);
    }

    set_reg(link_idx, regfile.next_pc);

    delay_slot_pending = true;

    if (condition) {
        delayed_jump(target);
    } else if constexpr (is_branch_likely) {
        // Skip the instruction in the delay slot
        jump(regfile.next_pc);
    }
}

template void Allegrex::branch<false>(const u32, const bool, const u32);
template void Allegrex::branch<true >(const u32, const bool, const u32);

u32 Allegrex::get_reg(const u32 idx) const {
    assert(idx < RegisterFile::NUM_GPRS);

    return regfile.gprs[idx];
}

void Allegrex::set_reg(const u32 idx, const u32 data) {
    assert(idx < RegisterFile::NUM_GPRS);

    regfile.gprs[idx] = data;
    regfile.gprs[0]   = 0;
}

u32 Allegrex::get_pc() const {
    return regfile.pc;
}

u32 Allegrex::get_control_reg(const u32 idx) const {
    assert(idx < Cp0::NUM_REGS);

    return cp0.control_regs[idx];
}

void Allegrex::set_control_reg(const u32 idx, const u32 data) {
    assert(idx < Cp0::NUM_REGS);

    cp0.control_regs[idx] = data;
}

u32 Allegrex::get_status_reg(const u32 idx) const {
    assert(idx < Cp0::NUM_REGS);

    u32 data = 0;

    switch (idx) {
        case Cp0::StatusRegister::STATUS_REGISTER_COUNT:
            data = get_count();
            break;
        case Cp0::StatusRegister::STATUS_REGISTER_COMPARE:
            data = cp0.compare;
            break;
        case Cp0::StatusRegister::STATUS_REGISTER_STATUS:
            data = cp0.status.raw;
            break;
        case Cp0::StatusRegister::STATUS_REGISTER_CAUSE:
            data = cp0.cause.raw;
            break;
        case Cp0::StatusRegister::STATUS_REGISTER_EPC:
            data = cp0.epc;
            break;
        case Cp0::StatusRegister::STATUS_REGISTER_CONFIG:
            data = Cp0::CONFIG;
            break;
        case Cp0::StatusRegister::STATUS_REGISTER_SCCODE:
            data = cp0.sccode;
            break;
        case Cp0::StatusRegister::STATUS_REGISTER_CPUID:
            data = cpu_id;
            break;
        case Cp0::StatusRegister::STATUS_REGISTER_EBASE:
            data = cp0.ebase;
            break;
        case Cp0::StatusRegister::STATUS_REGISTER_TAGLO:
            data = cp0.taglo;
            break;
        case Cp0::StatusRegister::STATUS_REGISTER_TAGHI:
            data = cp0.taghi;
            break;
        case 24:
            // Later firmwares read this register. Bit 0 = 1 seems to trigger clearing
            // the L2 cache, which doesn't exist on early models?
            break;
        default:
            logger->error("Unimplemented read from CP0 status register {} ({})", Cp0::STATUS_REGISTER_NAMES[idx], idx);
            exit(1);
    }

    logger->debug("Read from CP0 status register {}", Cp0::STATUS_REGISTER_NAMES[idx]);
    return data;
}

void Allegrex::set_status_reg(const u32 idx, const u32 data) {
    assert(idx < Cp0::NUM_REGS);

    switch (idx) {
        case Cp0::StatusRegister::STATUS_REGISTER_COUNT:
            cp0.count = data;
            cp0.count_timestamp = cycles;

            reschedule_count();
            break;
        case Cp0::StatusRegister::STATUS_REGISTER_COMPARE:
            cp0.compare = data;

            reschedule_count();
            clear_count_interrupt();
            break;
        case Cp0::StatusRegister::STATUS_REGISTER_STATUS:
            cp0.status.raw = data;
            break;
        case Cp0::StatusRegister::STATUS_REGISTER_CAUSE:
            cp0.cause.raw = (cp0.cause.raw & 0xFFFFFCFF) | (data & 0x300);
            break;
        case Cp0::StatusRegister::STATUS_REGISTER_EPC:
            cp0.epc = data;
            break;
        case Cp0::StatusRegister::STATUS_REGISTER_EBASE:
            cp0.ebase = data;
            break;
        case Cp0::StatusRegister::STATUS_REGISTER_TAGLO:
            cp0.taglo = data;
            break;
        case Cp0::StatusRegister::STATUS_REGISTER_TAGHI:
            cp0.taghi = data;
            break;
        case Cp0::StatusRegister::STATUS_REGISTER_ERROREPC:
            cp0.error_epc = data;
            break;
        default:
            logger->error("Unimplemented write to CP0 status register {} = {:08X}", Cp0::STATUS_REGISTER_NAMES[idx], data);
            exit(1);
    }

    logger->debug("Write to CP0 status register {} = {:08X}", Cp0::STATUS_REGISTER_NAMES[idx], data);

    if (is_interrupt_pending()) {
        raise_lv1_exception(Cp0::ExceptionCode::EXCEPTION_CODE_INTERRUPT);
    }
}

u32 Allegrex::status_get_ic() const {
    return cp0.status.interrupt_enable;
}

void Allegrex::status_set_ic(const u32 data) {
    cp0.status.interrupt_enable = data & 1;

    if (is_interrupt_pending()) {
        raise_lv1_exception(Cp0::ExceptionCode::EXCEPTION_CODE_INTERRUPT);
    }
}

u32 Allegrex::get_exception_pc() {
    u32 epc;

    if (cp0.status.error_level) {
        logger->error("Unimplemented return from error");
        exit(1);
    } else if (cp0.status.exception_level) {
        epc = cp0.epc;

        cp0.status.exception_level = 0;
    } else {
        logger->error("Invalid exception level");
        exit(1);
    }

    return epc;
}

void Allegrex::raise_lv1_exception(const Cp0::ExceptionCode excode) {
    constexpr u32 LV1_VECTOR_BASE = 0xBFC00200;

    u32 epc;

    if (excode == Cp0::ExceptionCode::EXCEPTION_CODE_INTERRUPT) {
        // For our delay slot logic to work (since interrupts are
        // triggered at the end of an instruction for us), we need to manually
        // call this here
        advance_delay_slot();

        // EPC also needs to point to the *next* instruction
        epc = get_pc();

        state = CpuState::Run;
    } else {
        // All other general exceptions are synchronous and restart the
        // current instruction
        epc = get_instr_addr();
    }

    logger->debug("{} exception (PC: {:08X})", Cp0::EXCEPTION_CODE_NAMES[excode], epc);

    assert(!cp0.status.exception_level);

    cp0.cause.exception_code = excode;

    u32 vector_base;

    if (cp0.status.bootstrap_vectors) {
        vector_base = LV1_VECTOR_BASE;
    } else {
        vector_base = cp0.ebase;
    }

    cp0.cause.in_delay_slot = in_delay_slot;

    if (in_delay_slot) {
        // The CPU always returns to the branch preceding the delay slot
        cp0.epc = epc - sizeof(u32);
    } else {
        cp0.epc = epc;
    }

    cp0.status.exception_level = 1;

    jump(vector_base);
}

void Allegrex::return_from_exception() {
    const u32 epc = get_exception_pc();

    logger->debug("Returning from exception (EPC: {:08X})", epc);

    jump(epc);

    // Re-check for interrupts here
    if (is_interrupt_pending()) {
        raise_lv1_exception(Cp0::ExceptionCode::EXCEPTION_CODE_INTERRUPT);
    }

    set_load_linked(false);
}

void Allegrex::set_syscall_code(const u32 sccode) {
    cp0.sccode = sccode << 2;
}

void Allegrex::wait_for_interrupt() {
    // This will exit the interpreter loop
    cycles = target_timestamp;

    state = CpuState::WaitForInterrupt;

    // HALTing with the interrupt enable bit turned off forces it on, probably
    status_set_ic(1);
}

void Allegrex::assert_interrupt() {
    cp0.cause.interrupt_flags |= 1 << 2;

    if (is_interrupt_pending()) {
        raise_lv1_exception(Cp0::ExceptionCode::EXCEPTION_CODE_INTERRUPT);
    }
}

void Allegrex::clear_interrupt() {
    cp0.cause.interrupt_flags &= ~(1 << 2);
}

u32 Allegrex::get_fpu_control_reg(const u32 idx) const {
    switch (idx) {
        case 31:
            logger->debug("Read from FPU control register Status");
            return fpu.status.raw;
        default:
            logger->error("Unimplemented read from FPU control register {}", idx);
            exit(1);
    }
}

void Allegrex::set_fpu_control_reg(const u32 idx, const u32 data) {
    switch (idx) {
        case 31:
            logger->debug("Write to FPU control register Status = {:08X}", data);

            fpu.status.raw = data;
            break;
        default:
            logger->error("Unimplemented write to FPU control register {}", idx);
            exit(1);
    }
}

f32 Allegrex::get_fgr(const u32 idx) const {
    assert(idx < Fpu::NUM_REGS);

    const f32 data = fpu.fgrs[idx].flt;

    assert(!(isnan(data) || isinf(data)));

    return data;
}

u32 Allegrex::get_fgr_raw(const u32 idx) const {
    assert(idx < Fpu::NUM_REGS);

    return fpu.fgrs[idx].raw;
}

void Allegrex::set_fgr(const u32 idx, const f32 data) {
    assert(idx < Fpu::NUM_REGS);
    assert(!(isnan(data) || isinf(data)));

    fpu.fgrs[idx].flt = data;
}

void Allegrex::set_fgr_raw(const u32 idx, const u32 data) {
    assert(idx < Fpu::NUM_REGS);

    fpu.fgrs[idx].raw = data;
}

void Allegrex::set_fpu_cond(const bool cond) {
    fpu.cond = cond;
}

bool Allegrex::get_fpu_cond() const {
    return fpu.cond;
}

u32 Allegrex::fetch_instr() {
    // Update current instruction address
    instr_addr = regfile.pc;

    advance_delay_slot();

    const u32 instr = read<u32>(regfile.pc);

    regfile.pc = regfile.next_pc;
    regfile.next_pc += sizeof(u32);

    return instr;
}

};
