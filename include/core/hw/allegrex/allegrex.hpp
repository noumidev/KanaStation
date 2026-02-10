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

struct Allegrex {
private:
    std::shared_ptr<spdlog::logger> logger;

    CpuId cpu_id;

    RegisterFile regfile;

    common::u32 instr_addr;
    common::i64 cycles;

    template<typename T>
    T read(const common::u32 addr);

    template<typename T>
    void write(const common::u32 addr, const T data);

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

    bool in_delay_slot() const;

    void jump(const common::u32 target);
    void delayed_jump(const common::u32 target);

    // Read handlers need to be set up externally
    common::u8  (*read8 )(const common::u32 addr);
    common::u16 (*read16)(const common::u32 addr);
    common::u32 (*read32)(const common::u32 addr);

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
