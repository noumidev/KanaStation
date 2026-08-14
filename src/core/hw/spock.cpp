/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/spock.cpp - SPOCK crypto engine */

#include <core/hw/spock.hpp>

#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <common/types.hpp>
#include <core/kanacore.hpp>
#include <core/scheduler.hpp>
#include <core/hw/atapi.hpp>
#include <core/hw/bus.hpp>
#include <core/hw/gpio.hpp>
#include <core/hw/intc.hpp>

namespace kanacore::hw::spock {

using namespace common;

constexpr u64 SPOCK_ADDR = 0x1DF00000;
constexpr u64 SPOCK_SIZE = 0x1000;

constexpr int SPOCK_INTERRUPT = 6;

constexpr u64 NUM_TRANS_REGS = 10;

enum IoAddress {
    IO_ADDRESS_RESET    = SPOCK_ADDR + 0x008,
    IO_ADDRESS_COMMAND  = SPOCK_ADDR + 0x010,
    IO_ADDRESS_DRVMODE  = SPOCK_ADDR + 0x018,
    IO_ADDRESS_STATUS   = SPOCK_ADDR + 0x01C,
    IO_ADDRESS_INTRSTAT = SPOCK_ADDR + 0x020,
    IO_ADDRESS_INTRCLR  = SPOCK_ADDR + 0x024,
    IO_ADDRESS_INTREN   = SPOCK_ADDR + 0x028,
    IO_ADDRESS_INTRDIS  = SPOCK_ADDR + 0x02C,
    IO_ADDRESS_RESULT   = SPOCK_ADDR + 0x030,
    IO_ADDRESS_TADDR0   = SPOCK_ADDR + 0x040,
    IO_ADDRESS_TLENGTH0 = SPOCK_ADDR + 0x044,
    IO_ADDRESS_TLENGTH9 = SPOCK_ADDR + 0x08C,
    IO_ADDRESS_LENGTH   = SPOCK_ADDR + 0x090,
};

#define HW_SPOCK_RESET    ctx.reset
#define HW_SPOCK_COMMAND  ctx.command
#define HW_SPOCK_STATUS   ctx.status
#define HW_SPOCK_INTRSTAT ctx.interrupt_status
#define HW_SPOCK_RESULT   ctx.result
#define HW_SPOCK_TADDR    ctx.transfer_addr
#define HW_SPOCK_TLENGTH  ctx.transfer_length
#define HW_SPOCK_LENGTH   ctx.total_length

enum SpockCommand {
    SPOCK_COMMAND_LEPTON_CHALLENGE = 0x01,
    SPOCK_COMMAND_AUTHENTICATE     = 0x02,
    SPOCK_COMMAND_SET_QTGP1        = 0x03,
    SPOCK_COMMAND_GET_QTGP2        = 0x04,
    SPOCK_COMMAND_GET_QTGP3        = 0x05,
    SPOCK_COMMAND_DECRYPT_MKI      = 0x08,
    SPOCK_COMMAND_DECRYPT_DKI      = 0x09,
    SPOCK_COMMAND_DECRYPT_SECTORS  = 0x0A,
    SPOCK_COMMAND_RESET            = 0x0B,
};

enum SpockResult {
    SPOCK_RESULT_OK,
};

static std::shared_ptr<spdlog::logger> logger;

static struct {
    u32 reset;

    union {
        u32 raw;

        struct {
            u32 command : 8;
            u32 flag    : 1; // Has to be 1?
            u32         : 23;
        };
    } command;

    union {
        u32 raw;

        struct {
            u32 busy : 1;
            u32      : 31;
        };
    } status;

    u32 interrupt_status;
    u32 interrupt_mask;
    u32 result;

    u32 transfer_addr[NUM_TRANS_REGS];
    u32 transfer_length[NUM_TRANS_REGS];
    u32 total_length;

    i64 command_delay;
    bool send_ata_interrupt;
} ctx;

static void dma_read(const u32 addr, u8* data, const u32 size) {
    bus::Bus* sc_bus = kanacore::get_sc_bus_ptr();

    for (u32 i = 0; i < size; i++) {
        data[i] = sc_bus->read<u8>(addr + i);
    }
}

static void dma_write(const u32 addr, const u8* data, const u32 size) {
    bus::Bus* sc_bus = kanacore::get_sc_bus_ptr();

    for (u32 i = 0; i < size; i++) {
        sc_bus->write<u8>(addr + i, data[i]);
    }
}

static void dma_memset(const u32 addr, const u8 data, const u32 size) {
    bus::Bus* sc_bus = kanacore::get_sc_bus_ptr();

    for (u32 i = 0; i < size; i++) {
        sc_bus->write<u8>(addr + i, data);
    }
}

static void check_pending_interrupts() {
    if ((ctx.interrupt_status & ctx.interrupt_mask) != 0) {
        intc::assert_sc_interrupt(SPOCK_INTERRUPT);
    } else {
        intc::clear_sc_interrupt(SPOCK_INTERRUPT);
    }
}

static void assert_interrupt(const int intr_num) {
    // SPOCK appears to have a wide range of interrupts, but I don't know what
    // all of them are

    ctx.interrupt_status |= 1 << intr_num;

    check_pending_interrupts();
}

static void end_command(const int result) {
    HW_SPOCK_STATUS.busy = 0;
    HW_SPOCK_RESULT = result;

    if (ctx.send_ata_interrupt) {
        ctx.send_ata_interrupt = false;

        atapi::assert_transfer_end_interrupt();
    }

    assert_interrupt(0);
}

static i32 command_lepton_challenge() {
    logger->debug("LEPTON_CHALLENGE");

    // umdman waits for this pin to go high after sending the LEPTON_CHALLENGE command
    gpio::set_pin(gpio::PIN_LEPTON_ALIVE);

    // Presumably, this is also where LEPTON will check if a UMD is inserted?
    // I should test this on hardware, but doing it here doesn't seem to hurt
    scheduler::schedule_event(
        scheduler::EventType::LEPTON,
        atapi::umd_initialize,
        0,
        scheduler::from_microseconds(5000),
        true
    );

    return SpockResult::SPOCK_RESULT_OK;
}

static i32 command_authenticate() {
    logger->debug("AUTHENTICATE");
    return SpockResult::SPOCK_RESULT_OK;
}

static i32 command_set_qtgp1() {
    logger->debug("SET_QTGP1");
    // This is said to get SPOCK a seed from LEPTON...
    return SpockResult::SPOCK_RESULT_OK;
}

// https://www.psdevwiki.com/psp/Spock#Command_4_(Step_2)
static i32 command_get_qtgp2() {
    static constexpr u8 QTGP2[] = {
        0x3E, 0x96, 0xA1, 0xF5, 0x00, 0x00, 0x00, 0x00,
    };

    logger->debug("GET_QTGP2");

    assert(sizeof(QTGP2) == HW_SPOCK_TLENGTH[0]);

    // The actual value returned doesn't appear to matter too much
    dma_write(HW_SPOCK_TADDR[0], QTGP2, HW_SPOCK_TLENGTH[0]);
    return SpockResult::SPOCK_RESULT_OK;
}

// https://www.psdevwiki.com/psp/Spock#Command_5_(Step_3)
static i32 command_get_qtgp3() {
    static constexpr u8 QTGP3[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xA4,
        0xDD, 0x85, 0x33, 0x99, 0xD7, 0x06, 0x00, 0x00,
    };

    logger->debug("GET_QTGP3");

    assert(sizeof(QTGP3) == HW_SPOCK_TLENGTH[0]);

    // The actual value returned doesn't appear to matter too much
    dma_write(HW_SPOCK_TADDR[0], QTGP3, HW_SPOCK_TLENGTH[0]);
    return SpockResult::SPOCK_RESULT_OK;
}

// https://www.psdevwiki.com/psp/Spock#Command_8_(Decrypt_UMD_MKI_/_Read_Media_Key_Index_/_Step_4)
static i32 command_decrypt_mki() {
    static constexpr u8 MKI[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x6A, 0x1D, 0x49, 0x3E, 0x9F, 0x74, 0x84, 0x8D,
        0x2E, 0x39, 0xDA, 0x7D, 0x63, 0xA8, 0xC8, 0x80,
        0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80,
        0x2E, 0x83, 0x6A, 0xD5, 0xFD, 0x3C, 0xD1, 0x97,
        0xB3, 0xBC, 0x7A, 0xC5, 0x2A, 0x31, 0xDD, 0xB8,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x3E, 0x66, 0x41, 0xAE, 0x34, 0xCA, 0x36, 0xEC,
        0x99, 0x75, 0x2A, 0xF6, 0x94, 0xDC, 0xC6, 0x66,
    };

    logger->debug("DECRYPT_MKI");

    assert(HW_SPOCK_TLENGTH[0] >= sizeof(MKI));

    // On hardware, this reads raw data ("Media Key Index") from
    // some place on the UMD and decrypts it
    dma_memset(HW_SPOCK_TADDR[0], 0, HW_SPOCK_TLENGTH[0]);
    dma_write (HW_SPOCK_TADDR[0], MKI, sizeof(MKI));

    // Commands that read data from UMD trigger ATA interrupts
    ctx.send_ata_interrupt = true;

    ctx.command_delay = 12000;

    return SpockResult::SPOCK_RESULT_OK;
}

static i32 command_decrypt_dki() {
    logger->debug("DECRYPT_DKI");

    // This command decrypts UMD leaves from IDstorage.
    // TODO: figure out how this works, would be cool to do it
    std::vector<u8> dki(HW_SPOCK_TLENGTH[4]);

    dma_read(HW_SPOCK_TADDR[4], dki.data(), dki.size());

    ctx.command_delay = 1000;

    return SpockResult::SPOCK_RESULT_OK;
}

static i32 command_decrypt_sectors() {
    logger->debug("DECRYPT_SECTORS");

    assert((HW_SPOCK_LENGTH > 0) && ((HW_SPOCK_LENGTH % 0x810) == 0));

    // On hardware, this generates a sector key to decrypt incoming UMD sectors.
    // The decrypted sectors are written to RAM

    const int total_length = (HW_SPOCK_LENGTH / 0x810) * 0x800;

    std::vector<u8> sector_bytes(total_length);

    atapi::read_sectors(sector_bytes);

    u32 sector_num = 0;

    for (u32 i = 0; i < NUM_TRANS_REGS; i++) {
        const u32 addr = HW_SPOCK_TADDR[i] & ~3;

        assert((HW_SPOCK_TLENGTH[i] & 0x7FF) == 0);
    
        dma_write(addr, sector_bytes.data() + sector_num * 0x800, HW_SPOCK_TLENGTH[i]);

        sector_num += HW_SPOCK_TLENGTH[i] / 0x800;

        if ((sector_num * 0x800) >= (u32)total_length) {
            break;
        }
    }

    // Commands that read data from UMD trigger ATA interrupts
    ctx.send_ata_interrupt = true;

    ctx.command_delay = sector_num * 1500;

    return SpockResult::SPOCK_RESULT_OK;
}

static i32 command_reset() {
    logger->debug("RESET");
    return SpockResult::SPOCK_RESULT_OK;
}

static void start_command() {
    assert(!HW_SPOCK_STATUS.busy);
    assert(HW_SPOCK_COMMAND.flag == 1);

    // Default delay
    ctx.command_delay = 10;

    i32 result;

    const u8 command = HW_SPOCK_COMMAND.command;

    switch (command) {
        case SpockCommand::SPOCK_COMMAND_LEPTON_CHALLENGE:
            result = command_lepton_challenge();
            break;
        case SpockCommand::SPOCK_COMMAND_AUTHENTICATE:
            result = command_authenticate();
            break;
        case SpockCommand::SPOCK_COMMAND_SET_QTGP1:
            result = command_set_qtgp1();
            break;
        case SpockCommand::SPOCK_COMMAND_GET_QTGP2:
            result = command_get_qtgp2();
            break;
        case SpockCommand::SPOCK_COMMAND_GET_QTGP3:
            result = command_get_qtgp3();
            break;
        case SpockCommand::SPOCK_COMMAND_DECRYPT_MKI:
            result = command_decrypt_mki();
            break;
        case SpockCommand::SPOCK_COMMAND_DECRYPT_DKI:
            result = command_decrypt_dki();
            break;
        case SpockCommand::SPOCK_COMMAND_DECRYPT_SECTORS:
            result = command_decrypt_sectors();
            break;
        case SpockCommand::SPOCK_COMMAND_RESET:
            result = command_reset();
            break;
        default:
            logger->error("Unimplemented command {:02X}", command);
            exit(1);
    }

    if (command == SpockCommand::SPOCK_COMMAND_RESET) {
        // Seems to not trigger an interrupt. The firmware immediately sends a new command
        // after RESET
        return;
    }

    // Timings will vary between commands, so we just pick a short delay for now
    scheduler::schedule_event(
        scheduler::EventType::SPOCK,
        end_command,
        result,
        scheduler::from_microseconds(ctx.command_delay),
        true
    );

    HW_SPOCK_STATUS.busy = 1;
}

static u32 read(const u32 addr) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_RESET:
            logger->debug("RESET read32");
            return HW_SPOCK_RESET;
        case IoAddress::IO_ADDRESS_COMMAND:
            logger->debug("COMMAND read32");
            return HW_SPOCK_COMMAND.raw;
        case IoAddress::IO_ADDRESS_DRVMODE:
            logger->debug("DRVMODE read32");
            return 0; // ?
        case IoAddress::IO_ADDRESS_STATUS:
            logger->debug("STATUS read32");
            return HW_SPOCK_STATUS.raw;
        case IoAddress::IO_ADDRESS_INTRSTAT:
            logger->debug("INTRSTAT read32 ({:08})", HW_SPOCK_INTRSTAT);
            return HW_SPOCK_INTRSTAT;
        case IoAddress::IO_ADDRESS_INTRCLR:
            logger->debug("INTRCLR read32");

            // Probably returns 0 (umdman presumably uses RMW to write the clear value)
            return 0;
        case IoAddress::IO_ADDRESS_INTREN:
            logger->debug("INTREN read32");

            // This probably just returns the mask?
            return ctx.interrupt_mask;
        case IoAddress::IO_ADDRESS_INTRDIS:
            logger->debug("INTRDIS read32");

            // Probably returns 0 (umdman presumably uses RMW to write the disable value)
            return 0;
        case IoAddress::IO_ADDRESS_RESULT:
            logger->debug("RESULT read32");
            return HW_SPOCK_RESULT;
        case IoAddress::IO_ADDRESS_LENGTH:
            logger->debug("LENGTH read32");
            return HW_SPOCK_LENGTH;
        case SPOCK_ADDR + 0x014:
        case SPOCK_ADDR + 0x038:
            logger->warn("Unmapped read32 @ {:08X}", addr);
            return 0;
        default:
            logger->error("Unmapped read32 @ {:08X}", addr);
            exit(1);
    }
}

static void write(const u32 addr, const u32 data) {
    if ((addr >= IoAddress::IO_ADDRESS_TADDR0) && (addr <= IoAddress::IO_ADDRESS_TLENGTH9)) {
        const int trans_idx = (addr - IoAddress::IO_ADDRESS_TADDR0) / sizeof(u64);

        if ((addr & 4) != 0) {
            logger->debug("TLENGTH{} write32 = {:08X}", trans_idx, data);

            HW_SPOCK_TLENGTH[trans_idx] = data;
        } else {
            logger->debug("TADDR{} write32 = {:08X}", trans_idx, data);

            HW_SPOCK_TADDR[trans_idx] = data;
        }

        return;
    }

    switch (addr) {
        case IoAddress::IO_ADDRESS_RESET:
            logger->debug("RESET write32 = {:08X}", data);
            
            HW_SPOCK_RESET = data;

            if ((HW_SPOCK_RESET & 1) != 0) {
                HW_SPOCK_RESET &= ~1;
            }
            break;
        case IoAddress::IO_ADDRESS_COMMAND:
            logger->debug("COMMAND write32 = {:08X}", data);

            HW_SPOCK_COMMAND.raw = data;

            start_command();
            break;
        case IoAddress::IO_ADDRESS_INTRCLR:
            logger->debug("INTRCLR write32 = {:08X}", data);

            HW_SPOCK_INTRSTAT &= ~data;
            break;
        case IoAddress::IO_ADDRESS_INTREN:
            logger->debug("INTREN write32 = {:08X}", data);

            ctx.interrupt_mask |= data;
            break;
        case IoAddress::IO_ADDRESS_INTRDIS:
            logger->debug("INTRDIS write32 = {:08X}", data);

            ctx.interrupt_mask &= ~data;
            break;
        case IoAddress::IO_ADDRESS_LENGTH:
            logger->debug("LENGTH write32 = {:08X}", data);

            HW_SPOCK_LENGTH = data;
            break;
        case SPOCK_ADDR + 0x038:
        case SPOCK_ADDR + 0x094:
            logger->warn("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            break;
        default:
            logger->error("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            exit(1);
    }

    check_pending_interrupts();
}

void initialize() {
    logger = spdlog::stdout_color_st("SPOCK");

    std::memset(&ctx, 0, sizeof(ctx));
}

void soft_reset() {
    
}

void hard_reset() {
    const bus::PageDescriptor page_desc {
        // To my knowledge, KIRK I/O is never not read/written using 32-bit accesses
        .read32_func  = read,
        .write32_func = write,
    };

    kanacore::get_sc_bus_ptr()->map(SPOCK_ADDR, SPOCK_SIZE, page_desc);
}

void shutdown() {

}

};
