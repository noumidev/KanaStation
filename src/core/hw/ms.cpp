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
#include <core/hw/intc.hpp>
#include <core/hw/sysctrl.hpp>

namespace kanacore::hw::ms {

using namespace common;

constexpr u64 MSIF0_ADDR = 0x1D200000;
constexpr u64 MSIF0_SIZE = 0x1000;

constexpr int MS_INTERRUPT = 7;

constexpr u64 SECTOR_SIZE = 512;

// https://dmitry.gr/?r=05.Projects&proj=31.%20Memory%20Stick

enum IoAddress {
    IO_ADDRESS_INTRSTAT = MSIF0_ADDR + 0x000,
    IO_ADDRESS_CMDSTATE = MSIF0_ADDR + 0x004,
    IO_ADDRESS_COMMAND  = MSIF0_ADDR + 0x030,
    IO_ADDRESS_DATA     = MSIF0_ADDR + 0x034,
    IO_ADDRESS_STATUS   = MSIF0_ADDR + 0x038,
    IO_ADDRESS_CONTROL  = MSIF0_ADDR + 0x03C,
};

#define HW_MSIF0_CMDSTATE ctx.command_state
#define HW_MSIF0_COMMAND  ctx.command
#define HW_MSIF0_STATUS   ctx.status
#define HW_MSIF0_CONTROL  ctx.control

enum Tpc {
    TPC_READ_LONG_DATA  = 0x2,
    TPC_READ_REG        = 0x4,
    TPC_GET_INT         = 0x7,
    TPC_SET_REGS_WINDOW = 0x8,
    TPC_SET_CMD_EX      = 0x9,
    TPC_WRITE_REG       = 0xB,
    TPC_SET_CMD         = 0xE,
};

enum ClassicCommand {
    CLASSIC_COMMAND_READ = 0xAA,
};

enum ProCommand {
    PRO_COMMAND_READ = 0x20,
};

enum ProRegister {
    PRO_REGISTER_INT       = 0x01,
    PRO_REGISTER_STA0      = 0x02,
    PRO_REGISTER_TYPE      = 0x04,
    PRO_REGISTER_NUMSEC_HI = 0x11,
    PRO_REGISTER_NUMSEC_LO = 0x12,
};

class MemoryStick {
private:
    FILE* file;

    u64 file_size;

public:
    bool is_mounted() const {
        return file != nullptr;
    }

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

static std::queue<u32> data_fifo;

static MemoryStick memory_stick;

static std::vector<u8> sector_bytes;

static struct {
    u16 command_state;

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
            u32 bad_command : 1;
            u32 buf_request : 1;
            u32 error       : 1;
            u32 command_end : 1;
            u32 fifo_full   : 1;
            u32 fifo_empty  : 1;
            u32             : 2;
            u32 timeout     : 1;
            u32 crc_error   : 1;
            u32             : 2;
            u32 ready       : 1;
            u32 interrupt   : 1;
            u32 dma_request : 1;
            u32             : 17;
        };
    } status;

    union {
        u32 raw;

        struct {
            u32             : 8;
            u32 fifo_write  : 1;
            u32 fifo_clear  : 1;
            u32             : 1;
            u32 intr_clear  : 1;
            u32 no_crc      : 1;
            u32 intr_enable : 1;
            u32             : 1;
            u32 reset       : 1;
            u32 dma_enable  : 1;
            u32             : 15;
        };
    } control;

    // For commands that needs additional params
    bool needs_data;
    u32 data_length;

    u16 sector_count;
    u32 sector_num;

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

static void assert_interrupt() {
    HW_MSIF0_STATUS.interrupt = 1;

    if (HW_MSIF0_CONTROL.intr_enable) {
        intc::assert_sc_interrupt(MS_INTERRUPT);
    }
}

static void clear_interrupt() {
    HW_MSIF0_STATUS.interrupt = 0;

    intc::clear_sc_interrupt(MS_INTERRUPT);
}

static void update_fifo_status() {
    HW_MSIF0_STATUS.fifo_empty = data_fifo.empty();
    HW_MSIF0_STATUS.fifo_full  = 0; // How deep is the FIFO?
}

static void clear_data_fifo() {
    while (!data_fifo.empty()) {
        data_fifo.pop();
    }

    update_fifo_status();
}

// This is to get command params
static u32 pop_data_fifo() {
    assert(!data_fifo.empty());

    const u32 data = data_fifo.front(); data_fifo.pop();

    update_fifo_status();

    return data;
}

// This is exposed to the CPU
static u32 read_data_fifo() {
    if (HW_MSIF0_CONTROL.fifo_write) {
        logger->warn("Reading data FIFO in write mode");
    }

    return pop_data_fifo();
}

static void push_data_fifo(const u32 data) {
    data_fifo.push(data);

    update_fifo_status();
}

static void write_data_fifo(const u32 data) {
    if (!HW_MSIF0_CONTROL.fifo_write) {
        logger->warn("Writing data FIFO in read mode");
    }

    push_data_fifo(data);
}

static void classic_command_read() {
    logger->debug("CLASSIC_READ (sector: {}, num sectors: {})", ctx.sector_num, ctx.sector_count);

    // In infinite mode (count == 0), this needs to return more sectors...
    memory_stick.read_sectors(sector_bytes, ctx.sector_num++, (ctx.sector_count == 0) ? 1 : ctx.sector_num);

    HW_MSIF0_STATUS.dma_request = 1;
}

static void start_classic_command(const u8 command) {
    // Despite reporting MS as MS Pro, it still sends Classic commands?
    switch (command) {
        case ClassicCommand::CLASSIC_COMMAND_READ:
            classic_command_read();
            break;
        default:
            logger->error("Unimplemented Classic command {:02X}", command);
            exit(1);
    }
}

static void read_register(const u8 idx, const u8 length) {
    const u8 MS_TYPE_PRO = 0x01;

    switch (idx) {
        case ProRegister::PRO_REGISTER_INT:
            logger->debug("INT read");
            push_data_fifo(((HW_MSIF0_STATUS.raw & 0xF) | 2) << 4);
            break;
        case ProRegister::PRO_REGISTER_STA0:
            logger->debug("STA0 read");
            push_data_fifo(0);
            break;
        case ProRegister::PRO_REGISTER_TYPE:
            logger->debug("TYPE read");
            push_data_fifo(MS_TYPE_PRO); // MS Pro
            break;
        case 0x00:
            // What is this?
            logger->warn("Unimplemented read @ {:02X}", ctx.read_idx);
            push_data_fifo(0);
            break;
        default:
            logger->error("Unimplemented read @ {:02X} (length: {})", idx, length);
            exit(1);
    }

    // Fill receive FIFO with dummy data if needed
    for (u32 i = data_fifo.size(); i < ctx.data_length; i++) {
        push_data_fifo(0);
    }
}

static void write_register(const u8 idx, const u8 length) {
    const u32 data = pop_data_fifo();

    switch (idx) {
        case ProRegister::PRO_REGISTER_NUMSEC_HI: {
            const u8 sector_hi = data;

            logger->debug("NUMSEC_HI write = {:02X}", sector_hi);

            ctx.sector_count &= ~0xFF;
            ctx.sector_count |= sector_hi << 8;
            break;
        }
        default:
            logger->error("Unimplemented write @ {:02X} (length: {})", idx, length);
            exit(1);
    }
}

static void tpc_read_long_data() {
    logger->debug("READ_LONG_DATA");

    for (u64 i = 0; i < SECTOR_SIZE; i += sizeof(u32)) {
        push_data_fifo(*(u32*)&sector_bytes[i]);
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
    const u32 data = pop_data_fifo();

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
        pop_data_fifo(), pop_data_fifo(),
    };

    const u8 command = byteswap_from_buf<u8>((u8*)data);
    const u16 count  = byteswap_from_buf<u16>((u8*)data + 1);
    u32 sector = byteswap_from_buf<u32>((u8*)data + 3);

    logger->debug("SET_CMD_EX (command: {:02X}, sector: {}, count: {})", command, sector, count);

    assert(command == ProCommand::PRO_COMMAND_READ);
    assert(count == 1);

    memory_stick.read_sectors(sector_bytes, sector, count);
}

static void tpc_write_reg() {
    logger->debug("WRITE_REG (index: {:02X})", ctx.write_idx);
    write_register(ctx.write_idx, ctx.write_length);
}

static void tpc_set_cmd() {
    const u8 command = pop_data_fifo();

    logger->debug("SET_CMD (command: {:02X})", command);
    start_classic_command(command);
}

static void end_command(const int has_data) {
    HW_MSIF0_STATUS.command_end = 1;
    HW_MSIF0_STATUS.ready = 1;
    HW_MSIF0_STATUS.dma_request = has_data;

    // Do all of them assert interrupts?
    assert_interrupt();
}

static void start_command() {
    const u32 tpc = HW_MSIF0_COMMAND.tpc;
    const u32 length = HW_MSIF0_COMMAND.length;

    if (!memory_stick.is_mounted()) {
        HW_MSIF0_STATUS.ready   = 1;
        HW_MSIF0_STATUS.timeout = 1;
        return;
    }

    bool has_data = false;

    switch (tpc) {
        case Tpc::TPC_READ_LONG_DATA:
            tpc_read_long_data();

            has_data = true;
            break;
        case Tpc::TPC_READ_REG:
            tpc_read_reg();

            has_data = true;
            break;
        case Tpc::TPC_GET_INT:
            tpc_get_int();

            has_data = true;
            break;
        case Tpc::TPC_SET_REGS_WINDOW:
            tpc_set_regs_window();
            break;
        case Tpc::TPC_SET_CMD_EX:
            tpc_set_cmd_ex();

            has_data = true;
            break;
        case Tpc::TPC_WRITE_REG:
            tpc_write_reg();
            break;
        case Tpc::TPC_SET_CMD:
            tpc_set_cmd();
            break;
        default:
            logger->error("Unimplemented TPC {:X} (length: {})", tpc, length);
            exit(1);
    }

    scheduler::schedule_event(scheduler::EventType::MEMORY_STICK, end_command, has_data, scheduler::from_microseconds(25));

    HW_MSIF0_STATUS.command_end = 0;
    HW_MSIF0_STATUS.ready = 0;
}

static void prepare_command(const u16 command) {
    HW_MSIF0_COMMAND.raw = command;

    const u32 tpc = HW_MSIF0_COMMAND.tpc;
    const u32 length = HW_MSIF0_COMMAND.length;

    // Commands appear to always require one additional word?
    ctx.data_length = align_up(HW_MSIF0_COMMAND.length) / sizeof(u32);
    ctx.needs_data  = false;

    switch (tpc) {
        case Tpc::TPC_READ_LONG_DATA:
        case Tpc::TPC_READ_REG:
        case Tpc::TPC_GET_INT:
            // These commands don't receive any data and need to trigger here
            start_command();
            return;
        case Tpc::TPC_SET_REGS_WINDOW:
        case Tpc::TPC_SET_CMD_EX:
        case Tpc::TPC_WRITE_REG:
        case Tpc::TPC_SET_CMD:
            break;
        default:
            logger->error("Unimplemented command {:X} (length: {})", tpc, length);
            exit(1);
    }

    ctx.needs_data = true;

    HW_MSIF0_STATUS.dma_request = 1;
}

static void set_control(const u32 data) {
    HW_MSIF0_CONTROL.raw = data;

    if (HW_MSIF0_CONTROL.reset) {
        HW_MSIF0_CONTROL.reset = 0;

        soft_reset();
    } else {
        if (HW_MSIF0_CONTROL.intr_clear) {
            HW_MSIF0_CONTROL.intr_clear = 0;

            clear_interrupt();
        }

        if (HW_MSIF0_CONTROL.fifo_clear) {
            HW_MSIF0_CONTROL.fifo_clear = 0;

            clear_data_fifo();
        }
    }
}

static u8 read8(const u32 addr) {
    switch (addr) {
        default:
            logger->error("Unmapped read8 @ {:08X}", addr);
            exit(1);
    }
}

static u16 read16(const u32 addr) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_INTRSTAT:
            logger->debug("INTRSTAT read16");
            return (HW_MSIF0_STATUS.raw & 0xF) << 4;
        case IoAddress::IO_ADDRESS_CMDSTATE:
            logger->debug("CMDSTATE read16");
            return HW_MSIF0_CMDSTATE;
        case IoAddress::IO_ADDRESS_STATUS:
            // logger->debug("STATUS read16");
            return HW_MSIF0_STATUS.raw;
        case IoAddress::IO_ADDRESS_CONTROL:
            logger->debug("CONTROL read16");
            return HW_MSIF0_CONTROL.raw;
        case MSIF0_ADDR + 0x08:
            logger->warn("Unmapped read16 @ {:08X}", addr);
            return 0;
        default:
            logger->error("Unmapped read16 @ {:08X}", addr);
            exit(1);
    }
}

static u32 read32(const u32 addr) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_DATA: {
            logger->debug("DATA read32");

            assert(HW_MSIF0_STATUS.dma_request);

            const u32 data = read_data_fifo();

            if (data_fifo.empty()) {
                HW_MSIF0_STATUS.dma_request = 0;
            }

            return data;
        }
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

static void write8(const u32 addr, const u8 data) {
    switch (addr) {
        default:
            logger->error("Unmapped write8 @ {:08X} = {:02X}", addr, data);
            exit(1);
    }
}

static void write16(const u32 addr, const u16 data) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_COMMAND:
            logger->debug("COMMAND write16 = {:04X}", data);
            prepare_command(data);
            break;
        case IoAddress::IO_ADDRESS_CONTROL:
            logger->debug("CONTROL write16 = {:04X}", data);
            set_control(data);
            break;
        case IoAddress::IO_ADDRESS_CMDSTATE:
            logger->debug("CMDSTATE write16 = {:04X}", data);
            
            HW_MSIF0_CMDSTATE = data & ~1;
            break;
        default:
            logger->error("Unmapped write16 @ {:08X} = {:04X}", addr, data);
            exit(1);
    }
}

static void write32(const u32 addr, const u32 data) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_COMMAND:
            logger->debug("COMMAND write32 = {:08X}", data);
            prepare_command(data);
            break;
        case IoAddress::IO_ADDRESS_DATA:
            logger->debug("DATA write32 = {:08X}", data);
            
            assert(HW_MSIF0_STATUS.dma_request);
            assert(ctx.needs_data);
            assert(data_fifo.size() < ctx.data_length);

            write_data_fifo(data);

            if (data_fifo.size() == ctx.data_length) {
                HW_MSIF0_STATUS.dma_request = 0;

                // NOW we can execute commands that need params
                start_command();
            }
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

    if ((ms_path == nullptr) || (!memory_stick.mount(ms_path))) {
        sysctrl::clear_ms0_connected();
    
        kanacore::release_button(kanacore::Button::BUTTON_MS);
    } else {
        logger->debug("Memory Stick connected");

        sysctrl::set_ms0_connected();

        kanacore::press_button(kanacore::Button::BUTTON_MS);
    }
}

void soft_reset() {
    HW_MSIF0_STATUS.dma_request = 0;

    clear_data_fifo();
    clear_interrupt();

    HW_MSIF0_STATUS.ready = 1;
}

void hard_reset() {
    const bus::PageDescriptor page_desc {
        .read8_func   = read8,
        .read16_func  = read16,
        .read32_func  = read32,
        .write8_func  = write8,
        .write16_func = write16,
        .write32_func = write32,
    };

    kanacore::get_sc_bus_ptr()->map(MSIF0_ADDR, MSIF0_SIZE, page_desc);

    soft_reset();
}

void shutdown() {

}

};
