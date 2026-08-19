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
#include <core/hw/dmac.hpp>
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
    IO_ADDRESS_NUMSEC   = MSIF0_ADDR + 0x010,
    IO_ADDRESS_CMDDATA  = MSIF0_ADDR + 0x024,
    IO_ADDRESS_PAGEDATA = MSIF0_ADDR + 0x028,
    IO_ADDRESS_COMMAND  = MSIF0_ADDR + 0x030,
    IO_ADDRESS_DATA     = MSIF0_ADDR + 0x034,
    IO_ADDRESS_STATUS   = MSIF0_ADDR + 0x038,
    IO_ADDRESS_CONTROL  = MSIF0_ADDR + 0x03C,
};

#define HW_MSIF0_INTRSTAT ctx.interrupt_status
#define HW_MSIF0_CMDSTATE ctx.command_state
#define HW_MSIF0_NUMSEC   ctx.num_sectors
#define HW_MSIF0_COMMAND  ctx.command
#define HW_MSIF0_STATUS   ctx.status
#define HW_MSIF0_CONTROL  ctx.control

enum Tpc {
    TPC_READ_LONG_DATA  = 0x2,
    TPC_READ_SHORT_DATA = 0x3,
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
    PRO_COMMAND_SLEEP = 0x11,
    PRO_COMMAND_READ  = 0x20,
    PRO_COMMAND_WRITE = 0x21,
    PRO_COMMAND_ATTR  = 0x24,
    PRO_COMMAND_STOP  = 0x25,
};

enum ProRegister {
    PRO_REGISTER_INT        = 0x01,
    PRO_REGISTER_STA0       = 0x02,
    PRO_REGISTER_TYPE       = 0x04,
    PRO_REGISTER_CATEGORY   = 0x06,
    PRO_REGISTER_CLASS      = 0x07,
    PRO_REGISTER_CFG        = 0x10,
    PRO_REGISTER_NUMSEC_HI  = 0x11,
    PRO_REGISTER_NUMSEC_LO  = 0x12,
    PRO_REGISTER_ADDR_HI    = 0x14,
    PRO_REGISTER_ADDR_MIDHI = 0x15,
    PRO_REGISTER_ADDR_MIDLO = 0x16,
    PRO_REGISTER_ADDR_LO    = 0x17,
};

class MemoryStick {
private:
    FILE* file;

    u64 file_size;

public:
    bool is_mounted() const {
        return file != nullptr;
    }

    u64 get_file_size() const {
        return file_size;
    }

    bool mount(const char* path) {
        if (path == nullptr) {
            return false;
        }

        file = std::fopen(path, "rb");

        if (file == nullptr) {
            return false;
        }

        std::fseek(file, 0, SEEK_END);
        file_size = std::ftell(file);
        std::fseek(file, 0, SEEK_SET);

        if ((file_size & (SECTOR_SIZE - 1)) != 0) {
            std::fclose(file);
            return false;
        }

        return true;
    }

    void read_sector(std::array<u8, SECTOR_SIZE>& bytes, const u32 sector) {
        assert((sector * SECTOR_SIZE) < file_size);

        std::fseek(file, sector * SECTOR_SIZE, SEEK_SET);
        std::fread(bytes.data(), sizeof(u8), bytes.size(), file);
    }

    void write_sector(std::array<u8, SECTOR_SIZE>& bytes, const u32 sector) {
        assert((sector * SECTOR_SIZE) < file_size);

        std::fseek(file, sector * SECTOR_SIZE, SEEK_SET);
        std::fwrite(bytes.data(), sizeof(u8), bytes.size(), file);
    }
};

static std::shared_ptr<spdlog::logger> logger;

static std::queue<u32> data_fifo;

static MemoryStick memory_stick;

static struct {
    u16 interrupt_status;
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

    int command_data_count;
    u8 pro_command;

    // For commands that needs additional params
    u32 data_length;
    u32 data_count;

    u16 num_sectors;
    u32 lba;

    // Command 0x8 sets these
    u8 read_idx, read_length;
    u8 write_idx, write_length;
} ctx;

static std::array<u8, 1 * SECTOR_SIZE> sector_buf;
static std::array<u8, 2 * SECTOR_SIZE> attributes;

static inline u64 align_up_pow2(u64 n) {
    if (n == 0) {
        return 1;
    }

    assert((n & 0x8000000000000000) == 0);

    n--;

    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n |= n >> 32;

    return n + 1;
}

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
template<typename T>
static inline void byteswap_to_buf(u8* buf, const T data);

template<>
inline void byteswap_to_buf(u8* buf, const u8 data) {
    buf[0] = data;
}

template<>
inline void byteswap_to_buf(u8* buf, const u16 data) {
    buf[0] = data >> 8;
    buf[1] = data >> 0;
}

template<>
inline void byteswap_to_buf(u8* buf, const u32 data) {
    buf[0] = data >> 24;
    buf[1] = data >> 16;
    buf[2] = data >> 8;
    buf[3] = data >> 0;
}

static void assert_interrupt() {
    HW_MSIF0_STATUS.interrupt = 1;

    ctx.interrupt_status |= 4;

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

static void end_pro_command(const int buf_request) {
    HW_MSIF0_STATUS.raw  |= 1 << 13;
    HW_MSIF0_STATUS.ready = 1;

    if (buf_request != 0) {
        HW_MSIF0_STATUS.buf_request = 1;
        HW_MSIF0_STATUS.dma_request = 1;

        dmac::assert_ms_dma_request();
    } else {
        HW_MSIF0_STATUS.buf_request = 0;
        HW_MSIF0_STATUS.command_end = 1;

        assert_interrupt();
    }
}

static void start_pro_command(const u8 command) {
    static u32 event_id = scheduler::NO_EVENT_ID;

    bool buf_request = false;

    ctx.data_count  = 0;
    ctx.data_length = SECTOR_SIZE / sizeof(u32);

    switch (command) {
        case ProCommand::PRO_COMMAND_SLEEP:
            // Zzz...
            logger->debug("PRO_SLEEP");
            break;
        case ProCommand::PRO_COMMAND_READ:
            logger->debug("PRO_READ");

            assert(ctx.num_sectors > 0);

            memory_stick.read_sector(sector_buf, ctx.lba);

            buf_request = true;
            break;
        case ProCommand::PRO_COMMAND_WRITE:
            logger->debug("PRO_WRITE");

            assert(ctx.num_sectors > 0);

            buf_request = true;
            break;
        case ProCommand::PRO_COMMAND_ATTR:
            logger->debug("PRO_ATTR");

            assert(ctx.lba < 2);
            assert(ctx.num_sectors == 1);

            std::memcpy(sector_buf.data(), attributes.data() + SECTOR_SIZE * ctx.lba, SECTOR_SIZE);

            buf_request = true;
            break;
        case ProCommand::PRO_COMMAND_STOP:
            logger->debug("PRO_STOP");
            break;
        case 0x40:
            logger->warn("Unimplemented PRO command {:02X}", command);
            break;
        default:
            logger->error("Unimplemented PRO command {:02X}", command);
            exit(1);
    }

    HW_MSIF0_STATUS.raw  &= ~(1 << 13);
    HW_MSIF0_STATUS.ready = 0;

    if (event_id == scheduler::NO_EVENT_ID) {
        event_id = scheduler::register_event("MEMORYSTICK");
    }

    scheduler::schedule_event(event_id, end_pro_command, buf_request, scheduler::from_microseconds(50));
}

static u8 read_register(const u8 idx) {
    const u8 MS_TYPE_PRO     = 0x01;
    const u8 MS_CATEGORY_PRO = 0x00;
    const u8 MS_CLASS_PRO    = 0x00;

    switch (idx) {
        case ProRegister::PRO_REGISTER_INT:
            logger->debug("INT read");
            return (HW_MSIF0_STATUS.raw & 0xF) << 4;
        case ProRegister::PRO_REGISTER_STA0:
            logger->debug("STA0 read");
            return 0;
        case ProRegister::PRO_REGISTER_TYPE:
            logger->debug("TYPE read");
            return MS_TYPE_PRO;
        case ProRegister::PRO_REGISTER_CATEGORY:
            logger->debug("CATEGORY read");
            return MS_CATEGORY_PRO;
        case ProRegister::PRO_REGISTER_CLASS:
            logger->debug("CLASS read");
            return MS_CLASS_PRO;
        case 0x00:
        case 0x03:
        case 0x05:
            logger->warn("Unimplemented read @ {:02X}", idx);
            return 0;
        default:
            logger->error("Unimplemented read @ {:02X}", idx);
            exit(1);
    }
}

static void write_register(const u8 idx, const u8 data) {
    switch (idx) {
        case ProRegister::PRO_REGISTER_CFG:
            logger->debug("CFG write = {:02X}", data);
            break;
        case ProRegister::PRO_REGISTER_ADDR_LO:
            logger->debug("ADDR_LO write = {:02X}", data);

            ctx.lba = (ctx.lba & 0xFFFFFF00) | data;
            break;
        default:
            logger->error("Unimplemented write @ {:02X} = {:02X}", idx, data);
            exit(1);
    }
}

static u32 tpc_read_reg() {
    logger->debug("READ_REG (index: {:02X}, length: {})", ctx.read_idx, ctx.read_length);

    if (ctx.read_length == 0) {
        logger->warn("READ_REG has no more data");
        return 0;
    }

    u32 data = 0;

    for (int i = 0; (i < 4) && (ctx.read_length > 0); i++, ctx.read_length--) {
        data |= read_register(ctx.read_idx++) << (8 * i);
    }

    return data;
}

static u32 tpc_get_int() {
    logger->debug("GET_INT");
    return read_register(ProRegister::PRO_REGISTER_INT);
}

static void tpc_set_regs_window(const u32 data) {
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

static void tpc_set_cmd_ex(const u32 idx, const u32 data) {
    static u32 buf[2];

    assert(idx < (sizeof(buf) / sizeof(u32)));

    buf[idx] = data;

    if (idx == 0) {
        return;
    }

    const u8 command = byteswap_from_buf<u8>(&((u8*)&buf)[0]);

    ctx.num_sectors = byteswap_from_buf<u16>(&((u8*)&buf)[1]);
    ctx.lba = byteswap_from_buf<u32>(&((u8*)&buf)[3]);

    logger->debug("SET_CMD_EX (command: {:02X}, sector: {}, count: {})", command, ctx.lba, ctx.num_sectors);

    start_pro_command(command);
}

static void tpc_write_reg(const u32 data) {
    logger->debug("WRITE_REG (index: {:02X}, length: {})", ctx.write_idx, ctx.write_length);

    if (ctx.write_length == 0) {
        logger->warn("WRITE_REG takes no more data");
        return;
    }

    for (int i = 0; (i < 4) && (ctx.write_length > 0); i++, ctx.write_length--) {
        write_register(ctx.write_idx++, data >> (8 * i));
    }
}

static void tpc_set_cmd(const u8 command) {
    logger->debug("SET_CMD (command: {:02X})", command);

    // This can also start Classic commands, but since we don't handle MS Classic...
    start_pro_command(command);
}

static void prepare_command(const u16 command) {
    HW_MSIF0_COMMAND.raw = command;

    const u32 tpc = HW_MSIF0_COMMAND.tpc;
    const u32 length = HW_MSIF0_COMMAND.length;

    if (!memory_stick.is_mounted()) {
        HW_MSIF0_STATUS.ready   = 1;
        HW_MSIF0_STATUS.timeout = 1;
        return;
    }

    // The data length always needs to be a multiple of 8
    ctx.data_length = align_up(HW_MSIF0_COMMAND.length) / sizeof(u32);
    ctx.data_count  = 0;

    switch (tpc) {
        case Tpc::TPC_READ_LONG_DATA:
            ctx.data_length = SECTOR_SIZE / sizeof(u32);
        case Tpc::TPC_READ_SHORT_DATA:
        case Tpc::TPC_READ_REG:
        case Tpc::TPC_GET_INT:
            // These commands don't receive any data
        case Tpc::TPC_SET_REGS_WINDOW:
        case Tpc::TPC_SET_CMD_EX:
        case Tpc::TPC_WRITE_REG:
        case Tpc::TPC_SET_CMD:
            break;
        default:
            logger->error("Unimplemented command {:X} (length: {})", tpc, length);
            exit(1);
    }

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

enum CommandData {
    COMMAND_DATA_CMD,
    COMMAND_DATA_NUMSEC_HI,
    COMMAND_DATA_NUMSEC_LO,
    COMMAND_DATA_LBA_HI,
    COMMAND_DATA_LBA_MIDHI,
    COMMAND_DATA_LBA_MIDLO,
    COMMAND_DATA_LBA_LO,
};

static void set_command_data(const u8 data) {
    const int count = ctx.command_data_count;

    switch (count) {
        case CommandData::COMMAND_DATA_CMD:
            ctx.pro_command = data;

            switch (data) {
                case ProCommand::PRO_COMMAND_READ:
                case ProCommand::PRO_COMMAND_WRITE:
                case ProCommand::PRO_COMMAND_ATTR:
                    break;
                default:
                    logger->error("Unimplemented PRO command {:02X}", data),
                    exit(1);
            }
            break;
        case CommandData::COMMAND_DATA_NUMSEC_HI:
            HW_MSIF0_NUMSEC = (HW_MSIF0_NUMSEC & 0xFF) | (data << 8);
            break;
        case CommandData::COMMAND_DATA_NUMSEC_LO:
            HW_MSIF0_NUMSEC = (HW_MSIF0_NUMSEC & ~0xFF) | data;
            break;
        case CommandData::COMMAND_DATA_LBA_HI:
            ctx.lba = (ctx.lba & 0xFFFFFF) | (data << 24);
            break;
        case CommandData::COMMAND_DATA_LBA_MIDHI:
            ctx.lba = (ctx.lba & 0xFF00FFFF) | (data << 16);
            break;
        case CommandData::COMMAND_DATA_LBA_MIDLO:
            ctx.lba = (ctx.lba & 0xFFFF00FF) | (data << 8);
            break;
        case CommandData::COMMAND_DATA_LBA_LO:
            ctx.lba = (ctx.lba & 0xFFFFFF00) | data;

            start_pro_command(ctx.pro_command);
            break;
        default:
            logger->error("Unimplemented CMDDATA{} write = {:02X}", count, data);
            exit(1);
    }

    logger->debug("CMDDATA{} write = {:02X}", count, data);

    if (ctx.command_data_count == CommandData::COMMAND_DATA_LBA_LO) {
        ctx.command_data_count = 0;
    } else {
        ctx.command_data_count++;
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
            return ctx.interrupt_status;
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
            return 0x40;
        default:
            logger->error("Unmapped read16 @ {:08X}", addr);
            exit(1);
    }
}

static u32 read32(const u32 addr) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_PAGEDATA: {
            logger->debug("PAGEDATA read32");

            assert(HW_MSIF0_STATUS.dma_request);

            if (ctx.data_count < ctx.data_length) {
                u32 data;

                data  = sector_buf[sizeof(u32) * ctx.data_count + 0];
                data |= sector_buf[sizeof(u32) * ctx.data_count + 1] << 8;
                data |= sector_buf[sizeof(u32) * ctx.data_count + 2] << 16;
                data |= sector_buf[sizeof(u32) * ctx.data_count + 3] << 24;
                
                ctx.data_count++;

                if (ctx.data_count == ctx.data_length) {
                    ctx.data_count = 0;

                    ctx.num_sectors--;

                    if (ctx.num_sectors == 0) {
                        HW_MSIF0_STATUS.command_end = 1;
                        HW_MSIF0_STATUS.dma_request = 0;
                        HW_MSIF0_STATUS.buf_request = 0;

                        dmac::clear_ms_dma_request();

                        assert_interrupt();
                    } else {
                        ctx.lba++;

                        memory_stick.read_sector(sector_buf, ctx.lba);
                    }
                }

                return data;
            }

            assert(false);
        }
        case IoAddress::IO_ADDRESS_DATA: {
            logger->debug("DATA read32");

            assert(HW_MSIF0_STATUS.dma_request);

            if (ctx.data_count < ctx.data_length) {
                const u8 tpc = ctx.command.tpc;

                u32 data;
            
                switch (tpc) {
                    case Tpc::TPC_READ_LONG_DATA: {
                        data  = sector_buf[sizeof(u32) * ctx.data_count + 0];
                        data |= sector_buf[sizeof(u32) * ctx.data_count + 1] << 8;
                        data |= sector_buf[sizeof(u32) * ctx.data_count + 2] << 16;
                        data |= sector_buf[sizeof(u32) * ctx.data_count + 3] << 24;

                        if ((ctx.data_count + 1) == (SECTOR_SIZE / sizeof(u32))) {
                            HW_MSIF0_STATUS.command_end = 1;

                            assert_interrupt();
                        }
                        break;
                    }
                    case Tpc::TPC_READ_SHORT_DATA: {
                        data = 0;

                        if ((ctx.data_count + 1) == (SECTOR_SIZE / sizeof(u32))) {
                            HW_MSIF0_STATUS.command_end = 1;

                            assert_interrupt();
                        }
                        break;
                    }
                    case Tpc::TPC_READ_REG:
                        data = tpc_read_reg();
                        break;
                    case Tpc::TPC_GET_INT:
                        data = tpc_get_int();
                        break;
                    default:
                        logger->error("Unimplemented DATA read for TPC {:X}", tpc);
                        exit(1);
                }

                ctx.data_count++;

                if (ctx.data_count == ctx.data_length) {
                    HW_MSIF0_STATUS.dma_request = 0;
                }

                return data;
            }   
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
        case IoAddress::IO_ADDRESS_CMDDATA:
            set_command_data(data);
            break;
        default:
            logger->error("Unmapped write8 @ {:08X} = {:02X}", addr, data);
            exit(1);
    }
}

static void write16(const u32 addr, const u16 data) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_INTRSTAT:
            logger->debug("INTRSTAT write16 = {:04X}", data);

            HW_MSIF0_STATUS.raw  &= ~((data >> 4) & 0xF);
            ctx.interrupt_status &= ~((data >> 0) & 0xF);

            if (ctx.interrupt_status == 0) {
                clear_interrupt();
            } else {
                assert_interrupt();
            }
            break;
        case IoAddress::IO_ADDRESS_NUMSEC:
            logger->debug("NUMSEC write16 = {:04X}", data);

            HW_MSIF0_NUMSEC = data;
            break;
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
        case MSIF0_ADDR + 0x002:
        case MSIF0_ADDR + 0x020:
            logger->warn("Unmapped write16 @ {:08X} = {:04X}", addr, data);
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
        case IoAddress::IO_ADDRESS_PAGEDATA: {
            logger->debug("PAGEDATA write32 = {:08X}", data);
            
            assert(HW_MSIF0_STATUS.dma_request);

            if (ctx.data_count < ctx.data_length) {
                sector_buf[sizeof(u32) * ctx.data_count + 0] = data >> 0;
                sector_buf[sizeof(u32) * ctx.data_count + 1] = data >> 8;
                sector_buf[sizeof(u32) * ctx.data_count + 2] = data >> 16;
                sector_buf[sizeof(u32) * ctx.data_count + 3] = data >> 24;
                
                ctx.data_count++;

                if (ctx.data_count == ctx.data_length) {
                    ctx.data_count = 0;

                    ctx.num_sectors--;

                    if (ctx.num_sectors == 0) {
                        HW_MSIF0_STATUS.command_end = 1;
                        HW_MSIF0_STATUS.dma_request = 0;
                        HW_MSIF0_STATUS.buf_request = 0;

                        dmac::clear_ms_dma_request();

                        assert_interrupt();
                    } else {
                        ctx.lba++;

                        memory_stick.write_sector(sector_buf, ctx.lba);
                    }
                }

                break;
            }

            assert(false);
        }
        case IoAddress::IO_ADDRESS_DATA: {
            logger->debug("DATA write32 = {:08X}", data);
            
            assert(HW_MSIF0_STATUS.dma_request);

            if (ctx.data_count < ctx.data_length) {
                if ((ctx.data_count + 1) == ctx.data_length) {
                    // Some commands set this flag again, so we should
                    // clear it here
                    HW_MSIF0_STATUS.dma_request = 0;

                    dmac::clear_ms_dma_request();
                }

                const u8 tpc = ctx.command.tpc;

                switch (tpc) {
                    case Tpc::TPC_SET_REGS_WINDOW:
                        if (ctx.data_count == 0) {
                            tpc_set_regs_window(data);
                        }
                        break;
                    case Tpc::TPC_SET_CMD_EX:
                        if (ctx.data_count < 2) {
                            tpc_set_cmd_ex(ctx.data_count, data);
                        }
                        break;
                    case Tpc::TPC_WRITE_REG:
                        tpc_write_reg(data);
                        break;
                    case Tpc::TPC_SET_CMD:
                        if (ctx.data_count == 0) {
                            tpc_set_cmd(data);
                        }
                        break;
                    default:
                        logger->error("Unimplemented DATA write for TPC {:X}", tpc);
                        exit(1);
                }
            }

            ctx.data_count++;
            break;
        }
        case IoAddress::IO_ADDRESS_CONTROL:
            logger->debug("CONTROL write32 = {:08X}", data);
            set_control(data);
            break;
        case MSIF0_ADDR + 0x040:
            logger->warn("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            break;
        default:
            logger->error("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            exit(1);
    }
}

static void get_attributes() {
    if (!memory_stick.is_mounted()) {
        return;
    }

    // TODO: use named constants

    // Attribute header
    byteswap_to_buf<u16>(&attributes[0x000], 0xA5C3);
    byteswap_to_buf<u16>(&attributes[0x002], 0);
    byteswap_to_buf<u8> (&attributes[0x004], 4);

    // SYS
    byteswap_to_buf<u32>(&attributes[0x010], 0x000001A0);
    byteswap_to_buf<u32>(&attributes[0x014], 0x00000060);
    byteswap_to_buf<u8> (&attributes[0x018], 0x10);

    u32 block_count = -1;
    u32 sectors_per_block = 8;

    // Since the block count is a 16-bit number, we need to pick a block size
    // such that block count < 0x10000

    const u64 aligned_file_size = align_up_pow2(memory_stick.get_file_size());

    while (block_count >= 0x10000) {
        sectors_per_block <<= 1;

        block_count = aligned_file_size / (SECTOR_SIZE * sectors_per_block);
    }

    assert(sectors_per_block < 0x10000);

    const u16 user_block_count = memory_stick.get_file_size() / (SECTOR_SIZE * sectors_per_block);

    logger->info(
        "Sectors per block: {}, block count: {} (user: {})",
        sectors_per_block,
        block_count,
        user_block_count
    );

    byteswap_to_buf<u8> (&attributes[0x1A0], 2);
    byteswap_to_buf<u16>(&attributes[0x1A2], sectors_per_block);
    byteswap_to_buf<u16>(&attributes[0x1A4], block_count);
    byteswap_to_buf<u16>(&attributes[0x1A6], user_block_count);
    byteswap_to_buf<u16>(&attributes[0x1A8], SECTOR_SIZE);
    byteswap_to_buf<u16>(&attributes[0x1DC], SECTOR_SIZE);
    byteswap_to_buf<u8> (&attributes[0x1E3], 1);
    byteswap_to_buf<u8> (&attributes[0x1E6], 1);

    // DEVINFO
    byteswap_to_buf<u32>(&attributes[0x01C], 0x00000200);
    byteswap_to_buf<u32>(&attributes[0x020], 0x00000010);
    byteswap_to_buf<u8> (&attributes[0x024], 0x30);

    // MBR
    byteswap_to_buf<u32>(&attributes[0x028], 0x00000210);
    byteswap_to_buf<u32>(&attributes[0x02C], 0x00000010);
    byteswap_to_buf<u8> (&attributes[0x030], 0x20);

    // Read MBR from MS image
    std::array<u8, SECTOR_SIZE> mbr;

    memory_stick.read_sector(mbr, 0);

    // PBR (no clue how this works)
    byteswap_to_buf<u32>(&attributes[0x034], 0x00000220);
    byteswap_to_buf<u32>(&attributes[0x038], 0x00000060);
    byteswap_to_buf<u8> (&attributes[0x03C], 0x22);
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

    get_attributes();
}

void soft_reset() {
    HW_MSIF0_STATUS.dma_request = 0;

    clear_data_fifo();
    clear_interrupt();

    HW_MSIF0_STATUS.ready = 1;
    HW_MSIF0_STATUS.command_end = 1;
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
