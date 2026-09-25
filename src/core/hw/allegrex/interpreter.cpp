/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/allegrex/interpreter.cpp - ALLEGREX interpreter */

#include <core/hw/allegrex/interpreter.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdlib>
#include <random>

#include <common/types.hpp>

#define OPCODE ((instr >> 26) & 0x3F)
#define FUNCT  ((instr >>  0) & 0x3F)
#define RS     ((instr >> 21) & 0x1F)
#define RT     ((instr >> 16) & 0x1F)
#define FT     ((instr >> 16) & 0x1F)
#define VT     ((instr >> 16) & 0x7F)
#define RD     ((instr >> 11) & 0x1F)
#define FS     ((instr >> 11) & 0x1F)
#define SIZE   ((instr >> 11) & 0x1F)
#define VS     ((instr >>  8) & 0x7F)
#define FD     ((instr >>  6) & 0x1F)
#define SA     ((instr >>  6) & 0x1F)
#define POS    ((instr >>  6) & 0x1F)
#define VD     ((instr >>  0) & 0x7F)
#define UIMM   ((instr >>  0) & 0xFFFF)
#define TARGET ((instr >>  0) & 0x3FFFFFF)
#define SCCODE ((instr >>  6) & 0xFFFFF)

namespace kanacore::hw::allegrex::interpreter {

using namespace common;

using Instruction = i64 (*)(Allegrex*, const u32);

constexpr bool SILENT_CACHE = true;

constexpr u64 PRIMARY_TABLE_SIZE = 0x40;
constexpr u64 SPECIAL_TABLE_SIZE = 0x40;

enum Opcode {
    OPCODE_SPECIAL  = 0x00,
    OPCODE_REGIMM   = 0x01,
    OPCODE_J        = 0x02,
    OPCODE_JAL      = 0x03,
    OPCODE_BEQ      = 0x04,
    OPCODE_BNE      = 0x05,
    OPCODE_BLEZ     = 0x06,
    OPCODE_BGTZ     = 0x07,
    OPCODE_ADDI     = 0x08,
    OPCODE_ADDIU    = 0x09,
    OPCODE_SLTI     = 0x0A,
    OPCODE_SLTIU    = 0x0B,
    OPCODE_ANDI     = 0x0C,
    OPCODE_ORI      = 0x0D,
    OPCODE_XORI     = 0x0E,
    OPCODE_LUI      = 0x0F,
    OPCODE_COP0     = 0x10,
    OPCODE_COP1     = 0x11,
    OPCODE_COP2     = 0x12,
    OPCODE_BEQL     = 0x14,
    OPCODE_BNEL     = 0x15,
    OPCODE_BLEZL    = 0x16,
    OPCODE_BGTZL    = 0x17,
    OPCODE_VFPU0    = 0x18,
    OPCODE_VFPU1    = 0x19,
    OPCODE_MFVME    = 0x1A,
    OPCODE_VFPU3    = 0x1B,
    OPCODE_SPECIAL2 = 0x1C,
    OPCODE_SPECIAL3 = 0x1F,
    OPCODE_LB       = 0x20,
    OPCODE_LH       = 0x21,
    OPCODE_LWL      = 0x22,
    OPCODE_LW       = 0x23,
    OPCODE_LBU      = 0x24,
    OPCODE_LHU      = 0x25,
    OPCODE_LWR      = 0x26,
    OPCODE_SB       = 0x28,
    OPCODE_SH       = 0x29,
    OPCODE_SWL      = 0x2A,
    OPCODE_SW       = 0x2B,
    OPCODE_MTVME    = 0x2C,
    OPCODE_SWR      = 0x2E,
    OPCODE_CACHE    = 0x2F,
    OPCODE_LL       = 0x30,
    OPCODE_LWC1     = 0x31,
    OPCODE_LWC2     = 0x32,
    OPCODE_VFPU4    = 0x34,
    OPCODE_LQC2     = 0x36,
    OPCODE_VFPU5    = 0x37,
    OPCODE_SC       = 0x38,
    OPCODE_SWC1     = 0x39,
    OPCODE_SWC2     = 0x3A,
    OPCODE_VFPU6    = 0x3C,
    OPCODE_SQC2     = 0x3E,
    OPCODE_VFPU7    = 0x3F,
};

enum SpecialOpcode {
    SPECIAL_OPCODE_SLL     = 0x00,
    SPECIAL_OPCODE_SRL     = 0x02,
    SPECIAL_OPCODE_SRA     = 0x03,
    SPECIAL_OPCODE_SLLV    = 0x04,
    SPECIAL_OPCODE_SRLV    = 0x06,
    SPECIAL_OPCODE_SRAV    = 0x07,
    SPECIAL_OPCODE_JR      = 0x08,
    SPECIAL_OPCODE_JALR    = 0x09,
    SPECIAL_OPCODE_MOVZ    = 0x0A,
    SPECIAL_OPCODE_MOVN    = 0x0B,
    SPECIAL_OPCODE_SYSCALL = 0x0C,
    SPECIAL_OPCODE_BREAK   = 0x0D,
    SPECIAL_OPCODE_SYNC    = 0x0F,
    SPECIAL_OPCODE_MFHI    = 0x10,
    SPECIAL_OPCODE_MTHI    = 0x11,
    SPECIAL_OPCODE_MFLO    = 0x12,
    SPECIAL_OPCODE_MTLO    = 0x13,
    SPECIAL_OPCODE_CLZ     = 0x16,
    SPECIAL_OPCODE_MULT    = 0x18,
    SPECIAL_OPCODE_MULTU   = 0x19,
    SPECIAL_OPCODE_DIV     = 0x1A,
    SPECIAL_OPCODE_DIVU    = 0x1B,
    SPECIAL_OPCODE_MADD    = 0x1C,
    SPECIAL_OPCODE_ADD     = 0x20,
    SPECIAL_OPCODE_ADDU    = 0x21,
    SPECIAL_OPCODE_SUB     = 0x22,
    SPECIAL_OPCODE_SUBU    = 0x23,
    SPECIAL_OPCODE_AND     = 0x24,
    SPECIAL_OPCODE_OR      = 0x25,
    SPECIAL_OPCODE_XOR     = 0x26,
    SPECIAL_OPCODE_NOR     = 0x27,
    SPECIAL_OPCODE_SLT     = 0x2A,
    SPECIAL_OPCODE_SLTU    = 0x2B,
    SPECIAL_OPCODE_MAX     = 0x2C,
    SPECIAL_OPCODE_MIN     = 0x2D,
};

enum Special2Opcode {
    SPECIAL2_OPCODE_HALT = 0x00,
    SPECIAL2_OPCODE_MFIC = 0x24,
    SPECIAL2_OPCODE_MTIC = 0x26,
};

enum Special3Opcode {
    SPECIAL3_OPCODE_EXT   = 0x00,
    SPECIAL3_OPCODE_INS   = 0x04,
    SPECIAL3_OPCODE_BSHFL = 0x20,
};

enum RegimmOpcode {
    REGIMM_OPCODE_BLTZ   = 0x00,
    REGIMM_OPCODE_BGEZ   = 0x01,
    REGIMM_OPCODE_BLTZL  = 0x02,
    REGIMM_OPCODE_BGEZL  = 0x03,
    REGIMM_OPCODE_BLTZAL = 0x10,
    REGIMM_OPCODE_BGEZAL = 0x11,
};

enum CopOpcode {
    COP_OPCODE_MFC       = 0x00,
    COP_OPCODE_CFC       = 0x02,
    COP_OPCODE_MFVC      = 0x03,
    COP_OPCODE_MTC       = 0x04,
    COP_OPCODE_CTC       = 0x06,
    COP_OPCODE_MTVC      = 0x07,
    COP_OPCODE_BC        = 0x08,
    COP_OPCODE_SECONDARY = 0x10, // CP0
    COP_OPCODE_SINGLE    = 0x10, // FPU
    COP_OPCODE_WORD      = 0x14, // FPU
};

enum CopBranchOpcode {
    COP_BRANCH_OPCODE_BCF  = 0x00,
    COP_BRANCH_OPCODE_BCT  = 0x01,
    COP_BRANCH_OPCODE_BCFL = 0x02,
    COP_BRANCH_OPCODE_BCTL = 0x03,
};

enum Cp0Opcode {
    CP0_OPCODE_ERET = 0x18,
};

enum FpuOpcode {
    FPU_OPCODE_ADD    = 0x00,
    FPU_OPCODE_SUB    = 0x01,
    FPU_OPCODE_MUL    = 0x02,
    FPU_OPCODE_DIV    = 0x03,
    FPU_OPCODE_SQRT   = 0x04,
    FPU_OPCODE_ABS    = 0x05,
    FPU_OPCODE_MOV    = 0x06,
    FPU_OPCODE_NEG    = 0x07,
    FPU_OPCODE_ROUNDW = 0x0C,
    FPU_OPCODE_TRUNCW = 0x0D,
    FPU_OPCODE_CEILW  = 0x0E,
    FPU_OPCODE_CVTS   = 0x20,
    FPU_OPCODE_C      = 0x30,
};

enum BitShuffleOpcode {
    BIT_SHUFFLE_OPCODE_WSBH   = 0x02,
    BIT_SHUFFLE_OPCODE_WSBW   = 0x03,
    BIT_SHUFFLE_OPCODE_SEB    = 0x10,
    BIT_SHUFFLE_OPCODE_BITREV = 0x14,
    BIT_SHUFFLE_OPCODE_SEH    = 0x18,
};

enum CopNum {
    COP_NUM_CP0,
    COP_NUM_FPU,
    COP_NUM_VFPU,
};

static std::array<Instruction, PRIMARY_TABLE_SIZE> primary_table;
static std::array<Instruction, SPECIAL_TABLE_SIZE> special_table;

// Is identical to ADDIU as far as I remember, so...
// static i64 i_addi(Allegrex* cpu, const u32 instr) { ... }

static i64 i_addiu(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RT, cpu->get_reg(RS) + (i32)(i16)UIMM);
    return 1;
}

// Is identical to ADDU as far as I remember, so...
// static i64 i_add(Allegrex* cpu, const u32 instr) {

static i64 i_addu(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RD, cpu->get_reg(RS) + cpu->get_reg(RT));
    return 1;
}

static i64 i_and(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RD, cpu->get_reg(RS) & cpu->get_reg(RT));
    return 1;
}

static i64 i_andi(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RT, cpu->get_reg(RS) & UIMM);
    return 1;
}

template<int cop_num>
static i64 i_bcf(Allegrex* cpu, const u32 instr) {
    bool cond;

    switch (cop_num) {
        case CopNum::COP_NUM_FPU:
            cond = cpu->get_fpu_cond();
            break;
        case CopNum::COP_NUM_VFPU:
            cond = cpu->get_vfpu_cond(VT >> 2);
            break;
        default:
            cpu->get_logger()->error("Unimplemented CP{} for BCF", cop_num);
            exit(1);
    }

    cpu->branch<false>(cpu->get_pc() + ((i32)(i16)UIMM << 2), !cond, 0);
    return 1;
}

template<int cop_num>
static i64 i_bcfl(Allegrex* cpu, const u32 instr) {
    bool cond;

    switch (cop_num) {
        case CopNum::COP_NUM_FPU:
            cond = cpu->get_fpu_cond();
            break;
        case CopNum::COP_NUM_VFPU:
            cond = cpu->get_vfpu_cond(VT >> 2);
            break;
        default:
            cpu->get_logger()->error("Unimplemented CP{} for BCFL", cop_num);
            exit(1);
    }

    cpu->branch<true>(cpu->get_pc() + ((i32)(i16)UIMM << 2), !cond, 0);
    return 1;
}

template<int cop_num>
static i64 i_bct(Allegrex* cpu, const u32 instr) {
    bool cond;

    switch (cop_num) {
        case CopNum::COP_NUM_FPU:
            cond = cpu->get_fpu_cond();
            break;
        case CopNum::COP_NUM_VFPU:
            cond = cpu->get_vfpu_cond(VT >> 2);
            break;
        default:
            cpu->get_logger()->error("Unimplemented CP{} for BCT", cop_num);
            exit(1);
    }

    cpu->branch<false>(cpu->get_pc() + ((i32)(i16)UIMM << 2), cond, 0);
    return 1;
}

template<int cop_num>
static i64 i_bctl(Allegrex* cpu, const u32 instr) {
    bool cond;

    switch (cop_num) {
        case CopNum::COP_NUM_FPU:
            cond = cpu->get_fpu_cond();
            break;
        case CopNum::COP_NUM_VFPU:
            cond = cpu->get_vfpu_cond(VT >> 2);
            break;
        default:
            cpu->get_logger()->error("Unimplemented CP{} for BCTL", cop_num);
            exit(1);
    }

    cpu->branch<true>(cpu->get_pc() + ((i32)(i16)UIMM << 2), cond, 0);
    return 1;
}

static i64 i_beq(Allegrex* cpu, const u32 instr) {
    cpu->branch<false>(cpu->get_pc() + ((i32)(i16)UIMM << 2), cpu->get_reg(RS) == cpu->get_reg(RT), 0);
    return 1;
}

static i64 i_beql(Allegrex* cpu, const u32 instr) {
    cpu->branch<true>(cpu->get_pc() + ((i32)(i16)UIMM << 2), cpu->get_reg(RS) == cpu->get_reg(RT), 0);
    return 1;
}

static i64 i_bgez(Allegrex* cpu, const u32 instr) {
    cpu->branch<false>(cpu->get_pc() + ((i32)(i16)UIMM << 2), (i32)cpu->get_reg(RS) >= 0, 0);
    return 1;
}

static i64 i_bgezal(Allegrex* cpu, const u32 instr) {
    cpu->branch<false>(cpu->get_pc() + ((i32)(i16)UIMM << 2), (i32)cpu->get_reg(RS) >= 0, 31);
    return 1;
}

static i64 i_bgezl(Allegrex* cpu, const u32 instr) {
    cpu->branch<true>(cpu->get_pc() + ((i32)(i16)UIMM << 2), (i32)cpu->get_reg(RS) >= 0, 0);
    return 1;
}

static i64 i_bgtz(Allegrex* cpu, const u32 instr) {
    cpu->branch<false>(cpu->get_pc() + ((i32)(i16)UIMM << 2), (i32)cpu->get_reg(RS) > 0, 0);
    return 1;
}

static i64 i_bgtzl(Allegrex* cpu, const u32 instr) {
    cpu->branch<true>(cpu->get_pc() + ((i32)(i16)UIMM << 2), (i32)cpu->get_reg(RS) > 0, 0);
    return 1;
}

static i64 i_bitrev(Allegrex* cpu, const u32 instr) {
    const u32 t = cpu->get_reg(RT);

    u32 t_reversed = 0;

    for (u32 i = 0; i < (8 * sizeof(u32)); i++) {
        t_reversed |= ((t >> (31 - i)) & 1) << i;
    }

    cpu->set_reg(RD, t_reversed);
    return 1;
}

static i64 i_blez(Allegrex* cpu, const u32 instr) {
    cpu->branch<false>(cpu->get_pc() + ((i32)(i16)UIMM << 2), (i32)cpu->get_reg(RS) <= 0, 0);
    return 1;
}

static i64 i_blezl(Allegrex* cpu, const u32 instr) {
    cpu->branch<true>(cpu->get_pc() + ((i32)(i16)UIMM << 2), (i32)cpu->get_reg(RS) <= 0, 0);
    return 1;
}

static i64 i_bltz(Allegrex* cpu, const u32 instr) {
    cpu->branch<false>(cpu->get_pc() + ((i32)(i16)UIMM << 2), (i32)cpu->get_reg(RS) < 0, 0);
    return 1;
}

static i64 i_bltzal(Allegrex* cpu, const u32 instr) {
    cpu->branch<false>(cpu->get_pc() + ((i32)(i16)UIMM << 2), (i32)cpu->get_reg(RS) < 0, 31);
    return 1;
}

static i64 i_bltzl(Allegrex* cpu, const u32 instr) {
    cpu->branch<true>(cpu->get_pc() + ((i32)(i16)UIMM << 2), (i32)cpu->get_reg(RS) < 0, 0);
    return 1;
}

static i64 i_bne(Allegrex* cpu, const u32 instr) {
    cpu->branch<false>(cpu->get_pc() + ((i32)(i16)UIMM << 2), cpu->get_reg(RS) != cpu->get_reg(RT), 0);
    return 1;
}

static i64 i_bnel(Allegrex* cpu, const u32 instr) {
    cpu->branch<true>(cpu->get_pc() + ((i32)(i16)UIMM << 2), cpu->get_reg(RS) != cpu->get_reg(RT), 0);
    return 1;
}

static i64 i_break(Allegrex* cpu, const u32) {
    // cpu->raise_lv1_exception(Cp0::EXCEPTION_CODE_BREAKPOINT);
    // return 1;

    cpu->get_logger()->error("BREAK");
    cpu->dump_state();
    cpu->get_logger()->info(" IA: {:08X}", cpu->get_instr_addr());
    exit(1);
}

static i64 i_ceilw(Allegrex* cpu, const u32 instr) {
    cpu->set_fgr_raw(FD, (i32)std::ceilf(cpu->get_fgr(FS)));
    return 1;
}

template<int cop_num>
static i64 i_cfc(Allegrex* cpu, const u32 instr) {
    if (!cpu->is_coprocessor_usable(cop_num)) {
        return 1;
    }

    switch (cop_num) {
        case CopNum::COP_NUM_CP0:
            cpu->set_reg(RT, cpu->get_control_reg(RD));
            break;
        case CopNum::COP_NUM_FPU:
            cpu->set_reg(RT, cpu->get_fpu_control_reg(RD));
            break;
        default:
            cpu->get_logger()->error("Unimplemented CP{} for CFC", cop_num);
            exit(1);
    }

    return 1;
}

static i64 i_clz(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RD, std::countl_zero(cpu->get_reg(RS)));
    return 1;
}

template<int cop_num>
static i64 i_ctc(Allegrex* cpu, const u32 instr) {
    if (!cpu->is_coprocessor_usable(cop_num)) {
        return 1;
    }

    switch (cop_num) {
        case CopNum::COP_NUM_CP0:
            cpu->set_control_reg(RD, cpu->get_reg(RT));
            break;
        case CopNum::COP_NUM_FPU:
            cpu->set_fpu_control_reg(RD, cpu->get_reg(RT));
            break;
        default:
            cpu->get_logger()->error("Unimplemented CP{} for CTC", cop_num);
            exit(1);
    }

    return 1;
}

static i64 i_cvts(Allegrex* cpu, const u32 instr) {
    cpu->set_fgr(FD, (f32)(i32)cpu->get_fgr_raw(FS));
    return 1;
}

static i64 i_div(Allegrex* cpu, const u32 instr) {
    const i32 num = cpu->get_reg(RS);
    const i32 denom = cpu->get_reg(RT);

    assert((denom != 0) && !((num == INT32_MIN) && (denom == -1)));

    cpu->set_reg(32, num / denom);
    cpu->set_reg(33, num % denom);

    return 1; // Not correct
}

static i64 i_divu(Allegrex* cpu, const u32 instr) {
    const u64 num = (u64)cpu->get_reg(RS);
    const u64 denom = (u64)cpu->get_reg(RT);

    if (denom == 0) {
        cpu->set_reg(32, ~0);
        cpu->set_reg(33, num);
    } else {
        cpu->set_reg(32, num / denom);
        cpu->set_reg(33, num % denom);
    }

    return 1; // Not correct
}

static i64 i_eret(Allegrex* cpu) {
    cpu->return_from_exception();
    return 1;
}

static i64 i_ext(Allegrex* cpu, const u32 instr) {
    // What happens when the CPU encounters this?
    assert((POS + SIZE + 1) <= 32);

    cpu->set_reg(RT, (cpu->get_reg(RS) >> POS) & (0xFFFFFFFFU >> (31 - SIZE)));
    return 1;
}

static i64 i_fabs(Allegrex* cpu, const u32 instr) {
    cpu->set_fgr(FD, std::abs(cpu->get_fgr(FS)));
    return 1;
}

static i64 i_fadd(Allegrex* cpu, const u32 instr) {
    cpu->set_fgr(FD, cpu->get_fgr(FS) + cpu->get_fgr(FT));
    return 1;
}

static i64 i_fc(Allegrex* cpu, const u32 instr) {
    const f32 s = cpu->get_fgr(FS);
    const f32 t = cpu->get_fgr(FT);
    
    u32 conds = 0;

    if (std::isnan(s) || std::isnan(t)) {
        // Unordered
        conds |= 1;
    } else {
        if (s < t) {
            conds |= 4;
        }

        if (s == t) {
            conds |= 2;
        }
    }

    cpu->set_fpu_cond(((instr & 7) & conds) != 0);

    return 1;
}

static i64 i_fdiv(Allegrex* cpu, const u32 instr) {
    cpu->set_fgr(FD, cpu->get_fgr(FS) / cpu->get_fgr(FT));
    return 1;
}

static i64 i_fmov(Allegrex* cpu, const u32 instr) {
    cpu->set_fgr(FD, cpu->get_fgr(FS));
    return 1;
}

static i64 i_fmul(Allegrex* cpu, const u32 instr) {
    cpu->set_fgr(FD, cpu->get_fgr(FS) * cpu->get_fgr(FT));
    return 1;
}

static i64 i_fneg(Allegrex* cpu, const u32 instr) {
    cpu->set_fgr(FD, -cpu->get_fgr(FS));
    return 1;
}

static i64 i_fsub(Allegrex* cpu, const u32 instr) {
    cpu->set_fgr(FD, cpu->get_fgr(FS) - cpu->get_fgr(FT));
    return 1;
}

static i64 i_fsqrt(Allegrex* cpu, const u32 instr) {
    cpu->set_fgr(FD, std::sqrtf(cpu->get_fgr(FS)));
    return 1;
}

static i64 i_halt(Allegrex* cpu) {
    cpu->wait_for_interrupt();
    return 1;
}

static i64 i_ins(Allegrex* cpu, const u32 instr) {
    // SIZE = POS + size - 1
    const i32 size = SIZE - POS;

    // What happens when the CPU encounters this?
    assert((size >= 0) && ((SIZE + 1) <= 32));

    const u32 mask = 0xFFFFFFFFU >> (31 - size);

    cpu->set_reg(RT, (cpu->get_reg(RT) & ~(mask << POS)) | ((cpu->get_reg(RS) & mask) << POS));
    return 1;
}

static i64 i_j(Allegrex* cpu, const u32 instr) {
    cpu->branch<false>((cpu->get_pc() & 0xF0000000) + (TARGET << 2), true, 0);
    return 1;
}

static i64 i_jal(Allegrex* cpu, const u32 instr) {
    cpu->branch<false>((cpu->get_pc() & 0xF0000000) + (TARGET << 2), true, 31);
    return 1;
}

static i64 i_jalr(Allegrex* cpu, const u32 instr) {
    cpu->branch<false>(cpu->get_reg(RS), true, RD);
    return 1;
}

static i64 i_jr(Allegrex* cpu, const u32 instr) {
    cpu->branch<false>(cpu->get_reg(RS), true, 0);
    return 1;
}

static i64 i_lb(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RT, (i32)(i8)cpu->read<u8>(cpu->get_reg(RS) + (i32)(i16)UIMM));
    return 1;
}

static i64 i_lbu(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RT, cpu->read<u8>(cpu->get_reg(RS) + (i32)(i16)UIMM));
    return 1;
}

static i64 i_lh(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RT, (i32)(i16)cpu->read<u16>(cpu->get_reg(RS) + (i32)(i16)UIMM));
    return 1;
}

static i64 i_lhu(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RT, cpu->read<u16>(cpu->get_reg(RS) + (i32)(i16)UIMM));
    return 1;
}

static i64 i_ll(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RT, cpu->read<u32>(cpu->get_reg(RS) + (i32)(i16)UIMM));
    cpu->set_load_linked(true);
    return 1;
}

// Also called LV.Q
static i64 i_lqc2(Allegrex* cpu, const u32 instr) {
    assert(cpu->get_cpu_id() == CPU_ID_SC);

    if (!cpu->is_coprocessor_usable(CopNum::COP_NUM_VFPU)) {
        return 1;
    }

    const u32 vt = ((UIMM & 1) << 5) | RT;
    const u32 addr = cpu->get_reg(RS) + (i32)(i16)(UIMM & ~3);

    assert((addr & 0xF) == 0);

    Vec4 vec;

    for (int i = 0; i < 4; i++) {
        vec.e[i].raw = cpu->read<u32>(addr + i * sizeof(u32));
    }

    cpu->set_matrix_file<Vfpu::MatrixType::QuadVector>(vt, vec.e);
    return 1;
}

static i64 i_lui(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RT, UIMM << 16);
    return 1;
}

static i64 i_lw(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RT, cpu->read<u32>(cpu->get_reg(RS) + (i32)(i16)UIMM));
    return 1;
}

static i64 i_lwc1(Allegrex* cpu, const u32 instr) {
    if (!cpu->is_coprocessor_usable(CopNum::COP_NUM_FPU)) {
        return 1;
    }

    cpu->set_fgr_raw(RT, cpu->read<u32>(cpu->get_reg(RS) + (i32)(i16)UIMM));
    return 1;
}

// LQ.S
static i64 i_lwc2(Allegrex* cpu, const u32 instr) {
    assert(cpu->get_cpu_id() == CPU_ID_SC);

    if (!cpu->is_coprocessor_usable(CopNum::COP_NUM_VFPU)) {
        return 1;
    }

    const u32 vt = ((UIMM & 3) << 5) | RT;
    const VfpuFloat flt { .raw = cpu->read<u32>(cpu->get_reg(RS) + (i32)(i16)(UIMM & ~3)) };

    cpu->set_matrix_file<Vfpu::MatrixType::Scalar>(vt, &flt);
    return 1;
}

static i64 i_lwl(Allegrex* cpu, const u32 instr) {
    const u32 addr  = cpu->get_reg(RS) + (i32)(i16)UIMM;
    const u32 shift = 24 - 8 * (addr & 3);

    cpu->set_reg(RT, (cpu->get_reg(RT) & ~(0xFFFFFFFFU << shift)) | (cpu->read<u32>(addr & ~3) << shift));
    return 1;
}

static i64 i_lwr(Allegrex* cpu, const u32 instr) {
    const u32 addr  = cpu->get_reg(RS) + (i32)(i16)UIMM;
    const u32 shift = 8 * (addr & 3);

    cpu->set_reg(RT, (cpu->get_reg(RT) & ~(0xFFFFFFFFU >> shift)) | (cpu->read<u32>(addr & ~3) >> shift));
    return 1;
}

static i64 i_madd(Allegrex* cpu, const u32 instr) {
    const i64 acc = ((u64)cpu->get_reg(33) << 32) | cpu->get_reg(32);
    const u64 result = acc + (i64)(i32)cpu->get_reg(RS) * (i64)(i32)cpu->get_reg(RT);

    cpu->set_reg(32, (u32)result);
    cpu->set_reg(33, (u32)(result >> 32));
    return 1; // Not correct
}

static i64 i_max(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RD, std::max((i32)cpu->get_reg(RS), (i32)cpu->get_reg(RT)));
    return 1;
}

template<int cop_num>
static i64 i_mfc(Allegrex* cpu, const u32 instr) {
    if (!cpu->is_coprocessor_usable(cop_num)) {
        return 1;
    }

    switch (cop_num) {
        case CopNum::COP_NUM_CP0:
            cpu->set_reg(RT, cpu->get_status_reg(RD));
            break;
        case CopNum::COP_NUM_FPU:
            cpu->set_reg(RT, cpu->get_fgr_raw(RD));
            break;
        default:
            cpu->get_logger()->error("Unimplemented CP{} for MFC", cop_num);
            exit(1);
    }

    return 1;
}

static i64 i_mfhi(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RD, cpu->get_reg(33));
    return 1;
}

static i64 i_mfic(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RT, cpu->status_get_ic());
    return 1;
}

static i64 i_mflo(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RD, cpu->get_reg(32));
    return 1;
}

static i64 i_mfvc(Allegrex* cpu, const u32 instr) {
    assert(cpu->get_cpu_id() == CpuId::CPU_ID_SC);

    cpu->set_reg(RT, cpu->get_vfpu_control_reg(UIMM & 0xFF));
    return 1;
}

static i64 i_mfvme(Allegrex* cpu, const u32 instr) {
    assert(cpu->get_cpu_id() == CpuId::CPU_ID_ME);

    cpu->get_logger()->info("MFVME {}, offset {:04X} @ {:08X}", RS, UIMM, cpu->get_instr_addr());
    cpu->set_reg(RT, 1);
    return 1;
}

static i64 i_min(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RD, std::min((i32)cpu->get_reg(RS), (i32)cpu->get_reg(RT)));
    return 1;
}

static i64 i_movn(Allegrex* cpu, const u32 instr) {
    if (cpu->get_reg(RT) != 0) cpu->set_reg(RD, cpu->get_reg(RS));
    return 1;
}

static i64 i_movz(Allegrex* cpu, const u32 instr) {
    if (cpu->get_reg(RT) == 0) cpu->set_reg(RD, cpu->get_reg(RS));
    return 1;
}

template<int cop_num>
static i64 i_mtc(Allegrex* cpu, const u32 instr) {
    if (!cpu->is_coprocessor_usable(cop_num)) {
        return 1;
    }

    switch (cop_num) {
        case CopNum::COP_NUM_CP0:
            cpu->set_status_reg(RD, cpu->get_reg(RT));
            break;
        case CopNum::COP_NUM_FPU:
            cpu->set_fgr_raw(RD, cpu->get_reg(RT));
            break;
        default:
            cpu->get_logger()->error("Unimplemented CP{} for MTC", cop_num);
            exit(1);
    }

    return 1;
}

static i64 i_mthi(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(33, cpu->get_reg(RS));
    return 1;
}

static i64 i_mtic(Allegrex* cpu, const u32 instr) {
    cpu->status_set_ic(cpu->get_reg(RT));
    return 1;
}

static i64 i_mtlo(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(32, cpu->get_reg(RS));
    return 1;
}

static i64 i_mtvc(Allegrex* cpu, const u32 instr) {
    assert(cpu->get_cpu_id() == CpuId::CPU_ID_SC);

    cpu->set_vfpu_control_reg(UIMM & 0xFF, cpu->get_reg(RT));
    return 1;
}

static i64 i_mtvme(Allegrex* cpu, const u32 instr) {
    assert(cpu->get_cpu_id() == CpuId::CPU_ID_ME);

    cpu->get_logger()->info("MTVME {}, offset {:04X}, data: {:08X} @ {:08X}", RS, UIMM, cpu->get_reg(RT), cpu->get_instr_addr());
    return 1;
}

static i64 i_mult(Allegrex* cpu, const u32 instr) {
    const u64 result = (i64)(i32)cpu->get_reg(RS) * (i64)(i32)cpu->get_reg(RT);

    cpu->set_reg(32, (u32)result);
    cpu->set_reg(33, (u32)(result >> 32));
    return 1; // Not correct
}

static i64 i_multu(Allegrex* cpu, const u32 instr) {
    const u64 result = (u64)cpu->get_reg(RS) * (u64)cpu->get_reg(RT);

    cpu->set_reg(32, (u32)result);
    cpu->set_reg(33, (u32)(result >> 32));
    return 1; // Not correct
}

static i64 i_nor(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RD, ~(cpu->get_reg(RS) | cpu->get_reg(RT)));
    return 1;
}

static i64 i_or(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RD, cpu->get_reg(RS) | cpu->get_reg(RT));
    return 1;
}

static i64 i_ori(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RT, cpu->get_reg(RS) | UIMM);
    return 1;
}

static i64 i_rotr(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RD, std::rotr(cpu->get_reg(RT), SA));
    return 1;
}

static i64 i_rotrv(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RD, std::rotr(cpu->get_reg(RT), (cpu->get_reg(RS) & 0x1F)));
    return 1;
}

static i64 i_roundw(Allegrex* cpu, const u32 instr) {
    cpu->set_fgr_raw(FD, (i32)std::roundf(cpu->get_fgr(FS)));
    return 1;
}

static i64 i_sb(Allegrex* cpu, const u32 instr) {
    cpu->write<u8>(cpu->get_reg(RS) + (i32)(i16)UIMM, (u8)cpu->get_reg(RT));
    return 1;
}

static i64 i_sc(Allegrex* cpu, const u32 instr) {
    if (cpu->is_load_linked()) {
        cpu->write<u32>(cpu->get_reg(RS) + (i32)(i16)UIMM, cpu->get_reg(RT));
        cpu->set_reg(RT, 1);
    } else {
        cpu->set_reg(RT, 0);
    }

    cpu->set_load_linked(false);
    return 1;
}

static i64 i_seb(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RD, (i32)(i8)cpu->get_reg(RT));
    return 1;
}

static i64 i_seh(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RD, (i32)(i16)cpu->get_reg(RT));
    return 1;
}

static i64 i_sh(Allegrex* cpu, const u32 instr) {
    cpu->write<u16>(cpu->get_reg(RS) + (i32)(i16)UIMM, (u16)cpu->get_reg(RT));
    return 1;
}

static i64 i_sll(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RD, cpu->get_reg(RT) << SA);
    return 1;
}

static i64 i_sllv(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RD, cpu->get_reg(RT) << (cpu->get_reg(RS) & 0x1F));
    return 1;
}

static i64 i_slt(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RD, (i32)cpu->get_reg(RS) < (i32)cpu->get_reg(RT));
    return 1;
}

static i64 i_slti(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RT, (i32)cpu->get_reg(RS) < (i32)(i16)UIMM);
    return 1;
}

static i64 i_sltiu(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RT, cpu->get_reg(RS) < (u32)(i16)UIMM);
    return 1;
}

static i64 i_sltu(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RD, cpu->get_reg(RS) < cpu->get_reg(RT));
    return 1;
}

// Also called SV.Q
static i64 i_sqc2(Allegrex* cpu, const u32 instr) {
    assert(cpu->get_cpu_id() == CPU_ID_SC);

    if (!cpu->is_coprocessor_usable(CopNum::COP_NUM_VFPU)) {
        return 1;
    }

    const u32 vt = ((UIMM & 1) << 5) | RT;
    const u32 addr = cpu->get_reg(RS) + (i32)(i16)(UIMM & ~3);

    assert((addr & 0xF) == 0);

    Vec4 vec;

    cpu->get_matrix_file<Vfpu::MatrixType::QuadVector>(vt, vec.e);

    for (int i = 0; i < 4; i++) {
        cpu->write<u32>(addr + i * sizeof(u32), vec.e[i].raw);
    }
    return 1;
}

static i64 i_sra(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RD, (i32)cpu->get_reg(RT) >> SA);
    return 1;
}

static i64 i_srav(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RD, (i32)cpu->get_reg(RT) >> (cpu->get_reg(RS) & 0x1F));
    return 1;
}

static i64 i_srl(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RD, cpu->get_reg(RT) >> SA);
    return 1;
}

static i64 i_srlv(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RD, cpu->get_reg(RT) >> (cpu->get_reg(RS) & 0x1F));
    return 1;
}

// Is identical to SUBU as far as I remember, so...
// static i64 i_sub(Allegrex* cpu, const u32 instr) {

static i64 i_subu(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RD, cpu->get_reg(RS) - cpu->get_reg(RT));
    return 1;
}

static i64 i_sw(Allegrex* cpu, const u32 instr) {
    cpu->write<u32>(cpu->get_reg(RS) + (i32)(i16)UIMM, cpu->get_reg(RT));
    return 1;
}

static i64 i_swc1(Allegrex* cpu, const u32 instr) {
    if (!cpu->is_coprocessor_usable(CopNum::COP_NUM_FPU)) {
        return 1;
    }

    cpu->write<u32>(cpu->get_reg(RS) + (i32)(i16)UIMM, cpu->get_fgr_raw(RT));
    return 1;
}

// SQ.S
static i64 i_swc2(Allegrex* cpu, const u32 instr) {
    assert(cpu->get_cpu_id() == CPU_ID_SC);

    if (!cpu->is_coprocessor_usable(CopNum::COP_NUM_VFPU)) {
        return 1;
    }

    const u32 vt = ((UIMM & 3) << 5) | RT;

    VfpuFloat flt;

    cpu->get_matrix_file<Vfpu::MatrixType::Scalar>(vt, &flt);
    cpu->write<u32>(cpu->get_reg(RS) + (i32)(i16)(UIMM & ~3), flt.raw);
    return 1;
}

static i64 i_swl(Allegrex* cpu, const u32 instr) {
    const u32 addr  = cpu->get_reg(RS) + (i32)(i16)UIMM;
    const u32 shift = 8 * (addr & 3);

    cpu->write<u32>(addr & ~3, (cpu->read<u32>(addr & ~3) & (0xFFFFFF00U << shift)) | (cpu->get_reg(RT) >> (24 - shift)));
    return 1;
}

static i64 i_swr(Allegrex* cpu, const u32 instr) {
    const u32 addr  = cpu->get_reg(RS) + (i32)(i16)UIMM;
    const u32 shift = 8 * (addr & 3);

    cpu->write<u32>(addr & ~3, (cpu->read<u32>(addr & ~3) & ~(0xFFFFFFFFU << shift)) | (cpu->get_reg(RT) << shift));
    return 1;
}

static i64 i_sync(Allegrex*, const u32) {
    return 1;
}

static i64 i_syscall(Allegrex* cpu, const u32 instr) {
    cpu->raise_lv1_exception(Cp0::EXCEPTION_CODE_SYSCALL);
    cpu->set_syscall_code(SCCODE);
    return 1;
}

static i64 i_truncw(Allegrex* cpu, const u32 instr) {
    cpu->set_fgr_raw(FD, (i32)std::truncf(cpu->get_fgr(FS)));
    return 1;
}

template<Vfpu::MatrixType type>
static constexpr inline i64 get_element_count() {
    // The return value is not really meaningful for matrix ops, but
    // I can use it to calculate instruction cycles
    switch (type) {
        case Vfpu::MatrixType::Scalar:
            return 1;
        case Vfpu::MatrixType::PairVector:
        case Vfpu::MatrixType::PairMatrix:
            return 2;
        case Vfpu::MatrixType::TripleVector:
        case Vfpu::MatrixType::TripleMatrix:
            return 3;
        case Vfpu::MatrixType::QuadVector:
        case Vfpu::MatrixType::QuadMatrix:
            return 4;
        default:
            assert(false);
    }
}

template<Vfpu::MatrixType type>
static i64 i_vabs(Allegrex* cpu, const u32 instr) {
    Vec4 vd, vs;

    cpu->get_matrix_file<type>(VS, vs.e);
    cpu->decorate_src(vs);

    for (int i = 0; i < get_element_count<type>(); i++) {
        vd.e[i].flt = std::abs(vs.e[i].flt);
    }

    cpu->decorate_dst(vd);
    cpu->clear_decorators();
    cpu->set_matrix_file<type>(VD, vd.e, vd.write_mask);
    return 3;
}

template<Vfpu::MatrixType type>
static i64 i_vadd(Allegrex* cpu, const u32 instr) {
    Vec4 vd, vs, vt;

    cpu->get_matrix_file<type>(VS, vs.e);
    cpu->get_matrix_file<type>(VT, vt.e);
    cpu->decorate_src(vs);
    cpu->decorate_tgt(vt);

    for (int i = 0; i < get_element_count<type>(); i++) {
        vd.e[i].flt = vs.e[i].flt + vt.e[i].flt;
    }

    cpu->decorate_dst(vd);
    cpu->clear_decorators();
    cpu->set_matrix_file<type>(VD, vd.e, vd.write_mask);
    return 5;
}

template<Vfpu::MatrixType type>
static i64 i_vasin(Allegrex* cpu, const u32 instr) {
    Vec4 vd, vs;

    cpu->get_matrix_file<type>(VS, vs.e);

    if constexpr (type == Vfpu::MatrixType::Scalar) {
        cpu->decorate_src(vs);
    }

    for (int i = 0; i < get_element_count<type>(); i++) {
        vd.e[i].flt = std::asinf(vs.e[i].flt) / M_PI_2;
    }

    if constexpr (type == Vfpu::MatrixType::Scalar) {
        cpu->decorate_dst(vd);
        cpu->clear_decorators();
        cpu->set_matrix_file<type>(VD, vd.e, vd.write_mask);
    } else {
        cpu->set_matrix_file<type>(VD, vd.e);
    }
    
    return 6 + get_element_count<type>();
}

template<Vfpu::MatrixType type>
static i64 i_vcmovt(Allegrex* cpu, const u32 instr) {
    Vec4 vd, vs, vt;

    cpu->get_matrix_file<type>(VS, vs.e);
    cpu->get_matrix_file<type>(VD, vt.e);
    cpu->decorate_src(vs);

    const u32 imm3 = (VT >> 1) & 7;

    assert(imm3 != 7);

    const u32 cc = cpu->get_vfpu_control_reg(131);

    for (int i = 0; i < get_element_count<type>(); i++) {
        vd.e[i].flt = vt.e[i].flt;

        if (imm3 < 6) {
            if ((cc & (1 << imm3)) != 0) {
                vd.e[i].flt = vs.e[i].flt;
            }
        } else if ((cc & (1 << i)) != 0) {
            vd.e[i].flt = vs.e[i].flt;
        }
    }

    cpu->set_matrix_file<type>(VD, vd.e);
    return 5;
}

template<Vfpu::MatrixType type>
static i64 i_vcmp(Allegrex* cpu, const u32 instr) {
    Vec4 vs, vt;

    cpu->get_matrix_file<type>(VS, vs.e);
    cpu->get_matrix_file<type>(VT, vt.e);
    cpu->decorate_src(vs);
    cpu->decorate_tgt(vt);

    const u32 cond_code = instr & 0xF;

    u32 cc = 0;

    for (int i = 0; i < get_element_count<type>(); i++) {
        bool cond;

        switch (cond_code) {
            case 0:
                cond = false;
                break;
            case 1:
                cond = vs.e[i].flt == vt.e[i].flt;
                break;
            case 2:
                cond = vs.e[i].flt < vt.e[i].flt;
                break;
            case 3:
                cond = vs.e[i].flt <= vt.e[i].flt;
                break;
            case 4:
                cond = true;
                break;
            case 5:
                cond = vs.e[i].flt != vt.e[i].flt;
                break;
            case 6:
                cond = vs.e[i].flt >= vt.e[i].flt;
                break;
            case 7:
                cond = vs.e[i].flt > vt.e[i].flt;
                break;
            case 8:
                cond = vs.e[i].flt == 0.0;
                break;
            case 9:
                cond = std::isnan(vs.e[i].flt);
                break;
            case 10:
                cond = std::isinf(std::abs(vs.e[i].flt));
                break;
            case 11:
                cond = std::isnan(vs.e[i].flt) || std::isinf(vs.e[i].flt);
                break;
            case 12:
                cond = vs.e[i].flt != 0.0;
                break;
            case 13:
                cond = !std::isnan(vs.e[i].flt);
                break;
            case 14:
                cond = !std::isinf(std::abs(vs.e[i].flt));
                break;
            case 15:
                cond = !std::isnan(vs.e[i].flt) && !std::isinf(vs.e[i].flt);
                break;
        }

        if (cond) {
            cc |= 1 << i;

            // If the comparison "passed", also update the OR bit
            if (i == 0) {
                // Initially, the AND bit is also set
                cc |= 0x30;
            } else {
                cc |= 0x10;
            }
        } else if (i != 0) {
            // Clear the AND bit if the comparison "failed"
            cc &= ~0x20;
        }
    }

    cpu->set_vfpu_control_reg(131, cc);
    return 3;
}

template<Vfpu::MatrixType type>
static i64 i_vcos(Allegrex* cpu, const u32 instr) {
    Vec4 vd, vs;

    cpu->get_matrix_file<type>(VS, vs.e);

    if constexpr (type == Vfpu::MatrixType::Scalar) {
        cpu->decorate_src(vs);
    }

    for (int i = 0; i < get_element_count<type>(); i++) {
        vd.e[i].flt = std::cosf(M_PI_2 * vs.e[i].flt);
    }

    if constexpr (type == Vfpu::MatrixType::Scalar) {
        cpu->decorate_dst(vd);
        cpu->clear_decorators();
        cpu->set_matrix_file<type>(VD, vd.e, vd.write_mask);
    } else {
        cpu->set_matrix_file<type>(VD, vd.e);
    }
    
    return 6 + get_element_count<type>();
}

static i64 i_vcrspt(Allegrex* cpu, const u32 instr) {
    Vec4 vd, vs, vt;

    cpu->get_matrix_file<Vfpu::MatrixType::TripleVector>(VS, vs.e);
    cpu->get_matrix_file<Vfpu::MatrixType::TripleVector>(VT, vt.e);

    vd.e[0].flt = vs.e[1].flt * vt.e[2].flt - vs.e[2].flt * vt.e[1].flt;
    vd.e[1].flt = vs.e[2].flt * vt.e[0].flt - vs.e[0].flt * vt.e[2].flt;
    vd.e[2].flt = vs.e[0].flt * vt.e[1].flt - vs.e[1].flt * vt.e[0].flt;

    cpu->set_matrix_file<Vfpu::MatrixType::TripleVector>(VD, vd.e);
    return 9;
}

template<Vfpu::MatrixType type>
static i64 i_vcst(Allegrex* cpu, const u32 instr) {
    static const f32 CONSTANTS[] = {
        from_u32(0x7F7FFFFF),
        M_SQRT2, M_SQRT1_2, M_2_SQRTPI,
        M_2_PI, M_1_PI, M_PI_4, M_PI_2, M_PI,
        M_E, M_LOG2E, M_LOG10E, M_LN2, M_LN10,
        2.0 * M_PI, M_PI / 6.0,  std::log10f(2), std::log2f(10), std::sqrtf(3) / 2.0F
    };

    Vec4 vd;

    const u32 imm5 = VT & 31;

    assert((imm5 != 0) && (imm5 <= 19));

    for (int i = 0; i < get_element_count<type>(); i++) {
        vd.e[i].flt = CONSTANTS[imm5 - 1];
    }

    cpu->decorate_dst(vd);
    cpu->clear_decorators();
    cpu->set_matrix_file<type>(VD, vd.e, vd.write_mask);
    return 3;
}

template<Vfpu::MatrixType type>
static i64 i_vdiv(Allegrex* cpu, const u32 instr) {
    Vec4 vd, vs, vt;

    cpu->get_matrix_file<type>(VS, vs.e);
    cpu->get_matrix_file<type>(VT, vt.e);

    if constexpr (type == Vfpu::MatrixType::Scalar) {
        cpu->decorate_src(vs);
        cpu->decorate_tgt(vt);
    }

    for (int i = 0; i < get_element_count<type>(); i++) {
        vd.e[i].flt = vs.e[i].flt / vt.e[i].flt;
    }

    if constexpr (type == Vfpu::MatrixType::Scalar) {
        cpu->decorate_dst(vd);
        cpu->clear_decorators();
        cpu->set_matrix_file<type>(VD, vd.e, vd.write_mask);
    } else {
        cpu->set_matrix_file<type>(VD, vd.e);
    }
    
    return 14 * get_element_count<type>();
}

template<Vfpu::MatrixType type>
static i64 i_vdot(Allegrex* cpu, const u32 instr) {
    Vec4 vd, vs, vt;

    cpu->get_matrix_file<type>(VS, vs.e);
    cpu->get_matrix_file<type>(VT, vt.e);
    cpu->decorate_src(vs);
    cpu->decorate_tgt(vt);

    vd.e[0].flt = 0;

    for (int i = 0; i < get_element_count<type>(); i++) {
        vd.e[0].flt += vs.e[i].flt * vt.e[i].flt;
    }

    cpu->decorate_dst(vd);
    cpu->clear_decorators();
    cpu->set_matrix_file<Vfpu::MatrixType::Scalar>(VD, vd.e, vd.write_mask);
    return 7;
}

template<Vfpu::MatrixType type>
static i64 i_vexp2(Allegrex* cpu, const u32 instr) {
    Vec4 vd, vs;

    cpu->get_matrix_file<type>(VS, vs.e);

    if constexpr (type == Vfpu::MatrixType::Scalar) {
        cpu->decorate_src(vs);
    }

    for (int i = 0; i < get_element_count<type>(); i++) {
        vd.e[i].flt = std::exp2f(vs.e[i].flt);
    }

    if constexpr (type == Vfpu::MatrixType::Scalar) {
        cpu->decorate_dst(vd);
        cpu->clear_decorators();
        cpu->set_matrix_file<type>(VD, vd.e, vd.write_mask);
    } else {
        cpu->set_matrix_file<type>(VD, vd.e);
    }
    
    return 6 + get_element_count<type>();
}

template<Vfpu::MatrixType type>
static i64 i_vf2iz(Allegrex* cpu, const u32 instr) {
    Vec4 vd, vs;

    cpu->get_matrix_file<type>(VS, vs.e);
    cpu->decorate_src(vs);

    const u32 imm5 = VT & 31;

    for (int i = 0; i < get_element_count<type>(); i++) {
        vd.e[i].flt = std::truncf(vs.e[i].flt * (1 << imm5));
    }

    cpu->decorate_dst(vd);
    cpu->clear_decorators();
    cpu->set_matrix_file<type>(VD, vd.e, vd.write_mask);
    return 5;
}

static i64 i_vfims(Allegrex* cpu, const u32 instr) {
    Vec4 vd;

    // Build f32 from f16
    u32 flt = (UIMM & 0x8000) << 16;

    const u32 exponent = (UIMM >> 10) & 31;
    const u32 mantissa = (UIMM >>  0) & 0x3FF;

    if (exponent == 0) {
        // This is either 0 or a denormal, which is flushed to 0 on the PSP?
    } else if (exponent == 0x1F) {
        // Infinity or NaN
        flt |= 0xFF << 23;
        flt |= mantissa << 13;
    } else {
        flt |= (exponent + 127 - 15) << 23;
        flt |= mantissa << 13;
    }

    vd.e[0].flt = from_u32(flt);

    cpu->decorate_dst(vd);
    cpu->clear_decorators();
    cpu->set_matrix_file<Vfpu::MatrixType::Scalar>(VT, vd.e, vd.write_mask);
    return 3;
}

static i64 i_vhtfm3t(Allegrex* cpu, const u32 instr) {
    Vec4 vd, vt;
    Mat4 mtxs;

    cpu->get_matrix_file<Vfpu::MatrixType::TripleMatrix>(VS, mtxs);
    cpu->get_matrix_file<Vfpu::MatrixType::TripleVector>(VT, vt.e);

    for (int i = 0; i < 3; i++) {
        vd.e[i].flt = mtxs[i].flt * vt.e[0].flt + mtxs[i + 4].flt * vt.e[1].flt + mtxs[i + 8].flt;
    }

    cpu->set_matrix_file<Vfpu::MatrixType::TripleVector>(VD, vd.e);
    return 9;
}

static i64 i_vhtfm4t(Allegrex* cpu, const u32 instr) {
    Vec4 vd, vt;
    Mat4 mtxs;

    cpu->get_matrix_file<Vfpu::MatrixType::QuadMatrix>(VS, mtxs);
    cpu->get_matrix_file<Vfpu::MatrixType::QuadVector>(VT, vt.e);

    for (int i = 0; i < 4; i++) {
        vd.e[i].flt = mtxs[i].flt * vt.e[0].flt + mtxs[i + 4].flt * vt.e[1].flt + mtxs[i + 8].flt * vt.e[2].flt + mtxs[i + 12].flt;
    }

    cpu->set_matrix_file<Vfpu::MatrixType::QuadVector>(VD, vd.e);
    return 10;
}

static i64 i_vi2ucq(Allegrex* cpu, const u32 instr) {
    Vec4 vd, vs;

    cpu->get_matrix_file<Vfpu::MatrixType::QuadVector>(VS, vs.e);
    cpu->decorate_src(vs);

    vd.e[0].raw = 0;

    for (int i = 0; i < 4; i++) {
        const i32 e = (i32)vs.e[i].raw;

        if (e >= 0) {
            vd.e[0].raw |= (((u32)e >> 23) & 0xFF) << (8 * i);
        }
    }

    cpu->set_matrix_file<Vfpu::MatrixType::Scalar>(VD, vd.e);
    return 3;
}

template<Vfpu::MatrixType type>
static i64 i_vidt(Allegrex* cpu, const u32 instr) {
    Vec4 vd;

    for (int i = 0; i < get_element_count<type>(); i++) {
        vd.e[i].flt = ((VD & 3) == i) ? 1.0 : 0.0;
    }

    cpu->decorate_dst(vd);
    cpu->clear_decorators();
    cpu->set_matrix_file<type>(VD, vd.e, vd.write_mask);
    return 3;
}

static i64 i_viims(Allegrex* cpu, const u32 instr) {
    Vec4 vd;

    vd.e[0].flt = (f32)(i16)UIMM;

    cpu->decorate_dst(vd);
    cpu->clear_decorators();
    cpu->set_matrix_file<Vfpu::MatrixType::Scalar>(VT, vd.e, vd.write_mask);
    return 3;
}

template<Vfpu::MatrixType type>
static i64 i_vlog2(Allegrex* cpu, const u32 instr) {
    Vec4 vd, vs;

    cpu->get_matrix_file<type>(VS, vs.e);

    if constexpr (type == Vfpu::MatrixType::Scalar) {
        cpu->decorate_src(vs);
    }

    for (int i = 0; i < get_element_count<type>(); i++) {
        vd.e[i].flt = std::log2f(vs.e[i].flt);
    }

    if constexpr (type == Vfpu::MatrixType::Scalar) {
        cpu->decorate_dst(vd);
        cpu->clear_decorators();
        cpu->set_matrix_file<type>(VD, vd.e, vd.write_mask);
    } else {
        cpu->set_matrix_file<type>(VD, vd.e);
    }
    
    return 6 + get_element_count<type>();
}

template<Vfpu::MatrixType type>
static i64 i_vmax(Allegrex* cpu, const u32 instr) {
    Vec4 vd, vs, vt;

    cpu->get_matrix_file<type>(VS, vs.e);
    cpu->get_matrix_file<type>(VT, vt.e);
    cpu->decorate_src(vs);
    cpu->decorate_tgt(vt);

    for (int i = 0; i < get_element_count<type>(); i++) {
        vd.e[i].flt = std::max(vs.e[i].flt, vt.e[i].flt);
    }

    cpu->decorate_dst(vd);
    cpu->clear_decorators();
    cpu->set_matrix_file<type>(VD, vd.e, vd.write_mask);
    return 3;
}

template<Vfpu::MatrixType type>
static i64 i_vmidt(Allegrex* cpu, const u32 instr) {
    constexpr Mat4 IDENTITY_MTX = {
        { 1.0 }, { 0.0 }, { 0.0 }, { 0.0 },
        { 0.0 }, { 1.0 }, { 0.0 }, { 0.0 },
        { 0.0 }, { 0.0 }, { 1.0 }, { 0.0 },
        { 0.0 }, { 0.0 }, { 0.0 }, { 1.0 },
    };

    cpu->set_matrix_file<type>(VD, IDENTITY_MTX);
    return 2 + get_element_count<type>();
}

template<Vfpu::MatrixType type>
static i64 i_vmin(Allegrex* cpu, const u32 instr) {
    Vec4 vd, vs, vt;

    cpu->get_matrix_file<type>(VS, vs.e);
    cpu->get_matrix_file<type>(VT, vt.e);
    cpu->decorate_src(vs);
    cpu->decorate_tgt(vt);

    for (int i = 0; i < get_element_count<type>(); i++) {
        vd.e[i].flt = std::min(vs.e[i].flt, vt.e[i].flt);
    }

    cpu->decorate_dst(vd);
    cpu->clear_decorators();
    cpu->set_matrix_file<type>(VD, vd.e, vd.write_mask);
    return 3;
}

static i64 i_vmmulq(Allegrex* cpu, const u32 instr) {
    Mat4 mtxd, mtxs, mtxt;

    cpu->get_matrix_file<Vfpu::MatrixType::QuadMatrix>(VS, mtxs);
    cpu->get_matrix_file<Vfpu::MatrixType::QuadMatrix>(VT, mtxt);

    for (int column = 0; column < 4; column++) {
        for (int row = 0; row < 4; row++) {
            f32 sum = 0;

            for (int k = 0; k < 4; k++) {
                sum += mtxs[4 * k + column].flt * mtxt[4 * k + row].flt;
            }

            mtxd[4 * column + row].flt = sum;
        }
    }

    cpu->set_matrix_file<Vfpu::MatrixType::QuadMatrix>(VD, mtxd);
    return 16;
}

template<Vfpu::MatrixType type>
static i64 i_vmone(Allegrex* cpu, const u32 instr) {
    constexpr Mat4 ONE_MTX = {
        { 1.0 }, { 1.0 }, { 1.0 }, { 1.0 },
        { 1.0 }, { 1.0 }, { 1.0 }, { 1.0 },
        { 1.0 }, { 1.0 }, { 1.0 }, { 1.0 },
        { 1.0 }, { 1.0 }, { 1.0 }, { 1.0 },
    };

    cpu->set_matrix_file<type>(VD, ONE_MTX);
    return 2 + get_element_count<type>();
}

template<Vfpu::MatrixType type>
static i64 i_vmov(Allegrex* cpu, const u32 instr) {
    Vec4 vd, vs;

    cpu->get_matrix_file<type>(VS, vs.e);
    cpu->decorate_src(vs);

    for (int i = 0; i < get_element_count<type>(); i++) {
        vd.e[i].flt = vs.e[i].flt;
    }

    cpu->decorate_dst(vd);
    cpu->clear_decorators();
    cpu->set_matrix_file<type>(VD, vd.e, vd.write_mask);
    return 3;
}

template<Vfpu::MatrixType type>
static i64 i_vmscl(Allegrex* cpu, const u32 instr) {
    Mat4 mtxd, mtxs;
    Vec4 vt;

    cpu->get_matrix_file<type>(VS, mtxs);
    cpu->get_matrix_file<Vfpu::MatrixType::Scalar>(VT, vt.e);

    // Here we can "safely" perform this multiplication over 16 elements...
    // Some of them can be invalid, but set_matrix_file will just ignore them
    for (int i = 0; i < 16; i++) {
        mtxd[i].flt = mtxs[i].flt * vt.e[0].flt;
    }

    cpu->set_matrix_file<type>(VD, mtxd);
    return 6 + get_element_count<type>();
}

template<Vfpu::MatrixType type>
static i64 i_vmul(Allegrex* cpu, const u32 instr) {
    Vec4 vd, vs, vt;

    cpu->get_matrix_file<type>(VS, vs.e);
    cpu->get_matrix_file<type>(VT, vt.e);
    cpu->decorate_src(vs);
    cpu->decorate_tgt(vt);

    for (int i = 0; i < get_element_count<type>(); i++) {
        vd.e[i].flt = vs.e[i].flt * vt.e[i].flt;
    }

    cpu->decorate_dst(vd);
    cpu->clear_decorators();
    cpu->set_matrix_file<type>(VD, vd.e, vd.write_mask);
    return 5;
}

template<Vfpu::MatrixType type>
static i64 i_vmzero(Allegrex* cpu, const u32 instr) {
    constexpr Mat4 ZERO_MTX = {
        { 0.0 }, { 0.0 }, { 0.0 }, { 0.0 },
        { 0.0 }, { 0.0 }, { 0.0 }, { 0.0 },
        { 0.0 }, { 0.0 }, { 0.0 }, { 0.0 },
        { 0.0 }, { 0.0 }, { 0.0 }, { 0.0 },
    };

    cpu->set_matrix_file<type>(VD, ZERO_MTX);
    return 2 + get_element_count<type>();
}

template<Vfpu::MatrixType type>
static i64 i_vneg(Allegrex* cpu, const u32 instr) {
    Vec4 vd, vs;

    cpu->get_matrix_file<type>(VS, vs.e);
    cpu->decorate_src(vs);

    for (int i = 0; i < get_element_count<type>(); i++) {
        vd.e[i].flt = -vs.e[i].flt;
    }

    cpu->decorate_dst(vd);
    cpu->clear_decorators();
    cpu->set_matrix_file<type>(VD, vd.e, vd.write_mask);
    return 3;
}

template<Vfpu::MatrixType type>
static i64 i_vocp(Allegrex* cpu, const u32 instr) {
    Vec4 vd, vs;

    cpu->get_matrix_file<type>(VS, vs.e);

    for (int i = 0; i < get_element_count<type>(); i++) {
        vd.e[i].flt = 1.0 - vs.e[i].flt;
    }

    cpu->decorate_dst(vd);
    cpu->clear_decorators();
    cpu->set_matrix_file<type>(VD, vd.e, vd.write_mask);
    return 5;
}

template<Vfpu::MatrixType type>
static i64 i_vone(Allegrex* cpu, const u32 instr) {
    assert(VS == 0);

    Vec4 vd;

    for (int i = 0; i < get_element_count<type>(); i++) {
        vd.e[i].flt = 1.0;
    }

    cpu->decorate_dst(vd);
    cpu->clear_decorators();
    cpu->set_matrix_file<type>(VD, vd.e, vd.write_mask);
    return 3;
}

static i64 i_vpfxd(Allegrex* cpu, const u32 instr) {
    const u32 decorator = instr & 0xFFF;

    cpu->set_vfpu_control_reg(130, decorator);
    return 1;
}

static i64 i_vpfxs(Allegrex* cpu, const u32 instr) {
    const u32 decorator = instr & 0xFFFFFF;

    cpu->set_vfpu_control_reg(128, decorator);
    return 1;
}

static i64 i_vpfxt(Allegrex* cpu, const u32 instr) {
    const u32 decorator = instr & 0xFFFFFF;

    cpu->set_vfpu_control_reg(129, decorator);
    return 1;
}

static i64 i_vqmulq(Allegrex* cpu, const u32 instr) {
    Vec4 vd, vs, vt;

    cpu->get_matrix_file<Vfpu::MatrixType::QuadVector>(VS, vs.e);
    cpu->get_matrix_file<Vfpu::MatrixType::QuadVector>(VT, vt.e);

    vd.e[0].flt =  vs.e[0].flt * vt.e[3].flt + vs.e[1].flt * vt.e[2].flt - vs.e[2].flt * vt.e[1].flt + vs.e[3].flt * vt.e[0].flt;
    vd.e[1].flt = -vs.e[0].flt * vt.e[2].flt + vs.e[1].flt * vt.e[3].flt + vs.e[2].flt * vt.e[0].flt + vs.e[3].flt * vt.e[1].flt;
    vd.e[2].flt =  vs.e[0].flt * vt.e[1].flt - vs.e[1].flt * vt.e[0].flt + vs.e[2].flt * vt.e[3].flt + vs.e[3].flt * vt.e[2].flt;
    vd.e[3].flt = -vs.e[0].flt * vt.e[0].flt - vs.e[1].flt * vt.e[1].flt - vs.e[2].flt * vt.e[2].flt + vs.e[3].flt * vt.e[3].flt;

    cpu->set_matrix_file<Vfpu::MatrixType::QuadVector>(VD, vd.e);
    return 5;
}

template<Vfpu::MatrixType type>
static i64 i_vrcp(Allegrex* cpu, const u32 instr) {
    Vec4 vd, vs;

    cpu->get_matrix_file<type>(VS, vs.e);

    if constexpr (type == Vfpu::MatrixType::Scalar) {
        cpu->decorate_src(vs);
    }

    for (int i = 0; i < get_element_count<type>(); i++) {
        vd.e[i].flt = 1.0 / vs.e[i].flt;
    }

    if constexpr (type == Vfpu::MatrixType::Scalar) {
        cpu->decorate_dst(vd);
        cpu->clear_decorators();
        cpu->set_matrix_file<type>(VD, vd.e, vd.write_mask);
    } else {
        cpu->set_matrix_file<type>(VD, vd.e);
    }
    
    return 6 + get_element_count<type>();
}

static inline f32 get_random_float() {
    static std::random_device random_dev;
    static std::mt19937 rng(random_dev());

    std::uniform_real_distribution<f32> dist(1.0, 2.0);

    return dist(rng);
}

template<Vfpu::MatrixType type>
static i64 i_vrndf1(Allegrex* cpu, const u32 instr) {
    Vec4 vd;

    for (int i = 0; i < get_element_count<type>(); i++) {
        vd.e[i].flt = get_random_float();
    }

    if constexpr (type == Vfpu::MatrixType::Scalar) {
        cpu->decorate_dst(vd);
        cpu->clear_decorators();
        cpu->set_matrix_file<type>(VD, vd.e, vd.write_mask);
    } else {
        cpu->set_matrix_file<type>(VD, vd.e);
    }
    
    return 2 + 3 * get_element_count<type>();
}

template<Vfpu::MatrixType type>
static i64 i_vrot(Allegrex* cpu, const u32 instr) {
    Vec4 vd, vs;

    cpu->get_matrix_file<Vfpu::MatrixType::Scalar>(VS, vs.e);

    const u32 imm5 = VT & 31;

    const f32 sin = ((imm5 & 16) != 0) ? -std::sinf(M_PI_2 * vs.e[0].flt) : std::sinf(M_PI_2 * vs.e[0].flt);
    const f32 cos = std::cosf(M_PI_2 * vs.e[0].flt);

    const int sin_idx = (imm5 >> 2) & 3;
    const int cos_idx = (imm5 >> 0) & 3;

    for (int i = 0; i < get_element_count<type>(); i++) {
        if (i == cos_idx) {
            vd.e[i].flt = cos;
        } else if ((i == sin_idx) || (cos_idx == sin_idx)) {
            vd.e[i].flt = sin;
        } else {
            vd.e[i].flt = 0.0;
        }
    }
    
    cpu->set_matrix_file<type>(VD, vd.e);
    return 8;
}

template<Vfpu::MatrixType type>
static i64 i_vrsq(Allegrex* cpu, const u32 instr) {
    Vec4 vd, vs;

    cpu->get_matrix_file<type>(VS, vs.e);

    if constexpr (type == Vfpu::MatrixType::Scalar) {
        cpu->decorate_src(vs);
    }

    for (int i = 0; i < get_element_count<type>(); i++) {
        vd.e[i].flt = 1.0 / std::sqrtf(vs.e[i].flt);
    }

    if constexpr (type == Vfpu::MatrixType::Scalar) {
        cpu->decorate_dst(vd);
        cpu->clear_decorators();
        cpu->set_matrix_file<type>(VD, vd.e, vd.write_mask);
    } else {
        cpu->set_matrix_file<type>(VD, vd.e);
    }
    
    return 6 + get_element_count<type>();
}

template<Vfpu::MatrixType type>
static i64 i_vsat0(Allegrex* cpu, const u32 instr) {
    Vec4 vd, vs;

    cpu->get_matrix_file<type>(VS, vs.e);
    cpu->decorate_src(vs);

    for (int i = 0; i < get_element_count<type>(); i++) {
        if (vs.e[i].flt < 0.0) {
            vd.e[i].flt = 0.0;
        } else if (vs.e[i].flt > 1.0) {
            vd.e[i].flt = 1.0;
        } else {
            vd.e[i].flt = vs.e[i].flt;
        }
    }

    cpu->set_matrix_file<type>(VD, vd.e);
    return 3;
}

template<Vfpu::MatrixType type>
static i64 i_vscl(Allegrex* cpu, const u32 instr) {
    Vec4 vd, vs, vt;

    cpu->get_matrix_file<type>(VS, vs.e);
    cpu->get_matrix_file<Vfpu::MatrixType::Scalar>(VT, vt.e);
    cpu->decorate_src(vs);

    for (int i = 0; i < get_element_count<type>(); i++) {
        vd.e[i].flt = vs.e[i].flt * vt.e[0].flt;
    }

    cpu->decorate_dst(vd);
    cpu->clear_decorators();
    cpu->set_matrix_file<type>(VD, vd.e, vd.write_mask);
    return 5;
}

template<Vfpu::MatrixType type>
static i64 i_vsin(Allegrex* cpu, const u32 instr) {
    Vec4 vd, vs;

    cpu->get_matrix_file<type>(VS, vs.e);

    if constexpr (type == Vfpu::MatrixType::Scalar) {
        cpu->decorate_src(vs);
    }

    for (int i = 0; i < get_element_count<type>(); i++) {
        vd.e[i].flt = std::sinf(M_PI_2 * vs.e[i].flt);
    }

    if constexpr (type == Vfpu::MatrixType::Scalar) {
        cpu->decorate_dst(vd);
        cpu->clear_decorators();
        cpu->set_matrix_file<type>(VD, vd.e, vd.write_mask);
    } else {
        cpu->set_matrix_file<type>(VD, vd.e);
    }
    
    return 6 + get_element_count<type>();
}

template<Vfpu::MatrixType type>
static i64 i_vsqrt(Allegrex* cpu, const u32 instr) {
    Vec4 vd, vs;

    cpu->get_matrix_file<type>(VS, vs.e);

    if constexpr (type == Vfpu::MatrixType::Scalar) {
        cpu->decorate_src(vs);
    }

    for (int i = 0; i < get_element_count<type>(); i++) {
        vd.e[i].flt = std::sqrtf(vs.e[i].flt);
    }

    if constexpr (type == Vfpu::MatrixType::Scalar) {
        cpu->decorate_dst(vd);
        cpu->clear_decorators();
        cpu->set_matrix_file<type>(VD, vd.e, vd.write_mask);
    } else {
        cpu->set_matrix_file<type>(VD, vd.e);
    }
    
    return 6 + get_element_count<type>();
}

template<Vfpu::MatrixType type>
static i64 i_vsub(Allegrex* cpu, const u32 instr) {
    Vec4 vd, vs, vt;

    cpu->get_matrix_file<type>(VS, vs.e);
    cpu->get_matrix_file<type>(VT, vt.e);
    cpu->decorate_src(vs);
    cpu->decorate_tgt(vt);

    for (int i = 0; i < get_element_count<type>(); i++) {
        vd.e[i].flt = vs.e[i].flt - vt.e[i].flt;
    }

    cpu->decorate_dst(vd);
    cpu->clear_decorators();
    cpu->set_matrix_file<type>(VD, vd.e, vd.write_mask);
    return 5;
}

static i64 i_vtfm3t(Allegrex* cpu, const u32 instr) {
    Vec4 vd, vt;
    Mat4 mtxs;

    cpu->get_matrix_file<Vfpu::MatrixType::TripleMatrix>(VS, mtxs);
    cpu->get_matrix_file<Vfpu::MatrixType::TripleVector>(VT, vt.e);

    for (int i = 0; i < 3; i++) {
        vd.e[i].flt = mtxs[i].flt * vt.e[0].flt + mtxs[i + 4].flt * vt.e[1].flt + mtxs[i + 8].flt * vt.e[2].flt;
    }

    cpu->set_matrix_file<Vfpu::MatrixType::TripleVector>(VD, vd.e);
    return 9;
}

static i64 i_vtfm4t(Allegrex* cpu, const u32 instr) {
    Vec4 vd, vt;
    Mat4 mtxs;

    cpu->get_matrix_file<Vfpu::MatrixType::QuadMatrix>(VS, mtxs);
    cpu->get_matrix_file<Vfpu::MatrixType::QuadVector>(VT, vt.e);

    for (int i = 0; i < 4; i++) {
        vd.e[i].flt = mtxs[i].flt * vt.e[0].flt + mtxs[i + 4].flt * vt.e[1].flt + mtxs[i + 8].flt * vt.e[2].flt + mtxs[i + 12].flt * vt.e[3].flt;
    }

    cpu->set_matrix_file<Vfpu::MatrixType::QuadVector>(VD, vd.e);
    return 10;
}

static i64 i_vwbns(Allegrex* cpu, const u32 instr) {
    Vec4 vd, vs;

    cpu->get_matrix_file<Vfpu::MatrixType::Scalar>(VS, vs.e);
    cpu->decorate_src(vs);

    const i32 imm8 = (instr >> 16) & 0xFF;

    assert((imm8 >= 1) && imm8 <= 254);

    const f32 p = std::exp2f(imm8 - 127);

    vd.e[0].flt = std::fmodf(vs.e[0].flt, p) + ((vs.e[0].flt < 0.0) ? -p : p);

    cpu->decorate_dst(vd);
    cpu->clear_decorators();
    cpu->set_matrix_file<Vfpu::MatrixType::Scalar>(VD, vd.e, vd.write_mask);
    return 5;
}

template<Vfpu::MatrixType type>
static i64 i_vzero(Allegrex* cpu, const u32 instr) {
    assert(VS == 0);

    Vec4 vd;

    for (int i = 0; i < get_element_count<type>(); i++) {
        vd.e[i].flt = 0.0;
    }

    cpu->decorate_dst(vd);
    cpu->clear_decorators();
    cpu->set_matrix_file<type>(VD, vd.e, vd.write_mask);
    return 3;
}

static i64 i_wsbh(Allegrex* cpu, const u32 instr) {
    const u32 t = cpu->get_reg(RT);

    cpu->set_reg(RD, ((t & 0xFF) << 8) | ((t & 0xFF00) >> 8) | ((t & 0xFF0000) << 8) | ((t & 0xFF000000) >> 8));
    return 1;
}

static i64 i_wsbw(Allegrex* cpu, const u32 instr) {
    const u32 t = cpu->get_reg(RT);

    cpu->set_reg(RD, (t >> 24) | (t << 24) | ((t & 0xFF0000) >> 8) | ((t & 0xFF00) << 8));
    return 1;
}

static i64 i_xor(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RD, cpu->get_reg(RS) ^ cpu->get_reg(RT));
    return 1;
}

static i64 i_xori(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RT, cpu->get_reg(RS) ^ UIMM);
    return 1;
}

static i64 i_bit_shuffle(Allegrex* cpu, const u32 instr) {
    switch (SA) {
        case BitShuffleOpcode::BIT_SHUFFLE_OPCODE_WSBH:
            return i_wsbh(cpu, instr);
        case BitShuffleOpcode::BIT_SHUFFLE_OPCODE_WSBW:
            return i_wsbw(cpu, instr);
        case BitShuffleOpcode::BIT_SHUFFLE_OPCODE_SEB:
            return i_seb(cpu, instr);
        case BitShuffleOpcode::BIT_SHUFFLE_OPCODE_BITREV:
            return i_bitrev(cpu, instr);
        case BitShuffleOpcode::BIT_SHUFFLE_OPCODE_SEH:
            return i_seh(cpu, instr);
        default:
            cpu->get_logger()->error("Undefined BSHFL instruction {:02X} ({:08X}) @ {:08X}", SA, instr, cpu->get_instr_addr());
            cpu->dump_state();
            exit(1);
    }
}

static i64 i_cache(Allegrex* cpu, const u32 instr) {
    const u32 addr = cpu->get_reg(RS) + (i32)(i16)UIMM;

    switch (RT) {
        default:
            if constexpr (!SILENT_CACHE) cpu->get_logger()->warn("Unimplemented CACHE instruction {:02X} ({:08X}) for address {:08X}", RT, instr, addr);
            break;
    }

    return 1;
}

template<int cop_num>
static i64 i_cop(Allegrex* cpu, const u32 instr) {
    if (!cpu->is_coprocessor_usable(cop_num)) {
        return 1;
    }

    switch (RS) {
        case CopOpcode::COP_OPCODE_MFC:
            return i_mfc<cop_num>(cpu, instr);
        case CopOpcode::COP_OPCODE_CFC:
            return i_cfc<cop_num>(cpu, instr);
        case CopOpcode::COP_OPCODE_MFVC:
            assert(cop_num == CopNum::COP_NUM_VFPU);

            return i_mfvc(cpu, instr);
        case CopOpcode::COP_OPCODE_MTC:
            return i_mtc<cop_num>(cpu, instr);
        case CopOpcode::COP_OPCODE_CTC:
            return i_ctc<cop_num>(cpu, instr);
        case CopOpcode::COP_OPCODE_MTVC:
            assert(cop_num == CopNum::COP_NUM_VFPU);

            return i_mtvc(cpu, instr);
        case CopOpcode::COP_OPCODE_BC:
            switch (RT & 3) {
                case CopBranchOpcode::COP_BRANCH_OPCODE_BCF:
                    return i_bcf<cop_num>(cpu, instr);
                case CopBranchOpcode::COP_BRANCH_OPCODE_BCT:
                    return i_bct<cop_num>(cpu, instr);
                case CopBranchOpcode::COP_BRANCH_OPCODE_BCFL:
                    return i_bcfl<cop_num>(cpu, instr);
                case CopBranchOpcode::COP_BRANCH_OPCODE_BCTL:
                    return i_bctl<cop_num>(cpu, instr);
                default:
                    cpu->get_logger()->error("Undefined CP{} BC instruction {:02X} ({:08X}) @ {:08X}", cop_num, RT, instr, cpu->get_instr_addr());
                    cpu->dump_state();
                    exit(1);
            }
        case CopOpcode::COP_OPCODE_SECONDARY:
            assert(cop_num <= COP_NUM_FPU);

            if (cop_num == COP_NUM_CP0) {
                switch (FUNCT) {
                    case Cp0Opcode::CP0_OPCODE_ERET:
                        return i_eret(cpu);
                    default:
                        cpu->get_logger()->error("Undefined CP0 secondary instruction {:02X} ({:08X}) @ {:08X}", FUNCT, instr, cpu->get_instr_addr());
                        cpu->dump_state();
                        exit(1);
                }
            } else {
                if (FUNCT >= FpuOpcode::FPU_OPCODE_C) {
                    return i_fc(cpu, instr);
                }

                switch (FUNCT) {
                    case FpuOpcode::FPU_OPCODE_ADD:
                        return i_fadd(cpu, instr);
                    case FpuOpcode::FPU_OPCODE_SUB:
                        return i_fsub(cpu, instr);
                    case FpuOpcode::FPU_OPCODE_MUL:
                        return i_fmul(cpu, instr);
                    case FpuOpcode::FPU_OPCODE_DIV:
                        return i_fdiv(cpu, instr);
                    case FpuOpcode::FPU_OPCODE_SQRT:
                        return i_fsqrt(cpu, instr);
                    case FpuOpcode::FPU_OPCODE_ABS:
                        return i_fabs(cpu, instr);
                    case FpuOpcode::FPU_OPCODE_MOV:
                        return i_fmov(cpu, instr);
                    case FpuOpcode::FPU_OPCODE_NEG:
                        return i_fneg(cpu, instr);
                    case FpuOpcode::FPU_OPCODE_ROUNDW:
                        return i_roundw(cpu, instr);
                    case FpuOpcode::FPU_OPCODE_TRUNCW:
                        return i_truncw(cpu, instr);
                    case FpuOpcode::FPU_OPCODE_CEILW:
                        return i_ceilw(cpu, instr);
                    default:
                        cpu->get_logger()->error("Undefined FPU SINGLE instruction {:02X} ({:08X}) @ {:08X}", FUNCT, instr, cpu->get_instr_addr());
                        cpu->dump_state();
                        exit(1);
                }
            }
        case CopOpcode::COP_OPCODE_WORD:
            assert(cop_num == COP_NUM_FPU);

            switch (FUNCT) {
                case FpuOpcode::FPU_OPCODE_CVTS:
                    return i_cvts(cpu, instr);
                default:
                    cpu->get_logger()->error("Undefined FPU WORD instruction {:02X} ({:08X}) @ {:08X}", FUNCT, instr, cpu->get_instr_addr());
                    cpu->dump_state();
                    exit(1);
            }
        default:
            cpu->get_logger()->error("Undefined COP{} primary instruction {:02X} ({:08X}) @ {:08X}", cop_num, RS, instr, cpu->get_instr_addr());
            cpu->dump_state();
            exit(1);
    }
}

static i64 i_regimm(Allegrex* cpu, const u32 instr) {
    switch (RT) {
        case RegimmOpcode::REGIMM_OPCODE_BLTZ:
            return i_bltz(cpu, instr);
        case RegimmOpcode::REGIMM_OPCODE_BGEZ:
            return i_bgez(cpu, instr);
        case RegimmOpcode::REGIMM_OPCODE_BLTZL:
            return i_bltzl(cpu, instr);
        case RegimmOpcode::REGIMM_OPCODE_BGEZL:
            return i_bgezl(cpu, instr);
        case RegimmOpcode::REGIMM_OPCODE_BLTZAL:
            return i_bltzal(cpu, instr);
        case RegimmOpcode::REGIMM_OPCODE_BGEZAL:
            return i_bgezal(cpu, instr);
        default:
            cpu->get_logger()->error("Undefined REGIMM instruction {:02X} ({:08X}) @ {:08X}", RT, instr, cpu->get_instr_addr());
            cpu->dump_state();
            exit(1);
    }
}

static i64 i_shift_right(Allegrex* cpu, const u32 instr) {
    if ((RS & 1) == 0) {
        return i_srl(cpu, instr);
    } else {
        return i_rotr(cpu, instr);
    }
}

static i64 i_shift_right_variable(Allegrex* cpu, const u32 instr) {
    if ((SA & 1) == 0) {
        return i_srlv(cpu, instr);
    } else {
        return i_rotrv(cpu, instr);
    }
}

static i64 i_special(Allegrex* cpu, const u32 instr) {
    return special_table[FUNCT](cpu, instr);
}

static i64 i_special2(Allegrex* cpu, const u32 instr) {
    switch (FUNCT) {
        case Special2Opcode::SPECIAL2_OPCODE_HALT:
            return i_halt(cpu);
        case Special2Opcode::SPECIAL2_OPCODE_MFIC:
            return i_mfic(cpu, instr);
        case Special2Opcode::SPECIAL2_OPCODE_MTIC:
            return i_mtic(cpu, instr);
        default:
            cpu->get_logger()->error("Undefined SPECIAL2 instruction {:02X} ({:08X}) @ {:08X}", FUNCT, instr, cpu->get_instr_addr());
            cpu->dump_state();
            exit(1);
    }
}

static i64 i_special3(Allegrex* cpu, const u32 instr) {
    switch (FUNCT) {
        case Special3Opcode::SPECIAL3_OPCODE_EXT:
            return i_ext(cpu, instr);
        case Special3Opcode::SPECIAL3_OPCODE_INS:
            return i_ins(cpu, instr);
        case Special3Opcode::SPECIAL3_OPCODE_BSHFL:
            return i_bit_shuffle(cpu, instr);
        default:
            cpu->get_logger()->error("Undefined SPECIAL3 instruction {:02X} ({:08X}) @ {:08X}", FUNCT, instr, cpu->get_instr_addr());
            cpu->dump_state();
            exit(1);
    }
}

enum Vfpu0Opcode {
    VFPU0_OPCODE_VADD  = 0,
    VFPU0_OPCODE_VSUB  = 1,
    VFPU0_OPCODE_VDIVS = 7,
};

enum Vfpu1Opcode {
    VFPU1_OPCODE_VMUL = 0,
    VFPU1_OPCODE_VDOT = 1,
    VFPU1_OPCODE_VSCL = 2,
};

enum Vfpu3Opcode {
    VFPU3_OPCODE_VCMP = 0,
    VFPU3_OPCODE_VMIN = 2,
    VFPU3_OPCODE_VMAX = 3,
};

enum Vfpu4Opcode {
    VFPU4_OPCODE_VMOV   = 0x000,
    VFPU4_OPCODE_VABS   = 0x001,
    VFPU4_OPCODE_VNEG   = 0x002,
    VFPU4_OPCODE_VIDT   = 0x003,
    VFPU4_OPCODE_VSAT0  = 0x004,
    VFPU4_OPCODE_VZERO  = 0x006,
    VFPU4_OPCODE_VONE   = 0x007,
    VFPU4_OPCODE_VRCP   = 0x010,
    VFPU4_OPCODE_VRSQ   = 0x011,
    VFPU4_OPCODE_VSIN   = 0x012,
    VFPU4_OPCODE_VCOS   = 0x013,
    VFPU4_OPCODE_VEXP2  = 0x014,
    VFPU4_OPCODE_VLOG2  = 0x015,
    VFPU4_OPCODE_VSQRT  = 0x016,
    VFPU4_OPCODE_VASIN  = 0x017,
    VFPU4_OPCODE_VRNDF1 = 0x022,
    VFPU4_OPCODE_VI2UCQ = 0x03C,
    VFPU4_OPCODE_VOCP   = 0x044,
};

enum Vfpu5Opcode {
    VFPU5_OPCODE_VPFXS = 0,
    VFPU5_OPCODE_VPFXT = 2,
    VFPU5_OPCODE_VPFXD = 4,
    VFPU5_OPCODE_VIIMS = 6,
    VFPU5_OPCODE_VFIMS = 7,
};

enum Vfpu6Opcode {
    VFPU6_OPCODE_VMMUL  = 0,
    VFPU6_OPCODE_VTFM3T = 2,
    VFPU6_OPCODE_VTFM4T = 3,
    VFPU6_OPCODE_VMSCL  = 4,
};

static i64 i_vfpu0(Allegrex* cpu, const u32 instr) {
    assert(cpu->get_cpu_id() == CpuId::CPU_ID_SC);

    if (!cpu->is_coprocessor_usable(2)) {
        return 1;
    }

    const u32 opcode = (instr >> 23) & 7;
    const u32 format = ((instr >> 14) & 2) | ((instr >> 7) & 1);

    switch (opcode) {
        case Vfpu0Opcode::VFPU0_OPCODE_VADD:
            switch (format) {
                case 0:
                    return i_vadd<Vfpu::MatrixType::Scalar>(cpu, instr);
                case 1:
                    return i_vadd<Vfpu::MatrixType::PairVector>(cpu, instr);
                case 2:
                    return i_vadd<Vfpu::MatrixType::TripleVector>(cpu, instr);
                case 3:
                    return i_vadd<Vfpu::MatrixType::QuadVector>(cpu, instr);
            }
        case Vfpu0Opcode::VFPU0_OPCODE_VSUB:
            switch (format) {
                case 0:
                    return i_vsub<Vfpu::MatrixType::Scalar>(cpu, instr);
                case 1:
                    return i_vsub<Vfpu::MatrixType::PairVector>(cpu, instr);
                case 2:
                    return i_vsub<Vfpu::MatrixType::TripleVector>(cpu, instr);
                case 3:
                    return i_vsub<Vfpu::MatrixType::QuadVector>(cpu, instr);
            }
        case Vfpu0Opcode::VFPU0_OPCODE_VDIVS:
            switch (format) {
                case 0:
                    return i_vdiv<Vfpu::MatrixType::Scalar>(cpu, instr);
                case 1:
                    return i_vdiv<Vfpu::MatrixType::PairVector>(cpu, instr);
                case 2:
                    return i_vdiv<Vfpu::MatrixType::TripleVector>(cpu, instr);
                case 3:
                    return i_vdiv<Vfpu::MatrixType::QuadVector>(cpu, instr);
            }
        default:
            cpu->get_logger()->error("Unimplemented VFPU0 instruction {} ({:08X}) @ {:08X}", opcode, instr, cpu->get_instr_addr());
            exit(1);
    }
}

static i64 i_vfpu1(Allegrex* cpu, const u32 instr) {
    assert(cpu->get_cpu_id() == CpuId::CPU_ID_SC);

    if (!cpu->is_coprocessor_usable(2)) {
        return 1;
    }

    const u32 opcode = (instr >> 23) & 7;
    const u32 format = ((instr >> 14) & 2) | ((instr >> 7) & 1);

    switch (opcode) {
        case Vfpu1Opcode::VFPU1_OPCODE_VMUL:
            switch (format) {
                case 0:
                    return i_vmul<Vfpu::MatrixType::Scalar>(cpu, instr);
                case 1:
                    return i_vmul<Vfpu::MatrixType::PairVector>(cpu, instr);
                case 2:
                    return i_vmul<Vfpu::MatrixType::TripleVector>(cpu, instr);
                case 3:
                    return i_vmul<Vfpu::MatrixType::QuadVector>(cpu, instr);
            }
        case Vfpu1Opcode::VFPU1_OPCODE_VDOT:
            switch (format) {
                case 1:
                    return i_vdot<Vfpu::MatrixType::PairVector>(cpu, instr);
                case 2:
                    return i_vdot<Vfpu::MatrixType::TripleVector>(cpu, instr);
                case 3:
                    return i_vdot<Vfpu::MatrixType::QuadVector>(cpu, instr);
                default:
                    cpu->get_logger()->error("Invalid Scalar format for VDOT");
                    exit(1);
            }
        case Vfpu1Opcode::VFPU1_OPCODE_VSCL:
            switch (format) {
                case 1:
                    return i_vscl<Vfpu::MatrixType::PairVector>(cpu, instr);
                case 2:
                    return i_vscl<Vfpu::MatrixType::TripleVector>(cpu, instr);
                case 3:
                    return i_vscl<Vfpu::MatrixType::QuadVector>(cpu, instr);
                default:
                    cpu->get_logger()->error("Invalid Scalar format for VSCL");
                    exit(1);
            }
        default:
            cpu->get_logger()->error("Unimplemented VFPU1 instruction {} ({:08X}) @ {:08X}", opcode, instr, cpu->get_instr_addr());
            exit(1);
    }
}

static i64 i_vfpu3(Allegrex* cpu, const u32 instr) {
    assert(cpu->get_cpu_id() == CpuId::CPU_ID_SC);

    if (!cpu->is_coprocessor_usable(2)) {
        return 1;
    }

    const u32 opcode = (instr >> 23) & 7;
    const u32 format = ((instr >> 14) & 2) | ((instr >> 7) & 1);

    switch (opcode) {
        case Vfpu3Opcode::VFPU3_OPCODE_VCMP:
            switch (format) {
                case 0:
                    return i_vcmp<Vfpu::MatrixType::Scalar>(cpu, instr);
                case 1:
                    return i_vcmp<Vfpu::MatrixType::PairVector>(cpu, instr);
                case 2:
                    return i_vcmp<Vfpu::MatrixType::TripleVector>(cpu, instr);
                case 3:
                    return i_vcmp<Vfpu::MatrixType::QuadVector>(cpu, instr);
            }
        case Vfpu3Opcode::VFPU3_OPCODE_VMIN:
            switch (format) {
                case 0:
                    return i_vmin<Vfpu::MatrixType::Scalar>(cpu, instr);
                case 1:
                    return i_vmin<Vfpu::MatrixType::PairVector>(cpu, instr);
                case 2:
                    return i_vmin<Vfpu::MatrixType::TripleVector>(cpu, instr);
                case 3:
                    return i_vmin<Vfpu::MatrixType::QuadVector>(cpu, instr);
            }
        case Vfpu3Opcode::VFPU3_OPCODE_VMAX:
            switch (format) {
                case 0:
                    return i_vmax<Vfpu::MatrixType::Scalar>(cpu, instr);
                case 1:
                    return i_vmax<Vfpu::MatrixType::PairVector>(cpu, instr);
                case 2:
                    return i_vmax<Vfpu::MatrixType::TripleVector>(cpu, instr);
                case 3:
                    return i_vmax<Vfpu::MatrixType::QuadVector>(cpu, instr);
            }
        default:
            cpu->get_logger()->error("Unimplemented VFPU3 instruction {} ({:08X}) @ {:08X}", opcode, instr, cpu->get_instr_addr());
            exit(1);
    }
}

static i64 i_vfpu4(Allegrex* cpu, const u32 instr) {
    assert(cpu->get_cpu_id() == CpuId::CPU_ID_SC);

    if (!cpu->is_coprocessor_usable(2)) {
        return 1;
    }

    const u32 opcode = (instr >> 16) & 0x3FF;
    const u32 format = ((instr >> 14) & 2) | ((instr >> 7) & 1);

    if ((opcode >> 8) == 3) {
        assert(format == 0);

        return i_vwbns(cpu, instr);
    }

    switch (opcode >> 5) {
        case 0x00:
        case 0x01:
        case 0x02:
            break;
        case 0x03:
            switch (format) {
                case 0:
                    return i_vcst<Vfpu::MatrixType::Scalar>(cpu, instr);
                case 1:
                    return i_vcst<Vfpu::MatrixType::PairVector>(cpu, instr);
                case 2:
                    return i_vcst<Vfpu::MatrixType::TripleVector>(cpu, instr);
                case 3:
                    return i_vcst<Vfpu::MatrixType::QuadVector>(cpu, instr);
            }
        case 0x11:
            switch (format) {
                case 0:
                    return i_vf2iz<Vfpu::MatrixType::Scalar>(cpu, instr);
                case 1:
                    return i_vf2iz<Vfpu::MatrixType::PairVector>(cpu, instr);
                case 2:
                    return i_vf2iz<Vfpu::MatrixType::TripleVector>(cpu, instr);
                case 3:
                    return i_vf2iz<Vfpu::MatrixType::QuadVector>(cpu, instr);
            }
        case 0x15:
            if ((opcode & 0x10) == 0) {
                switch (format) {
                    case 0:
                        return i_vcmovt<Vfpu::MatrixType::Scalar>(cpu, instr);
                    case 1:
                        return i_vcmovt<Vfpu::MatrixType::PairVector>(cpu, instr);
                    case 2:
                        return i_vcmovt<Vfpu::MatrixType::TripleVector>(cpu, instr);
                    case 3:
                        return i_vcmovt<Vfpu::MatrixType::QuadVector>(cpu, instr);
                }
            } else {
                cpu->get_logger()->error("Unimplemented VCMOVF");
                exit(1);
            }
        default:
            cpu->get_logger()->error("Unimplemented VFPU4 imm5 instruction {:02X} ({:08X}) @ {:08X}", opcode >> 5, instr, cpu->get_instr_addr());
            exit(1);
    }

    switch (opcode) {
        case Vfpu4Opcode::VFPU4_OPCODE_VMOV:
            switch (format) {
                case 0:
                    return i_vmov<Vfpu::MatrixType::Scalar>(cpu, instr);
                case 1:
                    return i_vmov<Vfpu::MatrixType::PairVector>(cpu, instr);
                case 2:
                    return i_vmov<Vfpu::MatrixType::TripleVector>(cpu, instr);
                case 3:
                    return i_vmov<Vfpu::MatrixType::QuadVector>(cpu, instr);
            }
        case Vfpu4Opcode::VFPU4_OPCODE_VABS:
            switch (format) {
                case 0:
                    return i_vabs<Vfpu::MatrixType::Scalar>(cpu, instr);
                case 1:
                    return i_vabs<Vfpu::MatrixType::PairVector>(cpu, instr);
                case 2:
                    return i_vabs<Vfpu::MatrixType::TripleVector>(cpu, instr);
                case 3:
                    return i_vabs<Vfpu::MatrixType::QuadVector>(cpu, instr);
            }
        case Vfpu4Opcode::VFPU4_OPCODE_VNEG:
            switch (format) {
                case 0:
                    return i_vneg<Vfpu::MatrixType::Scalar>(cpu, instr);
                case 1:
                    return i_vneg<Vfpu::MatrixType::PairVector>(cpu, instr);
                case 2:
                    return i_vneg<Vfpu::MatrixType::TripleVector>(cpu, instr);
                case 3:
                    return i_vneg<Vfpu::MatrixType::QuadVector>(cpu, instr);
            }
        case Vfpu4Opcode::VFPU4_OPCODE_VIDT:
            switch (format) {
                case 1:
                    return i_vidt<Vfpu::MatrixType::PairVector>(cpu, instr);
                case 3:
                    return i_vidt<Vfpu::MatrixType::QuadVector>(cpu, instr);
                default:
                    cpu->get_logger()->error("Invalid format for VIDT");
                    exit(1);
            }
        case Vfpu4Opcode::VFPU4_OPCODE_VSAT0:
            switch (format) {
                case 0:
                    return i_vsat0<Vfpu::MatrixType::Scalar>(cpu, instr);
                case 1:
                    return i_vsat0<Vfpu::MatrixType::PairVector>(cpu, instr);
                case 2:
                    return i_vsat0<Vfpu::MatrixType::TripleVector>(cpu, instr);
                case 3:
                    return i_vsat0<Vfpu::MatrixType::QuadVector>(cpu, instr);
            }
        case Vfpu4Opcode::VFPU4_OPCODE_VZERO:
            switch (format) {
                case 0:
                    return i_vzero<Vfpu::MatrixType::Scalar>(cpu, instr);
                case 1:
                    return i_vzero<Vfpu::MatrixType::PairVector>(cpu, instr);
                case 2:
                    return i_vzero<Vfpu::MatrixType::TripleVector>(cpu, instr);
                case 3:
                    return i_vzero<Vfpu::MatrixType::QuadVector>(cpu, instr);
            }
        case Vfpu4Opcode::VFPU4_OPCODE_VONE:
            switch (format) {
                case 0:
                    return i_vone<Vfpu::MatrixType::Scalar>(cpu, instr);
                case 1:
                    return i_vone<Vfpu::MatrixType::PairVector>(cpu, instr);
                case 2:
                    return i_vone<Vfpu::MatrixType::TripleVector>(cpu, instr);
                case 3:
                    return i_vone<Vfpu::MatrixType::QuadVector>(cpu, instr);
            }
        case Vfpu4Opcode::VFPU4_OPCODE_VRCP:
            switch (format) {
                case 0:
                    return i_vrcp<Vfpu::MatrixType::Scalar>(cpu, instr);
                case 1:
                    return i_vrcp<Vfpu::MatrixType::PairVector>(cpu, instr);
                case 2:
                    return i_vrcp<Vfpu::MatrixType::TripleVector>(cpu, instr);
                case 3:
                    return i_vrcp<Vfpu::MatrixType::QuadVector>(cpu, instr);
            }
        case Vfpu4Opcode::VFPU4_OPCODE_VRSQ:
            switch (format) {
                case 0:
                    return i_vrsq<Vfpu::MatrixType::Scalar>(cpu, instr);
                case 1:
                    return i_vrsq<Vfpu::MatrixType::PairVector>(cpu, instr);
                case 2:
                    return i_vrsq<Vfpu::MatrixType::TripleVector>(cpu, instr);
                case 3:
                    return i_vrsq<Vfpu::MatrixType::QuadVector>(cpu, instr);
            }
        case Vfpu4Opcode::VFPU4_OPCODE_VSIN:
            switch (format) {
                case 0:
                    return i_vsin<Vfpu::MatrixType::Scalar>(cpu, instr);
                case 1:
                    return i_vsin<Vfpu::MatrixType::PairVector>(cpu, instr);
                case 2:
                    return i_vsin<Vfpu::MatrixType::TripleVector>(cpu, instr);
                case 3:
                    return i_vsin<Vfpu::MatrixType::QuadVector>(cpu, instr);
            }
        case Vfpu4Opcode::VFPU4_OPCODE_VCOS:
            switch (format) {
                case 0:
                    return i_vcos<Vfpu::MatrixType::Scalar>(cpu, instr);
                case 1:
                    return i_vcos<Vfpu::MatrixType::PairVector>(cpu, instr);
                case 2:
                    return i_vcos<Vfpu::MatrixType::TripleVector>(cpu, instr);
                case 3:
                    return i_vcos<Vfpu::MatrixType::QuadVector>(cpu, instr);
            }
        case Vfpu4Opcode::VFPU4_OPCODE_VEXP2:
            switch (format) {
                case 0:
                    return i_vexp2<Vfpu::MatrixType::Scalar>(cpu, instr);
                case 1:
                    return i_vexp2<Vfpu::MatrixType::PairVector>(cpu, instr);
                case 2:
                    return i_vexp2<Vfpu::MatrixType::TripleVector>(cpu, instr);
                case 3:
                    return i_vexp2<Vfpu::MatrixType::QuadVector>(cpu, instr);
            }
        case Vfpu4Opcode::VFPU4_OPCODE_VLOG2:
            switch (format) {
                case 0:
                    return i_vlog2<Vfpu::MatrixType::Scalar>(cpu, instr);
                case 1:
                    return i_vlog2<Vfpu::MatrixType::PairVector>(cpu, instr);
                case 2:
                    return i_vlog2<Vfpu::MatrixType::TripleVector>(cpu, instr);
                case 3:
                    return i_vlog2<Vfpu::MatrixType::QuadVector>(cpu, instr);
            }
        case Vfpu4Opcode::VFPU4_OPCODE_VSQRT:
            switch (format) {
                case 0:
                    return i_vsqrt<Vfpu::MatrixType::Scalar>(cpu, instr);
                case 1:
                    return i_vsqrt<Vfpu::MatrixType::PairVector>(cpu, instr);
                case 2:
                    return i_vsqrt<Vfpu::MatrixType::TripleVector>(cpu, instr);
                case 3:
                    return i_vsqrt<Vfpu::MatrixType::QuadVector>(cpu, instr);
            }
        case Vfpu4Opcode::VFPU4_OPCODE_VASIN:
            switch (format) {
                case 0:
                    return i_vasin<Vfpu::MatrixType::Scalar>(cpu, instr);
                case 1:
                    return i_vasin<Vfpu::MatrixType::PairVector>(cpu, instr);
                case 2:
                    return i_vasin<Vfpu::MatrixType::TripleVector>(cpu, instr);
                case 3:
                    return i_vasin<Vfpu::MatrixType::QuadVector>(cpu, instr);
            }
        case Vfpu4Opcode::VFPU4_OPCODE_VRNDF1:
            switch (format) {
                case 0:
                    return i_vrndf1<Vfpu::MatrixType::Scalar>(cpu, instr);
                case 1:
                    return i_vrndf1<Vfpu::MatrixType::PairVector>(cpu, instr);
                case 2:
                    return i_vrndf1<Vfpu::MatrixType::TripleVector>(cpu, instr);
                case 3:
                    return i_vrndf1<Vfpu::MatrixType::QuadVector>(cpu, instr);
            }
        case Vfpu4Opcode::VFPU4_OPCODE_VI2UCQ:
            assert(format == 3);

            return i_vi2ucq(cpu, instr);
        case Vfpu4Opcode::VFPU4_OPCODE_VOCP:
            switch (format) {
                case 0:
                    return i_vocp<Vfpu::MatrixType::Scalar>(cpu, instr);
                case 1:
                    return i_vocp<Vfpu::MatrixType::PairVector>(cpu, instr);
                case 2:
                    return i_vocp<Vfpu::MatrixType::TripleVector>(cpu, instr);
                case 3:
                    return i_vocp<Vfpu::MatrixType::QuadVector>(cpu, instr);
            }
        default:
            cpu->get_logger()->error("Unimplemented VFPU4 instruction {:03X} ({:08X}) @ {:08X}", opcode, instr, cpu->get_instr_addr());
            exit(1);
    }
}

static i64 i_vfpu5(Allegrex* cpu, const u32 instr) {
    assert(cpu->get_cpu_id() == CpuId::CPU_ID_SC);

    if (!cpu->is_coprocessor_usable(2)) {
        return 1;
    }

    const u32 opcode = (instr >> 23) & 7;

    switch (opcode) {
        case Vfpu5Opcode::VFPU5_OPCODE_VPFXS + 0:
        case Vfpu5Opcode::VFPU5_OPCODE_VPFXS + 1:
            return i_vpfxs(cpu, instr);
        case Vfpu5Opcode::VFPU5_OPCODE_VPFXT + 0:
        case Vfpu5Opcode::VFPU5_OPCODE_VPFXT + 1:
            return i_vpfxt(cpu, instr);
        case Vfpu5Opcode::VFPU5_OPCODE_VPFXD + 0:
        case Vfpu5Opcode::VFPU5_OPCODE_VPFXD + 1:
            return i_vpfxd(cpu, instr);
        case Vfpu5Opcode::VFPU5_OPCODE_VIIMS:
            return i_viims(cpu, instr);
        case Vfpu5Opcode::VFPU5_OPCODE_VFIMS:
            return i_vfims(cpu, instr);
    }

    // Unreachable, we have all VFPU5 ops
    exit(1);
}

static i64 i_vfpu6(Allegrex* cpu, const u32 instr) {
    assert(cpu->get_cpu_id() == CpuId::CPU_ID_SC);

    if (!cpu->is_coprocessor_usable(2)) {
        return 1;
    }

    const u32 opcode = (instr >> 23) & 7;
    const u32 format = ((instr >> 14) & 2) | ((instr >> 7) & 1);

    // Not sure how to cleanly decode these instructions lmao
    switch (opcode) {
        case Vfpu6Opcode::VFPU6_OPCODE_VMMUL:
            switch (format) {
                case 3:
                    return i_vmmulq(cpu, instr);
                default:
                    cpu->get_logger()->error("Unimplemented VMMUL format {} ({:08X}) @ {:08X}", format, instr, cpu->get_instr_addr());
                    exit(1);
            }
        case Vfpu6Opcode::VFPU6_OPCODE_VTFM3T:
            assert((format > 0) && (format < 3));

            switch (format) {
                case 1:
                    return i_vhtfm3t(cpu, instr);
                case 2:
                    return i_vtfm3t(cpu, instr);
            }
        case Vfpu6Opcode::VFPU6_OPCODE_VTFM4T:
            assert(format >= 2);

            switch (format) {
                case 2:
                    return i_vhtfm4t(cpu, instr);
                case 3:
                    return i_vtfm4t(cpu, instr);
            }
        case Vfpu6Opcode::VFPU6_OPCODE_VMSCL:
            switch (format) {
                case 1:
                    return i_vmscl<Vfpu::MatrixType::PairMatrix>(cpu, instr);
                case 2:
                    return i_vmscl<Vfpu::MatrixType::TripleMatrix>(cpu, instr);
                case 3:
                    return i_vmscl<Vfpu::MatrixType::QuadMatrix>(cpu, instr);
                default:
                    cpu->get_logger()->error("Invalid format for VMSCL");
                    exit(1);
            }
        case 5:
            assert(format >= 2);

            switch (format) {
                case 2:
                    return i_vcrspt(cpu, instr);
                case 3:
                    return i_vqmulq(cpu, instr);
            }
        case 7:
            switch (VT >> 5) {
                case 0:
                    break;
                case 1:
                    switch (format) {
                        case 1:
                            return i_vrot<Vfpu::MatrixType::PairVector>(cpu, instr);
                        case 2:
                            return i_vrot<Vfpu::MatrixType::TripleVector>(cpu, instr);
                        case 3:
                            return i_vrot<Vfpu::MatrixType::QuadVector>(cpu, instr);
                        default:
                            cpu->get_logger()->error("Invalid Scalar format for VROT");
                            exit(1);
                    }
                default:
                    cpu->get_logger()->error("Unimplemented VFPU6 op7 imm5 instruction {} ({:08X}) @ {:08X}", opcode, VT >> 5, instr, cpu->get_instr_addr());
                    exit(1);
            }

            switch (VT) {
                case 3:
                    switch (format) {
                        case 1:
                            return i_vmidt<Vfpu::MatrixType::PairMatrix>(cpu, instr);
                        case 2:
                            return i_vmidt<Vfpu::MatrixType::TripleMatrix>(cpu, instr);
                        case 3:
                            return i_vmidt<Vfpu::MatrixType::QuadMatrix>(cpu, instr);
                        default:
                            cpu->get_logger()->error("Invalid format for VMIDT");
                            exit(1);
                    }
                case 6:
                    switch (format) {
                        case 1:
                            return i_vmzero<Vfpu::MatrixType::PairMatrix>(cpu, instr);
                        case 2:
                            return i_vmzero<Vfpu::MatrixType::TripleMatrix>(cpu, instr);
                        case 3:
                            return i_vmzero<Vfpu::MatrixType::QuadMatrix>(cpu, instr);
                        default:
                            cpu->get_logger()->error("Invalid format for VMZERO");
                            exit(1);
                    }
                case 7:
                    switch (format) {
                        case 1:
                            return i_vmone<Vfpu::MatrixType::PairMatrix>(cpu, instr);
                        case 2:
                            return i_vmone<Vfpu::MatrixType::TripleMatrix>(cpu, instr);
                        case 3:
                            return i_vmone<Vfpu::MatrixType::QuadMatrix>(cpu, instr);
                        default:
                            cpu->get_logger()->error("Invalid format for VMONE");
                            exit(1);
                    }
                default:
                    cpu->get_logger()->error("Unimplemented VFPU6 op7 instruction {} ({:08X}) @ {:08X}", opcode, VT, instr, cpu->get_instr_addr());
                    exit(1);
            }
            break;
        default:
            cpu->get_logger()->error("Unimplemented VFPU6 instruction {} ({:08X}) @ {:08X}", opcode, instr, cpu->get_instr_addr());
            exit(1);
    }
}

static i64 i_vfpu7(Allegrex* cpu, const u32) {
    assert(cpu->get_cpu_id() == CpuId::CPU_ID_SC);

    if (!cpu->is_coprocessor_usable(2)) {
        return 1;
    }

    // This group is only populated with VFLUSH, VNOP and VSYNC, so we can
    // more or less safely ignore this
    return 1;
}

// Dummy instruction handler
static i64 i_undefined(Allegrex* cpu, const u32 instr) {
    cpu->get_logger()->error("Undefined primary instruction {:02X} ({:08X}) @ {:08X}", OPCODE, instr, cpu->get_instr_addr());
    cpu->dump_state();
    exit(1);
}

// Dummy instruction handler for SPECIAL class instructions
static i64 i_undefined_secondary(Allegrex* cpu, const u32 instr) {
    cpu->get_logger()->error("Undefined secondary instruction {:02X} ({:08X}) @ {:08X}", FUNCT, instr, cpu->get_instr_addr());
    cpu->dump_state();
    exit(1);
}

static i64 i_dummy(Allegrex*, const u32) {
    return 1;
}

void initialize() {
    primary_table.fill(i_undefined);
    special_table.fill(i_undefined_secondary);

    primary_table[Opcode::OPCODE_SPECIAL ] = i_special;
    primary_table[Opcode::OPCODE_REGIMM  ] = i_regimm;
    primary_table[Opcode::OPCODE_J       ] = i_j;
    primary_table[Opcode::OPCODE_JAL     ] = i_jal;
    primary_table[Opcode::OPCODE_BEQ     ] = i_beq;
    primary_table[Opcode::OPCODE_BNE     ] = i_bne;
    primary_table[Opcode::OPCODE_BLEZ    ] = i_blez;
    primary_table[Opcode::OPCODE_BGTZ    ] = i_bgtz;
    primary_table[Opcode::OPCODE_ADDI    ] = i_addiu;
    primary_table[Opcode::OPCODE_ADDIU   ] = i_addiu;
    primary_table[Opcode::OPCODE_SLTI    ] = i_slti;
    primary_table[Opcode::OPCODE_SLTIU   ] = i_sltiu;
    primary_table[Opcode::OPCODE_ANDI    ] = i_andi;
    primary_table[Opcode::OPCODE_ORI     ] = i_ori;
    primary_table[Opcode::OPCODE_XORI    ] = i_xori;
    primary_table[Opcode::OPCODE_LUI     ] = i_lui;
    primary_table[Opcode::OPCODE_COP0    ] = i_cop<0>;
    primary_table[Opcode::OPCODE_COP1    ] = i_cop<1>;
    primary_table[Opcode::OPCODE_COP2    ] = i_cop<2>;
    primary_table[Opcode::OPCODE_BEQL    ] = i_beql;
    primary_table[Opcode::OPCODE_BNEL    ] = i_bnel;
    primary_table[Opcode::OPCODE_BLEZL   ] = i_blezl;
    primary_table[Opcode::OPCODE_BGTZL   ] = i_bgtzl;
    primary_table[Opcode::OPCODE_VFPU0   ] = i_vfpu0;
    primary_table[Opcode::OPCODE_VFPU1   ] = i_vfpu1;
    primary_table[Opcode::OPCODE_MFVME   ] = i_mfvme;
    primary_table[Opcode::OPCODE_VFPU3   ] = i_vfpu3;
    primary_table[Opcode::OPCODE_SPECIAL2] = i_special2;
    primary_table[Opcode::OPCODE_SPECIAL3] = i_special3;
    primary_table[Opcode::OPCODE_LB      ] = i_lb;
    primary_table[Opcode::OPCODE_LH      ] = i_lh;
    primary_table[Opcode::OPCODE_LWL     ] = i_lwl;
    primary_table[Opcode::OPCODE_LW      ] = i_lw;
    primary_table[Opcode::OPCODE_LBU     ] = i_lbu;
    primary_table[Opcode::OPCODE_LHU     ] = i_lhu;
    primary_table[Opcode::OPCODE_LWR     ] = i_lwr;
    primary_table[Opcode::OPCODE_SB      ] = i_sb;
    primary_table[Opcode::OPCODE_SH      ] = i_sh;
    primary_table[Opcode::OPCODE_SWL     ] = i_swl;
    primary_table[Opcode::OPCODE_SW      ] = i_sw;
    primary_table[Opcode::OPCODE_MTVME   ] = i_mtvme;
    primary_table[Opcode::OPCODE_SWR     ] = i_swr;
    primary_table[Opcode::OPCODE_CACHE   ] = i_cache;
    primary_table[Opcode::OPCODE_LL      ] = i_ll;
    primary_table[Opcode::OPCODE_LWC1    ] = i_lwc1;
    primary_table[Opcode::OPCODE_LWC2    ] = i_lwc2;
    primary_table[Opcode::OPCODE_VFPU4   ] = i_vfpu4;
    primary_table[Opcode::OPCODE_LQC2    ] = i_lqc2;
    primary_table[Opcode::OPCODE_VFPU5   ] = i_vfpu5;
    primary_table[Opcode::OPCODE_SC      ] = i_sc;
    primary_table[Opcode::OPCODE_SWC1    ] = i_swc1;
    primary_table[Opcode::OPCODE_SWC2    ] = i_swc2;
    primary_table[Opcode::OPCODE_VFPU6   ] = i_vfpu6;
    primary_table[Opcode::OPCODE_SQC2    ] = i_sqc2;
    primary_table[Opcode::OPCODE_VFPU7   ] = i_vfpu7;

    special_table[SpecialOpcode::SPECIAL_OPCODE_SLL    ] = i_sll;
    special_table[SpecialOpcode::SPECIAL_OPCODE_SRL    ] = i_shift_right;
    special_table[SpecialOpcode::SPECIAL_OPCODE_SRA    ] = i_sra;
    special_table[SpecialOpcode::SPECIAL_OPCODE_SLLV   ] = i_sllv;
    special_table[SpecialOpcode::SPECIAL_OPCODE_SRLV   ] = i_shift_right_variable;
    special_table[SpecialOpcode::SPECIAL_OPCODE_SRAV   ] = i_srav;
    special_table[SpecialOpcode::SPECIAL_OPCODE_JR     ] = i_jr;
    special_table[SpecialOpcode::SPECIAL_OPCODE_JALR   ] = i_jalr;
    special_table[SpecialOpcode::SPECIAL_OPCODE_MOVZ   ] = i_movz;
    special_table[SpecialOpcode::SPECIAL_OPCODE_MOVN   ] = i_movn;
    special_table[SpecialOpcode::SPECIAL_OPCODE_SYSCALL] = i_syscall;
    special_table[SpecialOpcode::SPECIAL_OPCODE_BREAK  ] = i_break;
    special_table[SpecialOpcode::SPECIAL_OPCODE_SYNC   ] = i_sync;
    special_table[SpecialOpcode::SPECIAL_OPCODE_MFHI   ] = i_mfhi;
    special_table[SpecialOpcode::SPECIAL_OPCODE_MTHI   ] = i_mthi;
    special_table[SpecialOpcode::SPECIAL_OPCODE_MFLO   ] = i_mflo;
    special_table[SpecialOpcode::SPECIAL_OPCODE_MTLO   ] = i_mtlo;
    special_table[SpecialOpcode::SPECIAL_OPCODE_CLZ    ] = i_clz;
    special_table[SpecialOpcode::SPECIAL_OPCODE_MULT   ] = i_mult;
    special_table[SpecialOpcode::SPECIAL_OPCODE_MULTU  ] = i_multu;
    special_table[SpecialOpcode::SPECIAL_OPCODE_DIV    ] = i_div;
    special_table[SpecialOpcode::SPECIAL_OPCODE_DIVU   ] = i_divu;
    special_table[SpecialOpcode::SPECIAL_OPCODE_MADD   ] = i_madd;
    special_table[SpecialOpcode::SPECIAL_OPCODE_ADD    ] = i_addu;
    special_table[SpecialOpcode::SPECIAL_OPCODE_ADDU   ] = i_addu;
    special_table[SpecialOpcode::SPECIAL_OPCODE_SUB    ] = i_subu;
    special_table[SpecialOpcode::SPECIAL_OPCODE_SUBU   ] = i_subu;
    special_table[SpecialOpcode::SPECIAL_OPCODE_AND    ] = i_and;
    special_table[SpecialOpcode::SPECIAL_OPCODE_OR     ] = i_or;
    special_table[SpecialOpcode::SPECIAL_OPCODE_XOR    ] = i_xor;
    special_table[SpecialOpcode::SPECIAL_OPCODE_NOR    ] = i_nor;
    special_table[SpecialOpcode::SPECIAL_OPCODE_SLT    ] = i_slt;
    special_table[SpecialOpcode::SPECIAL_OPCODE_SLTU   ] = i_sltu;
    special_table[SpecialOpcode::SPECIAL_OPCODE_MAX    ] = i_max;
    special_table[SpecialOpcode::SPECIAL_OPCODE_MIN    ] = i_min;

    // What is this?
    special_table[0x2E] = i_dummy;
}

void soft_reset() {

}

void hard_reset() {

}

void shutdown() {

}

void run(Allegrex* cpu, const i64 target_timestamp) {
    if (!cpu->is_running()) {
        *cpu->get_cycles() = target_timestamp;
        return;
    }

    *cpu->get_target_timestamp() = target_timestamp;

    assert(*cpu->get_target_timestamp() > *cpu->get_cycles());

    while (*cpu->get_cycles() < *cpu->get_target_timestamp()) {
        const u32 instr = cpu->fetch_instr();

        *cpu->get_cycles() += primary_table[OPCODE](cpu, instr);
    }
};

}
