/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/mg.cpp - MagicGate crypto engine */

#include <core/hw/mg.hpp>

#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <queue>
#include <vector>

#include <cryptopp/aes.h>
#include <cryptopp/des.h>
#include <cryptopp/cryptlib.h>
#include <cryptopp/modes.h>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <common/types.hpp>
#include <core/kanacore.hpp>
#include <core/scheduler.hpp>
#include <core/hw/bus.hpp>
#include <core/hw/intc.hpp>

namespace kanacore::hw::mg {

using namespace common;
using namespace CryptoPP;

constexpr u64 MG_ADDR = 0x1E100000;
constexpr u64 MG_SIZE = 0x1000;

constexpr int MG_INTERRUPT = 9;

constexpr u64 DES_KEY_SIZE = 8;
constexpr u64 AES_KEY_SIZE = 16;

static std::shared_ptr<spdlog::logger> logger;

enum IoAddress {
    IO_ADDRESS_KEY0    = MG_ADDR + 0x040,
    IO_ADDRESS_KEY1    = MG_ADDR + 0x044,
    IO_ADDRESS_IV0     = MG_ADDR + 0x060,
    IO_ADDRESS_IV1     = MG_ADDR + 0x064,
    IO_ADDRESS_CONTROL = MG_ADDR + 0x080,
    IO_ADDRESS_STATUS  = MG_ADDR + 0x084,
    IO_ADDRESS_MODE    = MG_ADDR + 0x088,
    IO_ADDRESS_SIZE    = MG_ADDR + 0x094,
    IO_ADDRESS_DATA    = MG_ADDR + 0x0A0,
};

#define HW_MG_KEY     ctx.key
#define HW_MG_IV      ctx.iv
#define HW_MG_CONTROL ctx.control
#define HW_MG_STATUS  ctx.status
#define HW_MG_MODE    ctx.mode
#define HW_MG_SIZE    ctx.size

enum EncdecMode {
    ENCDEC_MODE_DES_DECRYPT = 0x0020,
    ENCDEC_MODE_DES_ENCRYPT = 0x0021,
    ENCDEC_MODE_AES_DECRYPT = 0x1020,
    ENCDEC_MODE_AES_ENCRYPT = 0x1021,
};

static struct {
    // 8-byte DES key, 16-byte AES key
    u32 key[AES_KEY_SIZE / sizeof(u32)];
    u32 iv [AES_KEY_SIZE / sizeof(u32)];

    // Figure out what the bits mean...
    u32 control;

    union {
        u32 raw;

        struct {
            u32 busy        : 1;
            u32 command_end : 1;
            u32             : 30;
        };
    } status;

    u32 unk_010;
    u32 unk_090;

    u32 mode;
    u32 size;
} ctx;

static std::queue<u32> data_fifo;

static inline const char* get_mode_name(const u32 mode) {
    switch (mode) {
        case EncdecMode::ENCDEC_MODE_DES_DECRYPT:
            return "DES_DECRYPT";
        case EncdecMode::ENCDEC_MODE_DES_ENCRYPT:
            return "DES_ENCRYPT";
        case EncdecMode::ENCDEC_MODE_AES_DECRYPT:
            return "AES_DECRYPT";
        case EncdecMode::ENCDEC_MODE_AES_ENCRYPT:
            return "AES_ENCRYPT";
        default:
            return "N/A";
    }
}

static inline u32 get_fifo_size() {
    return sizeof(u32) * data_fifo.size();
}

static std::vector<u8> get_in_data() {
    std::vector<u8> in_data;

    while (!data_fifo.empty()) {
        const u32 data = data_fifo.front(); data_fifo.pop();

        in_data.push_back(data >>  0);
        in_data.push_back(data >>  8);
        in_data.push_back(data >> 16);
        in_data.push_back(data >> 24);
    }

    return in_data;
}

static void push_out_data(const std::vector<u8>& out_data) {
    assert((out_data.size() & 3) == 0);

    for (u64 i = 0; i < out_data.size(); i += 4) {
        data_fifo.push((out_data[i + 3] << 24) | (out_data[i + 2] << 16) | (out_data[i + 1] << 8) | out_data[i + 0]);
    }
}

static void end_command(const int) {
    intc::assert_sc_interrupt(MG_INTERRUPT);

    // I'm not sure if this is actually a "command end" bit, but
    // the MG driver will wait for it to go high after a command, so...
    HW_MG_STATUS.command_end = 1;

    HW_MG_STATUS.busy = 0;
}

// Encrypts data in-place using DES
static void des_encrypt(u8* data) {
    const u8* key = (u8*)HW_MG_KEY;
    const u8* iv  = (u8*)HW_MG_IV;

    // The uploaded key doesn't seem to pass this check... maybe this works a bit
    // different
    /* if (!DES::CheckKeyParityBits((u8*)HW_MG_KEY)) {
        logger->error("DES key parity check failed");
        exit(1);
    } */

    CBC_Mode<DES>::Encryption(key, DES_KEY_SIZE, iv).ProcessData(data, data, HW_MG_SIZE);
}

static void start_command() {
    std::vector<u8> in_data = get_in_data();

    // Unsure if this limitation actually exists, but it's better to check...
    assert((HW_MG_SIZE & 0xF) == 0);

    switch (HW_MG_MODE) {
        case EncdecMode::ENCDEC_MODE_DES_ENCRYPT:
            des_encrypt(in_data.data());
            break;
        default:
            logger->error("Unimplemented mode {}", get_mode_name(HW_MG_MODE));
            exit(1);
    }

    push_out_data(in_data);

    scheduler::schedule_event(
        scheduler::EventType::MG,
        end_command,
        0,
        scheduler::from_microseconds(20)
    );

    HW_MG_STATUS.command_end = 0;
    HW_MG_STATUS.busy = 1;
}

static u32 read(const u32 addr) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_STATUS:
            logger->info("STATUS read32");
            return HW_MG_STATUS.raw;
        case IoAddress::IO_ADDRESS_DATA: {
            logger->info("DATA read32");

            assert(!data_fifo.empty());

            const u32 data = data_fifo.front(); data_fifo.pop();

            return data;
        }
        case MG_ADDR + 0x010:
            logger->warn("Unmapped read32 @ {:08X}", addr);
            return ctx.unk_010;
        case MG_ADDR + 0x090:
            logger->warn("Unmapped read32 @ {:08X}", addr);
            return ctx.unk_090;
        case MG_ADDR + 0x018:
        case MG_ADDR + 0x070:
        case MG_ADDR + 0x074:
            logger->warn("Unmapped read32 @ {:08X}", addr);
            return 0;
        default:
            logger->error("Unmapped read32 @ {:08X}", addr);
            kanacore::get_sc_ptr()->dump_state();
            kanacore::get_sc_ptr()->get_logger()->info(" IA: {:08X}", kanacore::get_sc_ptr()->get_instr_addr());
            exit(1);
    }
}

static void push_in_fifo(const u32 data) {
    assert(!HW_MG_STATUS.busy);
    assert(get_fifo_size() < HW_MG_SIZE);

    data_fifo.push(data);

    if (get_fifo_size() >= HW_MG_SIZE) {
        start_command();
    }
}

static void write(const u32 addr, const u32 data) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_KEY0:
        case IoAddress::IO_ADDRESS_KEY1: {
            const int key_idx = (addr - IoAddress::IO_ADDRESS_KEY0) / sizeof(u32);

            logger->info("KEY{} write32 = {:08X}", key_idx, data);

            HW_MG_KEY[key_idx] = data;
            break;
        }
        case IoAddress::IO_ADDRESS_IV0:
        case IoAddress::IO_ADDRESS_IV1: {
            const int iv_idx = (addr - IoAddress::IO_ADDRESS_IV0) / sizeof(u32);

            logger->info("IV{} write32 = {:08X}", iv_idx, data);

            HW_MG_IV[iv_idx] = data;
            break;
        }
        case IoAddress::IO_ADDRESS_CONTROL:
            logger->info("CONTROL write32 = {:08X}", data);

            HW_MG_CONTROL = data;

            if ((data & 1) != 0) {
                intc::clear_sc_interrupt(MG_INTERRUPT);

                HW_MG_CONTROL &= ~1;
            }
            break;
        case IoAddress::IO_ADDRESS_MODE:
            logger->info("MODE write32 = {:08X}", data);

            HW_MG_MODE = data;
            break;
        case IoAddress::IO_ADDRESS_SIZE:
            logger->info("SIZE write32 = {:08X}", data);

            HW_MG_SIZE = data;
            break;
        case IoAddress::IO_ADDRESS_DATA:
            logger->info("DATA write32 = {:08X}", data);
            push_in_fifo(data);
            break;
        case MG_ADDR + 0x010:
            logger->warn("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            ctx.unk_010 = data;
            break;
        case MG_ADDR + 0x090:
            logger->warn("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            ctx.unk_090 = data;
            break;
        case MG_ADDR + 0x000:
        case MG_ADDR + 0x020:
        case MG_ADDR + 0x050:
        case MG_ADDR + 0x054:
            logger->warn("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            break;
        default:
            logger->error("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            kanacore::get_sc_ptr()->dump_state();
            kanacore::get_sc_ptr()->get_logger()->info(" IA: {:08X}", kanacore::get_sc_ptr()->get_instr_addr());
            exit(1);
    }
}

void initialize() {
    logger = spdlog::stdout_color_st("MG");

    std::memset(&ctx, 0, sizeof(ctx));
}

void soft_reset() {
    
}

void hard_reset() {
    const bus::PageDescriptor page_desc {
        .read32_func  = read,
        .write32_func = write,
    };

    kanacore::get_sc_bus_ptr()->map(MG_ADDR, MG_SIZE, page_desc);
}

void shutdown() {

}

};
