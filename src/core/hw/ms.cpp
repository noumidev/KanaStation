/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/ms.cpp - Memory Stick interface 0 */

#include <core/hw/ms.hpp>

#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <queue>
#include <vector>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <common/types.hpp>
#include <core/kanacore.hpp>
#include <core/scheduler.hpp>
#include <core/hw/bus.hpp>
#include <core/hw/sysctrl.hpp>

namespace kanacore::hw::ms {

using namespace common;

constexpr u64 MSIF0_ADDR = 0x1D200000;
constexpr u64 MSIF0_SIZE = 0x1000;

constexpr u64 SECTOR_SIZE = 512;

// https://dmitry.gr/?r=05.Projects&proj=31.%20Memory%20Stick

enum IoAddress {
    IO_ADDRESS_COMMAND = MSIF0_ADDR + 0x030,
    IO_ADDRESS_DATA    = MSIF0_ADDR + 0x034,
    IO_ADDRESS_STATUS  = MSIF0_ADDR + 0x038,
    IO_ADDRESS_CONTROL = MSIF0_ADDR + 0x03C,
};

#define HW_MSIF0_COMMAND ctx.command
#define HW_MSIF0_STATUS  ctx.status
#define HW_MSIF0_CONTROL ctx.control

enum Tpc {
    TPC_READ_LONG_DATA  = 0x2,
    TPC_READ_REG        = 0x4,
    TPC_GET_INT         = 0x7,
    TPC_SET_REGS_WINDOW = 0x8,
    TPC_SET_CMD_EX      = 0x9,
};

class MemoryStick {
private:
    FILE* file;

    u64 file_size;

public:
    bool mount(const char* path) {
        if (path == nullptr) {
            std::printf("NO PATH\n");
            return false;
        }

        file = std::fopen(path, "rb");

        if (file == nullptr) {
            std::printf("NO FILE\n");
            return false;
        }

        std::fseek(file, 0, SEEK_END);
        file_size = std::ftell(file);
        std::fseek(file, 0, SEEK_SET);

        return true;
    }

    void read_sectors(std::vector<u8>& bytes, const u32 sector, const u16 count) {
        assert(((sector + count - 1) * SECTOR_SIZE) < file_size);
    
        bytes.resize(count * SECTOR_SIZE);

        std::fseek(file, sector * SECTOR_SIZE, SEEK_SET);
        std::fread(bytes.data(), sizeof(u8), bytes.size(), file);
    }
};

static std::shared_ptr<spdlog::logger> logger;

static std::queue<u32> transmit_fifo;
static std::queue<u32> receive_fifo;

static MemoryStick ms;

static std::vector<u8> sector_bytes;

static struct {
    union {
        u32 raw;

        struct {
            u32 length : 10;
            u32        : 2;
            u32 tpc    : 4;
            u32        : 16;
        };
    } command;

    union {
        u32 raw;

        struct {
            u32            : 12;
            u32 ready      : 1;
            u32            : 1;
            u32 data_ready : 1;
            u32            : 17;
        };
    } status;

    union {
        u32 raw;

        struct {
            u32       : 15;
            u32 reset : 1;
            u32       : 16;
        };
    } control;

    u64 command_length;

    // Command 0x8 sets these
    u8 read_idx, read_length;
    u8 write_idx, write_length;
} ctx;

static inline u32 align_up(const u32 data) {
    constexpr u32 ALIGNMENT = 8;

    if ((data & (ALIGNMENT - 1)) != 0) {
        return (data | (ALIGNMENT - 1)) + 1;
    }

    return data;
}

// Move this to common...
template<typename T>
static inline T byteswap_from_buf(const u8* buf);

template<>
inline u8 byteswap_from_buf(const u8* buf) {
    return buf[0];
}

template<>
inline u16 byteswap_from_buf(const u8* buf) {
    return (buf[0] << 8) | buf[1];
}

template<>
inline u32 byteswap_from_buf(const u8* buf) {
    return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
}

static u32 read_transmit_fifo() {
    assert(!transmit_fifo.empty());

    const u32 data = transmit_fifo.front(); transmit_fifo.pop();

    return data;
}

static void write_receive_fifo(const u32 data) {
    receive_fifo.push(data);
}

static void read_register(const u8 idx, const u8 length) {
    switch (idx) {
        case 0x00:
            logger->warn("Unimplemented read @ {:02X}", ctx.read_idx);

            for (u32 i = 0; i < ctx.read_length; i += sizeof(u32)) {
                write_receive_fifo(0);
            }
            break;
        case 0x01:
            logger->debug("INT read");
            write_receive_fifo(0xA0); // Command ready, data available
            break;
        default:
            logger->error("Unimplemented read @ {:02X} (length: {})", idx, length);
            exit(1);
    }

    // Fill receive FIFO with dummy data if needed
    for (u32 i = receive_fifo.size(); i < ctx.command_length; i += sizeof(u32)) {
        write_receive_fifo(0);
    }
}

static void tpc_read_long_data() {
    logger->debug("READ_LONG_DATA");

    for (u64 i = 0; i < SECTOR_SIZE; i += sizeof(u32)) {
        write_receive_fifo(*(u32*)&sector_bytes[i]);
    }

    HW_MSIF0_STATUS.raw |= 1 << 13;
}

static void tpc_read_reg() {
    logger->debug("READ_REG (index: {:02X})", ctx.read_idx);
    read_register(ctx.read_idx, ctx.read_length);
}

static void tpc_get_int() {
    logger->debug("GET_INT");
    read_register(1, 1);
}

static void tpc_set_regs_window() {
    const u32 data = transmit_fifo.front(); transmit_fifo.pop();

    ctx.read_idx     = (u8)data;
    ctx.read_length  = (u8)(data >> 8);
    ctx.write_idx    = (u8)(data >> 16);
    ctx.write_length = (u8)(data >> 24);

    logger->debug(
        "SET_REGS_WINDOW (read index: {:02X}, read length: {}, write index: {:02X}, write length: {}",
        ctx.read_idx,
        ctx.read_length,
        ctx.write_idx,
        ctx.write_length
    );
}

static void tpc_set_cmd_ex() {
    const u32 data[2] = {
        read_transmit_fifo(), read_transmit_fifo(),
    };

    const u8 command = byteswap_from_buf<u8>((u8*)data);
    const u16 count  = byteswap_from_buf<u16>((u8*)data + 1);
    const u32 sector = byteswap_from_buf<u32>((u8*)data + 3);

    logger->debug("SET_CMD_EX (command: {:02X}, sector: {}, count: {})", command, sector, count);

    assert(command == 0x20);
    assert(count == 1);

    ms.read_sectors(sector_bytes, sector, count);

    HW_MSIF0_STATUS.raw |= 1 << 13;
}

static void end_command(const int data_ready) {
    HW_MSIF0_STATUS.ready = 1;
    HW_MSIF0_STATUS.data_ready = data_ready;

    while (!transmit_fifo.empty()) {
        transmit_fifo.pop();
    }
}

static void start_command() {
    const u32 tpc = HW_MSIF0_COMMAND.tpc;
    const u32 length = HW_MSIF0_COMMAND.length;

    // Clear status bits here (some commands will set DATA_READY)
    HW_MSIF0_STATUS.ready = 0;
    HW_MSIF0_STATUS.raw &= ~(1 << 13);
    HW_MSIF0_STATUS.data_ready = 0;

    bool data_ready = false;

    switch (tpc) {
        case Tpc::TPC_READ_LONG_DATA:
            tpc_read_long_data();

            data_ready = true;
            break;
        case Tpc::TPC_READ_REG:
            tpc_read_reg();

            data_ready = true;
            break;
        case Tpc::TPC_GET_INT:
            tpc_get_int();

            data_ready = true;
            break;
        case Tpc::TPC_SET_REGS_WINDOW:
            tpc_set_regs_window();
            break;
        case Tpc::TPC_SET_CMD_EX:
            tpc_set_cmd_ex();

            data_ready = true;
            break;
        default:
            logger->error("Unimplemented command {:X} (length: {})", tpc, length);
            exit(1);
    }

    scheduler::schedule_event(scheduler::EventType::MEMORY_STICK, end_command, data_ready, scheduler::from_microseconds(5));
}

static void prepare_command() {
    const u32 tpc = HW_MSIF0_COMMAND.tpc;
    const u32 length = HW_MSIF0_COMMAND.length;

    // Commands appear to always require one additional word?
    ctx.command_length = align_up(HW_MSIF0_COMMAND.length) / sizeof(u32);

    switch (tpc) {
        case Tpc::TPC_READ_LONG_DATA:
        case Tpc::TPC_READ_REG:
        case Tpc::TPC_GET_INT:
            // These commands don't receive any data and need to trigger here
            start_command();
            break;
        case Tpc::TPC_SET_REGS_WINDOW:
        case Tpc::TPC_SET_CMD_EX:
            break;
        default:
            logger->error("Unimplemented command {:X} (length: {})", tpc, length);
            exit(1);
    }
}

static void set_control(const u32 data) {
    HW_MSIF0_CONTROL.raw = data;

    if (HW_MSIF0_CONTROL.reset) {
        HW_MSIF0_CONTROL.reset = 0;
    }
}

static u32 read_receive_fifo() {
    assert(!receive_fifo.empty());

    const u32 data = receive_fifo.front(); receive_fifo.pop();

    return data;
}

static u32 read32(const u32 addr) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_DATA:
            // logger->debug("DATA read32");
            return read_receive_fifo();
        case IoAddress::IO_ADDRESS_STATUS:
            // logger->debug("STATUS read32");
            return HW_MSIF0_STATUS.raw;
        case IoAddress::IO_ADDRESS_CONTROL:
            logger->debug("CONTROL read32");
            return HW_MSIF0_CONTROL.raw;
        default:
            logger->error("Unmapped read32 @ {:08X}", addr);
            exit(1);
    }
}

static void write_transmit_fifo(const u32 data) {
    assert(transmit_fifo.size() < ctx.command_length);

    transmit_fifo.push(data);

    if (transmit_fifo.size() == ctx.command_length) {
        // Start command when all command words have been sent to the FIFO
        start_command();
    }
}

static void write32(const u32 addr, const u32 data) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_COMMAND:
            logger->debug("COMMAND write32 = {:08X}", data);

            HW_MSIF0_COMMAND.raw = data;

            prepare_command();
            break;
        case IoAddress::IO_ADDRESS_DATA:
            logger->debug("DATA write32 = {:08X}", data);

            write_transmit_fifo(data);
            break;
        case IoAddress::IO_ADDRESS_CONTROL:
            logger->debug("CONTROL write32 = {:08X}", data);
            set_control(data);
            break;
        default:
            logger->error("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            exit(1);
    }
}

void initialize(const char* ms_path) {
    logger = spdlog::stdout_color_st("MS");

    std::memset(&ctx, 0, sizeof(ctx));

    if (ms_path == nullptr) {
        sysctrl::clear_ms0_connected();
    } else if (ms.mount(ms_path)) {
        sysctrl::set_ms0_connected();

        logger->debug("MS connected");
    }
}

void soft_reset() {

}

void hard_reset() {
    const bus::PageDescriptor page_desc {
        .read32_func  = read32,
        .write32_func = write32,
    };

    kanacore::get_sc_bus_ptr()->map(MSIF0_ADDR, MSIF0_SIZE, page_desc);
}

void shutdown() {

}

};
