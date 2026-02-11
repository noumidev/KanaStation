/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/allegrex/interpreter.cpp - ALLEGREX interpreter */

#include "core/hw/allegrex/allegrex.hpp"
#include <core/hw/allegrex/interpreter.hpp>

#include <array>
#include <cstdlib>

#include <common/types.hpp>

#define OPCODE ((instr >> 26) & 0x3F)
#define FUNCT  ((instr >>  0) & 0x7F) // Intentionally 7-bit to help with decoding
#define RS     ((instr >> 21) & 0x1F)
#define RT     ((instr >> 16) & 0x1F)
#define RD     ((instr >> 11) & 0x1F)
#define SIZE   ((instr >> 11) & 0x1F)
#define SA     ((instr >>  6) & 0x1F)
#define POS    ((instr >>  6) & 0x1F)
#define UIMM   ((instr >>  0) & 0xFFFF)
#define TARGET ((instr >>  0) & 0x3FFFFFF)

namespace kanacore::hw::allegrex::interpreter {

using namespace common;

using Instruction = i64 (*)(Allegrex*, const u32);

constexpr u64 PRIMARY_TABLE_SIZE = 0x40;
constexpr u64 SPECIAL_TABLE_SIZE = 0x80;

enum Opcode {
    OPCODE_SPECIAL  = 0x00,
    OPCODE_JAL      = 0x03,
    OPCODE_BNE      = 0x05,
    OPCODE_BGTZ     = 0x07,
    OPCODE_ADDIU    = 0x09,
    OPCODE_ORI      = 0x0D,
    OPCODE_LUI      = 0x0F,
    OPCODE_COP0     = 0x10,
    OPCODE_SPECIAL3 = 0x1F,
    OPCODE_LW       = 0x23,
    OPCODE_SW       = 0x2B,
    OPCODE_CACHE    = 0x2F,
};

enum SpecialOpcode {
    SPECIAL_OPCODE_SLL  = 0x00,
    SPECIAL_OPCODE_SLLV = 0x04,
    SPECIAL_OPCODE_JR   = 0x08,
    SPECIAL_OPCODE_SYNC = 0x0F,
    SPECIAL_OPCODE_ADDU = 0x21,
};

enum Special3Opcode {
    SPECIAL3_OPCODE_EXT = 0x00,
};

enum CopOpcode {
    COP_OPCODE_MFC = 0x00,
    COP_OPCODE_MTC = 0x04,
    COP_OPCODE_CTC = 0x06,
};

enum CopNum {
    COP_NUM_CP0,
};

static std::array<Instruction, PRIMARY_TABLE_SIZE> primary_table;
static std::array<Instruction, SPECIAL_TABLE_SIZE> special_table;

static i64 i_addiu(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RT, cpu->get_reg(RS) + (i32)(i16)UIMM);
    return 1;
}

static i64 i_addu(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RD, cpu->get_reg(RS) + cpu->get_reg(RT));
    return 1;
}

static i64 i_bgtz(Allegrex* cpu, const u32 instr) {
    cpu->branch<false>(cpu->get_pc() + ((i32)(i16)UIMM << 2), (i32)cpu->get_reg(RS) > 0, 0);
    return 1;
}

static i64 i_bne(Allegrex* cpu, const u32 instr) {
    cpu->branch<false>(cpu->get_pc() + ((i32)(i16)UIMM << 2), cpu->get_reg(RS) != cpu->get_reg(RT), 0);
    return 1;
}

template<int cop_num>
static i64 i_ctc(Allegrex* cpu, const u32 instr) {
    switch (cop_num) {
        case CopNum::COP_NUM_CP0:
            cpu->set_control_reg(RD, cpu->get_reg(RT));
            break;
        default:
            cpu->get_logger()->error("Unimplemented CP{} for CTC", cop_num);
            exit(1);
    }

    return 1;
}

static i64 i_ext(Allegrex* cpu, const u32 instr) {
    // What happens when the CPU encounters this?
    assert((POS + SIZE + 1) <= 32);

    cpu->set_reg(RT, (cpu->get_reg(RS) >> POS) & (0xFFFFFFFFU >> (31 - SIZE)));
    return 1;
}

static i64 i_jal(Allegrex* cpu, const u32 instr) {
    cpu->branch<false>((cpu->get_pc() & 0xF0000000) + (TARGET << 2), true, 31);
    return 1;
}

static i64 i_jr(Allegrex* cpu, const u32 instr) {
    cpu->branch<false>(cpu->get_reg(RS), true, 0);
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

template<int cop_num>
static i64 i_mfc(Allegrex* cpu, const u32 instr) {
    switch (cop_num) {
        case CopNum::COP_NUM_CP0:
            cpu->set_reg(RT, cpu->get_status_reg(RD));
            break;
        default:
            cpu->get_logger()->error("Unimplemented CP{} for MFC", cop_num);
            exit(1);
    }

    return 1;
}

template<int cop_num>
static i64 i_mtc(Allegrex* cpu, const u32 instr) {
    switch (cop_num) {
        case CopNum::COP_NUM_CP0:
            cpu->set_status_reg(RD, cpu->get_reg(RT));
            break;
        default:
            cpu->get_logger()->error("Unimplemented CP{} for MTC", cop_num);
            exit(1);
    }

    return 1;
}

static i64 i_ori(Allegrex* cpu, const u32 instr) {
    cpu->set_reg(RT, cpu->get_reg(RS) | UIMM);
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

static i64 i_sw(Allegrex* cpu, const u32 instr) {
    cpu->write<u32>(cpu->get_reg(RS) + (i32)(i16)UIMM, cpu->get_reg(RT));
    return 1;
}

static i64 i_cache(Allegrex* cpu, const u32 instr) {
    const u32 addr = cpu->get_reg(RS) + (i32)(i16)UIMM;

    switch (RT) {
        default:
            cpu->get_logger()->warn("Unimplemented CACHE instruction {:02X} ({:08X}) for address {:08X}", RT, instr, addr);
            break;
    }

    return 1;
}

template<int cop_num>
static i64 i_cop(Allegrex* cpu, const u32 instr) {
    switch (RS) {
        case CopOpcode::COP_OPCODE_MFC:
            return i_mfc<cop_num>(cpu, instr);
        case CopOpcode::COP_OPCODE_MTC:
            return i_mtc<cop_num>(cpu, instr);
        case CopOpcode::COP_OPCODE_CTC:
            return i_ctc<cop_num>(cpu, instr);
        default:
            cpu->get_logger()->error("Undefined COP{} instruction {:02X} ({:08X}) @ {:08X}", cop_num, RS, instr, cpu->get_instr_addr());
            cpu->dump_state();
            exit(1);
    }
}

static i64 i_special(Allegrex* cpu, const u32 instr) {
    return special_table[FUNCT](cpu, instr);
}

static i64 i_special3(Allegrex* cpu, const u32 instr) {
    // SPECIAL3 uses the regular 6-bit FUNCT field
    const u32 funct = FUNCT & ~0x40;

    switch (funct) {
        case Special3Opcode::SPECIAL3_OPCODE_EXT:
            return i_ext(cpu, instr);
        default:
            cpu->get_logger()->error("Undefined SPECIAL3 instruction {:02X} ({:08X}) @ {:08X}", funct, instr, cpu->get_instr_addr());
            cpu->dump_state();
            exit(1);
    }
}

static i64 i_sync(Allegrex*, const u32) {
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

void initialize() {
    primary_table.fill(i_undefined);
    special_table.fill(i_undefined_secondary);

    primary_table[Opcode::OPCODE_SPECIAL ] = i_special;
    primary_table[Opcode::OPCODE_JAL     ] = i_jal;
    primary_table[Opcode::OPCODE_BNE     ] = i_bne;
    primary_table[Opcode::OPCODE_BGTZ    ] = i_bgtz;
    primary_table[Opcode::OPCODE_ADDIU   ] = i_addiu;
    primary_table[Opcode::OPCODE_ORI     ] = i_ori;
    primary_table[Opcode::OPCODE_LUI     ] = i_lui;
    primary_table[Opcode::OPCODE_COP0    ] = i_cop<0>;
    primary_table[Opcode::OPCODE_SPECIAL3] = i_special3;
    primary_table[Opcode::OPCODE_LW      ] = i_lw;
    primary_table[Opcode::OPCODE_SW      ] = i_sw;
    primary_table[Opcode::OPCODE_CACHE   ] = i_cache;

    special_table[SpecialOpcode::SPECIAL_OPCODE_SLL ] = i_sll;
    special_table[SpecialOpcode::SPECIAL_OPCODE_SLLV] = i_sllv;
    special_table[SpecialOpcode::SPECIAL_OPCODE_JR  ] = i_jr;
    special_table[SpecialOpcode::SPECIAL_OPCODE_SYNC] = i_sync;
    special_table[SpecialOpcode::SPECIAL_OPCODE_ADDU] = i_addu;
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

        *cpu->get_cycles() -= primary_table[OPCODE](cpu, instr);
    }
};

}
