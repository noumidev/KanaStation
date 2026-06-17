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
        "N/A"     , "SCCode", "CPUId"   , "N/A"    ,
        "N/A"     , "EBase" , "N/A"     , "N/A"    ,
        "TagLo"   , "TagHi" , "ErrorEPC", "N/A"    ,
    };

    enum StatusRegister {
        STATUS_REGISTER_COUNT  = 0x09,
        STATUS_REGISTER_STATUS = 0x0C,
        STATUS_REGISTER_CAUSE  = 0x0D,
        STATUS_REGISTER_EPC    = 0x0E,
        STATUS_REGISTER_CONFIG = 0x10,
        STATUS_REGISTER_SCCODE = 0x15,
        STATUS_REGISTER_EBASE  = 0x19,
        STATUS_REGISTER_TAGLO  = 0x1C,
        STATUS_REGISTER_TAGHI  = 0x1D,
    };


    static constexpr common::u64 NUM_EXCODES = 32;

    static constexpr const char* EXCEPTION_CODE_NAMES[NUM_EXCODES] = {
        "Interrupt"         , "N/A"                , "N/A"                  , "N/A"                 ,
        "Load address error", "Store address error", "Instruction bus error", "Data bus error"      ,
        "Syscall"           , "Breakpoint"         , "Reserved instruction" , "Coprocessor unusable",
        "Overflow"          , "N/A"                , "N/A"                  , "FPU"                 ,
        "N/A"               , "N/A"                , "N/A"                  , "N/A"                 ,
        "N/A"               , "N/A"                , "N/A"                  , "N/A"                 ,
        "N/A"               , "N/A"                , "N/A"                  , "N/A"                 ,
        "N/A"               , "N/A"                , "N/A"                  , "N/A"                 ,
    };

    enum ExceptionCode {
        EXCEPTION_CODE_INTERRUPT = 0x00,
        EXCEPTION_CODE_SYSCALL   = 0x08,
    };

    // 2^(12 + 2) = 16 KB
    static constexpr common::u32 CACHE_SIZE = 2;

    static constexpr common::u32 CONFIG = (CACHE_SIZE << 9) | (CACHE_SIZE << 6);

    // "Free real estate"
    common::u32 control_regs[NUM_REGS];

    union {
        common::u32 raw;

        struct {
            common::u32 interrupt_enable   : 1;
            common::u32 exception_level    : 1;
            common::u32 error_level        : 1;
            common::u32 mode               : 2;
            common::u32                    : 3;
            common::u32 interrupt_mask     : 8;
            common::u32                    : 4;
            common::u32 software_reset     : 1;
            common::u32                    : 1;
            common::u32 bootstrap_vectors  : 1;
            common::u32                    : 2;
            common::u32 reverse_endian     : 1;
            common::u32                    : 2;
            common::u32 coprocessor_usable : 4;
        };
    } status;

    union {
        common::u32 raw;

        struct {
            common::u32                   : 2;
            common::u32 exception_code    : 6;
            common::u32 interrupt_flags   : 8;
            common::u32                   : 12;
            common::u32 coprocessor_error : 2;
            common::u32                   : 1;
            common::u32 in_delay_slot     : 1;
        };
    } cause;

    common::u32 count;
    common::u32 epc;
    common::u32 sccode;
    common::u32 ebase;
    common::u32 taglo;
    common::u32 taghi;

    common::i64 count_timestamp;
};

// CP1
struct Fpu {
    static constexpr common::u64 NUM_REGS = 32;

    union {
        common::u32 raw;
        common::f32 flt;
    } fgrs[NUM_REGS];

    union {
        common::u32 raw;

        struct {
            common::u32 rounding_mode : 2;
            common::u32 flags         : 5;
            common::u32 enables       : 5;
            common::u32 cause         : 6;
            common::u32               : 5;
            common::u32 condition     : 1;
            common::u32 flush_denorm  : 1;
            common::u32               : 7;
        };
    } status;
};

struct Allegrex {
private:
    std::shared_ptr<spdlog::logger> logger;

    CpuId cpu_id;

    RegisterFile regfile;
    Cp0 cp0;
    Fpu fpu;

    common::u32 instr_addr;
    common::i64 cycles;
    common::i64 target_timestamp;

    bool delay_slot_pending;
    bool in_delay_slot;

    enum class CpuState {
        Run,
        WaitForInterrupt
    } state;

    bool is_interrupt_pending() const;

public:
    Allegrex(const CpuId cpu_id);
    ~Allegrex();

    bool trace;

    CpuId get_cpu_id() const {
        return cpu_id;
    }

    std::shared_ptr<spdlog::logger> get_logger() {
        return logger;
    }

    common::i64* get_cycles() {
        return &cycles;
    }

    common::i64* get_target_timestamp() {
        return &target_timestamp;
    }

    bool is_media_engine() const {
        return cpu_id == CpuId::CPU_ID_ME;
    }

    bool is_running() const {
        return state == CpuState::Run;
    }

    void advance_delay_slot() {
        in_delay_slot = delay_slot_pending;
        delay_slot_pending = false;
    }

    void clear_delay_slot() {
        in_delay_slot = false;
        delay_slot_pending = false;
    }

    void soft_reset();
    void hard_reset();

    void dump_state();

    void jump(const common::u32 target);
    void delayed_jump(const common::u32 target);

    template<bool is_branch_likely>
    void branch(const common::u32 target, const bool condition, const common::u32 link_idx);

    common::u32 get_reg(const common::u32 idx) const;
    void set_reg(const common::u32 idx, const common::u32 data);

    common::u32 get_pc() const;

    // CP0 handlers (control and status registers)
    common::u32 get_control_reg(const common::u32 idx) const;
    void set_control_reg(const common::u32 idx, const common::u32 data);

    common::u32 get_status_reg(const common::u32 idx) const;
    void set_status_reg(const common::u32 idx, const common::u32 data);

    // CP0 Status register
    common::u32 status_get_ic() const;
    void status_set_ic(const common::u32 data);

    common::u32 get_exception_pc();
    void raise_lv1_exception(const Cp0::ExceptionCode excode);
    void return_from_exception();
    void set_syscall_code(const common::u32 sccode);
    void wait_for_interrupt();
    void assert_interrupt();
    void clear_interrupt();

    // FPU handlers (control registers, FGRs)
    common::u32 get_fpu_control_reg(const common::u32 idx) const;
    void set_fpu_control_reg(const common::u32 idx, const common::u32 data);

    common::f32 get_fgr(const common::u32 idx) const;
    common::u32 get_fgr_raw(const common::u32 idx) const;
    void set_fgr(const common::u32 idx, const common::f32 data);
    void set_fgr_raw(const common::u32 idx, const common::u32 data);

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
