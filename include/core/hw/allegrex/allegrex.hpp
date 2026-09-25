/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/allegrex/allegrex.hpp - ALLEGREX CPU */

#pragma once

#include <memory>

#include <spdlog/spdlog.h>

#include <common/types.hpp>
#include <core/hw/bus.hpp>

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
        "24"      , "EBase" , "N/A"     , "N/A"    ,
        "TagLo"   , "TagHi" , "ErrorEPC", "N/A"    ,
    };

    enum StatusRegister {
        STATUS_REGISTER_COUNT    = 0x09,
        STATUS_REGISTER_COMPARE  = 0x0B,
        STATUS_REGISTER_STATUS   = 0x0C,
        STATUS_REGISTER_CAUSE    = 0x0D,
        STATUS_REGISTER_EPC      = 0x0E,
        STATUS_REGISTER_CONFIG   = 0x10,
        STATUS_REGISTER_SCCODE   = 0x15,
        STATUS_REGISTER_CPUID    = 0x16,
        STATUS_REGISTER_EBASE    = 0x19,
        STATUS_REGISTER_TAGLO    = 0x1C,
        STATUS_REGISTER_TAGHI    = 0x1D,
        STATUS_REGISTER_ERROREPC = 0x1E,
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
        EXCEPTION_CODE_INTERRUPT    = 0x00,
        EXCEPTION_CODE_SYSCALL      = 0x08,
        EXCEPTION_CODE_BREAKPOINT   = 0x09,
        EXCEPTION_CODE_COP_UNUSABLE = 0x0B,
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

    common::u32 compare;
    common::u32 count;
    common::u32 epc;
    common::u32 sccode;
    common::u32 ebase;
    common::u32 taglo;
    common::u32 taghi;
    common::u32 error_epc;

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

    bool cond;
};

// CP2
struct Vfpu {
    static constexpr common::u64 NUM_REGS = 128;

    enum class MatrixType {
        Scalar,
        PairVector,
        TripleVector,
        QuadVector,
        PairMatrix,
        TripleMatrix,
        QuadMatrix,
    };

    static constexpr common::u32 REVISION = 0;

    common::VfpuFloat matrixfile[NUM_REGS];

    struct {
        common::u32 source, target, destination;
    } prefix_stack;

    common::u32 cond;
    common::u32 internal;

    union {
        common::u32 raw;
        common::f32 flt;
    } prng_ctx[8];
};

struct Allegrex {
private:
    std::shared_ptr<spdlog::logger> logger;

    CpuId cpu_id;

    RegisterFile regfile;
    Cp0 cp0;
    Fpu fpu;
    Vfpu vfpu;

    common::u32 instr_addr;
    common::i64 cycles;
    common::i64 target_timestamp;

    bool delay_slot_pending;
    bool in_delay_slot;
    bool load_linked;

    bus::Bus bus;

    enum class CpuState {
        Run,
        WaitForInterrupt
    } state;

    bool is_interrupt_pending() const;

    common::u32 get_count() const;

    void clear_count_interrupt();
    void reschedule_count();

    common::u32 event_id;

    void decorate(common::Vec4& vec, const common::u32 decorator);

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

    void initialize();
    void soft_reset();
    void hard_reset();

    void dump_state();

    void jump(common::u32 target);
    void delayed_jump(common::u32 target);

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
    void assert_count_interrupt();
    void clear_interrupt();

    inline bool is_load_linked() const {
        return load_linked;
    }

    inline void set_load_linked(const bool is_load_linked) {
        load_linked = is_load_linked;
    }

    inline bool is_coprocessor_usable(const int idx) {
        // Force CP0 to be usable for now
        if (idx == 0) {
            return true;
        }

        if ((cp0.status.coprocessor_usable & (1 << idx)) == 0) {
            cp0.cause.coprocessor_error = idx;

            raise_lv1_exception(Cp0::ExceptionCode::EXCEPTION_CODE_COP_UNUSABLE);

            return false;
        }

        return true;
    }

    // FPU handlers (control registers, FGRs)
    common::u32 get_fpu_control_reg(const common::u32 idx) const;
    void set_fpu_control_reg(const common::u32 idx, const common::u32 data);

    common::f32 get_fgr(const common::u32 idx) const;
    common::u32 get_fgr_raw(const common::u32 idx) const;
    void set_fgr(const common::u32 idx, const common::f32 data);
    void set_fgr_raw(const common::u32 idx, const common::u32 data);

    void set_fpu_cond(const bool cond);
    bool get_fpu_cond() const;

    // VFPU handlers
    common::u32 get_vfpu_control_reg(const common::u32 idx);
    void set_vfpu_control_reg(const common::u32 idx, const common::u32 data);

    bool get_vfpu_cond(const common::u32 idx) const;

    void decorate_src(common::Vec4& vec);
    void decorate_tgt(common::Vec4& vec);
    void decorate_dst(common::Vec4& vec);
    void clear_decorators();

    template<Vfpu::MatrixType mtx_type>
    void get_matrix_file(const common::u32 code, common::VfpuFloat* flts) {
        const common::u32 matrix_bank = (code >> 2) & 7;

        common::u32 idx;
        common::u32 fsl;
        common::u32 num_rows, num_columns;

        bool is_row;

        switch (mtx_type) {
            case Vfpu::MatrixType::Scalar:
                is_row = false;

                idx = (code >> 0) & 3;
                fsl = (code >> 5) & 3;

                num_rows    = 1;
                num_columns = 1;
                break;
            case Vfpu::MatrixType::PairVector:
                is_row = ((code >> 5) & 1) != 0;

                idx = (code >> 0) & 3;
                fsl = (code >> 5) & 2;

                num_rows    = !is_row ? 2 : 1;
                num_columns =  is_row ? 2 : 1;
                break;
            case Vfpu::MatrixType::TripleVector:
                is_row = ((code >> 5) & 1) != 0;

                idx = (code >> 0) & 3;
                fsl = (code >> 6) & 1;

                num_rows    = !is_row ? 3 : 1;
                num_columns =  is_row ? 3 : 1;
                break;
            case Vfpu::MatrixType::QuadVector:
                is_row = ((code >> 5) & 1) != 0;

                idx = (code >> 0) & 3;
                fsl = (code >> 5) & 2;

                num_rows    = !is_row ? 4 : 1;
                num_columns =  is_row ? 4 : 1;
                break;
            case Vfpu::MatrixType::PairMatrix:
                is_row = ((code >> 5) & 1) == 0;

                fsl = (code >> 0) & 3;
                idx = (code >> 5) & 2;

                num_rows    = 2;
                num_columns = 2;
                break;
            case Vfpu::MatrixType::TripleMatrix:
                is_row = ((code >> 5) & 1) == 0;

                fsl = (code >> 0) & 3;
                idx = (code >> 6) & 1;

                num_rows    = 3;
                num_columns = 3;
                break;
            case Vfpu::MatrixType::QuadMatrix:
                is_row = ((code >> 5) & 1) == 0;

                fsl = (code >> 0) & 3;
                idx = (code >> 5) & 2;

                num_rows    = 4;
                num_columns = 4;
                break;
        }

        common::u32 k = 0;

        if (is_row) {
            for (common::u32 i = 0; i < num_rows; i++) {
                for (common::u32 j = 0; j < num_columns; j++) {
                    flts[k + j] = vfpu.matrixfile[4 * matrix_bank + ((fsl + j) & 3) + 32 * ((idx + i) & 3)];
                }

                k += 4;
            }
        } else {
            for (common::u32 j = 0; j < num_columns; j++) {
                for (common::u32 i = 0; i < num_rows; i++) {
                    flts[k + i] = vfpu.matrixfile[4 * matrix_bank + ((idx + j) & 3) + 32 * ((fsl + i) & 3)];
                }

                k += 4;
            }
        }
    }

    template<Vfpu::MatrixType mtx_type>
    void set_matrix_file(const common::u32 code, const common::VfpuFloat* flts, const bool* write_mask = nullptr) {
        const common::u32 matrix_bank = (code >> 2) & 7;

        common::u32 idx;
        common::u32 fsl;
        common::u32 num_rows, num_columns;

        bool is_row;

        switch (mtx_type) {
            case Vfpu::MatrixType::Scalar:
                is_row = false;

                idx = (code >> 0) & 3;
                fsl = (code >> 5) & 3;

                num_rows    = 1;
                num_columns = 1;
                break;
            case Vfpu::MatrixType::PairVector:
                is_row = ((code >> 5) & 1) != 0;

                idx = (code >> 0) & 3;
                fsl = (code >> 5) & 2;

                num_rows    = !is_row ? 2 : 1;
                num_columns =  is_row ? 2 : 1;
                break;
            case Vfpu::MatrixType::TripleVector:
                is_row = ((code >> 5) & 1) != 0;

                idx = (code >> 0) & 3;
                fsl = (code >> 6) & 1;

                num_rows    = !is_row ? 3 : 1;
                num_columns =  is_row ? 3 : 1;
                break;
            case Vfpu::MatrixType::QuadVector:
                is_row = ((code >> 5) & 1) != 0;

                idx = (code >> 0) & 3;
                fsl = (code >> 5) & 2;

                num_rows    = !is_row ? 4 : 1;
                num_columns =  is_row ? 4 : 1;
                break;
            case Vfpu::MatrixType::PairMatrix:
                is_row = ((code >> 5) & 1) == 0;

                fsl = (code >> 0) & 3;
                idx = (code >> 5) & 2;

                num_rows    = 2;
                num_columns = 2;
                break;
            case Vfpu::MatrixType::TripleMatrix:
                is_row = ((code >> 5) & 1) == 0;

                fsl = (code >> 0) & 3;
                idx = (code >> 6) & 1;

                num_rows    = 3;
                num_columns = 3;
                break;
            case Vfpu::MatrixType::QuadMatrix:
                is_row = ((code >> 5) & 1) == 0;

                fsl = (code >> 0) & 3;
                idx = (code >> 5) & 2;

                num_rows    = 4;
                num_columns = 4;
                break;
        }

        common::u32 k = 0;

        if (is_row) {
            for (common::u32 i = 0; i < num_rows; i++) {
                for (common::u32 j = 0; j < num_columns; j++) {
                    if ((write_mask == nullptr) || (!write_mask[k + j])) {
                        vfpu.matrixfile[4 * matrix_bank + ((fsl + j) & 3) + 32 * ((idx + i) & 3)] = flts[k + j];
                    }
                }

                k += 4;
            }
        } else {
            for (common::u32 j = 0; j < num_columns; j++) {
                for (common::u32 i = 0; i < num_rows; i++) {
                    if ((write_mask == nullptr) || (!write_mask[k + j])) {
                        vfpu.matrixfile[4 * matrix_bank + ((idx + j) & 3) + 32 * ((fsl + i) & 3)] = flts[k + i];
                    }
                }

                k += 4;
            }
        }
    }

    template<typename T>
    T read(const common::u32 addr);

    template<typename T>
    void write(const common::u32 addr, const T data);

    common::u32 fetch_instr();

    common::u32 get_instr_addr() const {
        return instr_addr;
    }

    bus::Bus* get_bus_ptr() {
        return &bus;
    }
};

};
