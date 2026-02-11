/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/allegrex/allegrex.hpp - ALLEGREX CPU */

#pragma once

#include <memory>

#include <spdlog/spdlog.h>

#include <common/types.hpp>

namespace kanacore::hw::allegrex {

enum CpuId {
    CPU_ID_SC = 0,
    CPU_ID_ME = 1,
};

struct RegisterFile {
    static constexpr common::u64 NUM_GPRS = 34;

    // 32 GPRs + LO/HI
    common::u32 gprs[NUM_GPRS];

    // Program counters
    common::u32 pc;
    common::u32 next_pc;
};

// System Control coprocessor (because it's needed for a lot of things,
// we will integrate it in the Allegrex core)
struct Cp0 {
    static constexpr common::u64 NUM_REGS = 32;

    static constexpr const char* STATUS_REGISTER_NAMES[NUM_REGS] = {
        "N/A"     , "N/A"   , "N/A"     , "N/A"    ,
        "N/A"     , "N/A"   , "N/A"     , "N/A"    ,
        "BadVAddr", "Count" , "N/A"     , "Compare",
        "Status"  , "Cause" , "EPC"     , "PRId"   ,
        "Config"  , "N/A"   , "N/A"     , "N/A"    ,
        "N/A"     , "SCCode", "CPUId"   , "EBase"  ,
        "N/A"     , "N/A"   , "N/A"     , "N/A"    ,
        "TagLo"   , "TagHi" , "ErrorEPC", "N/A"    ,
    };

    enum StatusRegister {
        STATUS_REGISTER_CONFIG = 0x10,
    };

    // 2^(12 + 2) = 16 KB
    static constexpr common::u32 CACHE_SIZE = 2;

    static constexpr common::u32 CONFIG = (CACHE_SIZE << 9) | (CACHE_SIZE << 6);

    // "Free real estate"
    common::u32 control_regs[NUM_REGS];
};

struct Allegrex {
private:
    std::shared_ptr<spdlog::logger> logger;

    CpuId cpu_id;

    RegisterFile regfile;
    Cp0 cp0;

    common::u32 instr_addr;
    common::i64 cycles;

public:
    Allegrex(const CpuId cpu_id);
    ~Allegrex();

    CpuId get_cpu_id() const {
        return cpu_id;
    }

    std::shared_ptr<spdlog::logger> get_logger() {
        return logger;
    }

    common::i64* get_cycles() {
        return &cycles;
    }

    bool is_media_engine() const {
        return cpu_id == CpuId::CPU_ID_ME;
    }

    void soft_reset();
    void hard_reset();

    void dump_state();

    bool in_delay_slot() const;

    void jump(const common::u32 target);
    void delayed_jump(const common::u32 target);

    template<bool is_branch_likely>
    void branch(const common::u32 target, const bool condition, const common::u32 link_idx);

    common::u32 get_reg(const common::u32 idx) const;
    void set_reg(const common::u32 idx, const common::u32 data);

    common::u32 get_pc() const;

    // CP0 handlers (control and status registers)
    void set_control_reg(const common::u32 idx, const common::u32 data);

    common::u32 get_status_reg(const common::u32 idx) const;
    void set_status_reg(const common::u32 idx, const common::u32 data);

    template<typename T>
    T read(const common::u32 addr);

    // Read handlers need to be set up externally
    common::u8  (*read8 )(const common::u32 addr);
    common::u16 (*read16)(const common::u32 addr);
    common::u32 (*read32)(const common::u32 addr);

    template<typename T>
    void write(const common::u32 addr, const T data);

    // Write handlers need to be set up externally
    void (*write8 )(const common::u32 addr, const common::u8  data);
    void (*write16)(const common::u32 addr, const common::u16 data);
    void (*write32)(const common::u32 addr, const common::u32 data);

    common::u32 fetch_instr();

    common::u32 get_instr_addr() const {
        return instr_addr;
    }
};

};
