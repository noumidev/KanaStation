/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/atapi.cpp - ATAPI for UMD */

#include <core/hw/atapi.hpp>

#include <cassert>
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

namespace kanacore::hw::atapi {

using namespace common;

constexpr u64 ATAPI_AHB_ADDR = 0x1D600000;
constexpr u64 ATAPI_ADDR     = 0x1D700000;
constexpr u64 ATAPI_SIZE     = 0x1000;

constexpr int ATA_INTERRUPT = 5;

constexpr u32 AHB_REVISION = 0x00010033;

// https://www.psdevwiki.com/psp/Hardware_Registers#0xBD600000:_ATA/UMD

enum IoAddress {
    IO_ADDRESS_AHB_REVISION = ATAPI_AHB_ADDR + 0x000,
    IO_ADDRESS_AHB_CONTROL  = ATAPI_AHB_ADDR + 0x004,
    IO_ADDRESS_AHB_RESET    = ATAPI_AHB_ADDR + 0x010,
    IO_ADDRESS_AHB_PIODLY   = ATAPI_AHB_ADDR + 0x014,
    IO_ADDRESS_AHB_MDMADLY  = ATAPI_AHB_ADDR + 0x01C,
    IO_ADDRESS_AHB_PIOCTRL  = ATAPI_AHB_ADDR + 0x034,
    IO_ADDRESS_AHB_INTR     = ATAPI_AHB_ADDR + 0x044,
    IO_ADDRESS_AHB_DEVSTAT  = ATAPI_AHB_ADDR + 0x04C,
    IO_ADDRESS_DATA         = ATAPI_ADDR     + 0x000,
    IO_ADDRESS_FEATURES     = ATAPI_ADDR     + 0x001,
    IO_ADDRESS_REASON       = ATAPI_ADDR     + 0x002,
    IO_ADDRESS_LBA_LO       = ATAPI_ADDR     + 0x003,
    IO_ADDRESS_LBA_MID      = ATAPI_ADDR     + 0x004,
    IO_ADDRESS_LBA_HI       = ATAPI_ADDR     + 0x005,
    IO_ADDRESS_DRIVE        = ATAPI_ADDR     + 0x006,
    IO_ADDRESS_COMMAND      = ATAPI_ADDR     + 0x007,
    IO_ADDRESS_STATUS       = ATAPI_ADDR     + 0x007,
    IO_ADDRESS_PKTEND       = ATAPI_ADDR     + 0x008,
    IO_ADDRESS_DEVCTRL      = ATAPI_ADDR     + 0x00E,
    IO_ADDRESS_ALTSTAT      = ATAPI_ADDR     + 0x00E,
};

enum AtaCommand {
    ATA_COMMAND_PACKET = 0xA0,
};

enum ScsiCommand {
    SCSI_COMMAND_INQUIRY = 0x12,
};

#define HW_ATAPI_AHB_PIOCTRL ctx.ahb.pio_control
#define HW_ATAPI_AHB_INTR    ctx.ahb.interrupt
#define HW_ATAPI_AHB_DEVSTAT ctx.ahb.device_status
#define HW_ATAPI_FEATURES    ctx.features
#define HW_ATAPI_REASON      ctx.interrupt_reason
#define HW_ATAPI_LBA         ctx.lba.raw
#define HW_ATAPI_LBA_LO      ctx.lba.lo
#define HW_ATAPI_LBA_MID     ctx.lba.mid
#define HW_ATAPI_LBA_HI      ctx.lba.hi
#define HW_ATAPI_DRIVE       ctx.drive
#define HW_ATAPI_STATUS      ctx.status
#define HW_ATAPI_DEVCTRL     ctx.device_control

static struct {
    // I wonder if the AHB regs have any connection to SPOCK/LEPTON
    struct {
        u32 pio_control;
        u32 interrupt; // Combined mask/status?
        u32 device_status;
    } ahb;

    u8 features;

    union {
        u8 raw;

        struct {
            u8 is_command  : 1;
            u8 from_device : 1;
            u8             : 6;
        };
    } interrupt_reason;

    union {
        u32 raw;

        struct {
            u32 lo  : 8;
            u32 mid : 8;
            u32 hi  : 8;
            u32     : 8;
        };
    } lba;

    u8 drive;

    union {
        u8 raw;

        // Some bits are probably unused/different, but it should be fine
        struct {
            u8 check_condition : 1;
            u8 sense_available : 1;
            u8 alignment_error : 1;
            u8 data_request    : 1;
            u8 write_error     : 1;
            u8 device_fault    : 1;
            u8 device_ready    : 1;
            u8 busy            : 1;
        };
    } status;

    union {
        u8 raw;

        struct {
            u8                   : 1;
            u8 interrupt_disable : 1;
            u8 software_reset    : 1;
            u8                   : 5;
        };
    } device_control;
} ctx;

static std::shared_ptr<spdlog::logger> logger;

static std::queue<u16> in_fifo;
static std::queue<u16> out_fifo;

static std::vector<u8> in_params;

static void assert_interrupt() {
    if (!HW_ATAPI_DEVCTRL.interrupt_disable) {
        intc::assert_sc_interrupt(ATA_INTERRUPT);
    }
}

static void set_reason(const bool is_command, const bool from_device) {
    HW_ATAPI_REASON.is_command  = is_command;
    HW_ATAPI_REASON.from_device = from_device;
}

static void get_in_params() {
    assert(!in_fifo.empty());

    in_params.clear();

    while (!in_fifo.empty()) {
        const u16 data = in_fifo.front(); in_fifo.pop();

        in_params.push_back(data >> 0);
        in_params.push_back(data >> 8);
    }
}

static u16 get_out_data() {
    assert(!out_fifo.empty());

    u16 data = out_fifo.front(); out_fifo.pop();

    if (!out_fifo.empty()) {
        data |= out_fifo.front() << 8; out_fifo.pop();
    }

    if (out_fifo.empty()) {
        HW_ATAPI_STATUS.data_request = 0;

        set_reason(true, true);

        assert_interrupt();
    }

    return data;
}

static void write_out_fifo(const u8 data) {
    out_fifo.push(data);
}

static void write_out_fifo(const char* data, const int length) {
    for (int i = 0; i < length; i++) {
        out_fifo.push(data[i]);
    }
}

static u16 scsi_command_inquiry() {
    constexpr u8 INQUIRY_SIZE = 0x60;

    // Some identifying info for UMD drives
    constexpr u8 MULTIMEDIA_DEVICE  = 0x05;
    constexpr u8 IS_REMOVABLE_MEDIA = 0x80;
    constexpr u8 VERSION = 0;
    constexpr u8 RESPONSE_FORMAT = 2;
    constexpr u8 HISUP   = 0x10;
    constexpr u8 NORMACA = 0x20;

    logger->debug("SCSI INQUIRY");

    write_out_fifo(MULTIMEDIA_DEVICE);
    write_out_fifo(IS_REMOVABLE_MEDIA);
    write_out_fifo(VERSION);
    write_out_fifo(NORMACA | HISUP | RESPONSE_FORMAT);
    write_out_fifo(INQUIRY_SIZE - sizeof(u32));
    write_out_fifo(0);
    write_out_fifo(0);
    write_out_fifo(0);

    // Unless I misunderstood the INQUIRY response format, the following
    // is a little nonstandard

    // T10 vendor ID
    write_out_fifo("SCEI    ", 8);
    // Product ID
    write_out_fifo("UMD ROM DRIVE   ", 16);
    // Product revision
    write_out_fifo("    ", 4);
    // Drive serial/vendor-unique
    write_out_fifo("1.150AAug30 ,2005   ", 20);

    for (int i = 56; i < INQUIRY_SIZE; i++) {
        write_out_fifo(0);
    }

    return INQUIRY_SIZE;
}

static void end_scsi_command(const int command) {
    u16 length;

    switch (command) {
        case ScsiCommand::SCSI_COMMAND_INQUIRY:
            length = scsi_command_inquiry();
            break;
        default:
            logger->error("Unimplemented SCSI command {:02X}", command);
            exit(1);
    }

    HW_ATAPI_LBA_MID = length;
    HW_ATAPI_LBA_HI  = length >> 8;

    HW_ATAPI_STATUS.data_request = length != 0;
    HW_ATAPI_STATUS.device_ready = 1;

    set_reason(false, true);

    HW_ATAPI_STATUS.busy = 0;
    
    assert_interrupt();
}

static void start_scsi_command() {
    assert(!HW_ATAPI_STATUS.busy);

    get_in_params();

    const u8 command = in_params[0];

    HW_ATAPI_STATUS.busy = 1;

    scheduler::schedule_event(
        scheduler::EventType::ATAPI,
        end_scsi_command,
        command,
        scheduler::from_microseconds(1000),
        true
    );
}

static void ata_command_packet() {
    logger->debug("ATA PACKET");

    HW_ATAPI_STATUS.data_request = 1;

    set_reason(true, false);
}

static void end_ata_command(const int command) {
    switch (command) {
        case AtaCommand::ATA_COMMAND_PACKET:
            ata_command_packet();
            break;
        default:
            logger->error("Unimplemented ATA command {:02X}", command);
            exit(1);
    }

    HW_ATAPI_STATUS.busy = 0;

    assert_interrupt();
}

static void start_ata_command(const u8 command) {
    assert(!HW_ATAPI_STATUS.busy);

    HW_ATAPI_STATUS.busy = 1;

    scheduler::schedule_event(
        scheduler::EventType::ATAPI,
        end_ata_command,
        command,
        scheduler::from_microseconds(1000),
        true
    );
}

static u32 ahb_read(const u32 addr) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_AHB_REVISION:
            logger->debug("AHB_REVISION read32");
            return AHB_REVISION;
        case IoAddress::IO_ADDRESS_AHB_PIOCTRL:
            logger->debug("AHB_PIOCTRL read32");
            return HW_ATAPI_AHB_PIOCTRL;
        default:
            logger->error("Unmapped AHB read32 @ {:08X}", addr);
            exit(1);
    }
}

static u8 read8(const u32 addr) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_REASON:
            logger->debug("REASON read8");
            return HW_ATAPI_REASON.raw;
        case IoAddress::IO_ADDRESS_LBA_LO:
            logger->debug("LBA_LO read8");
            return HW_ATAPI_LBA_LO;
        case IoAddress::IO_ADDRESS_LBA_MID:
            logger->debug("LBA_MID read8");
            return HW_ATAPI_LBA_MID;
        case IoAddress::IO_ADDRESS_LBA_HI:
            logger->debug("LBA_HI read8");
            return HW_ATAPI_LBA_HI;
        case IoAddress::IO_ADDRESS_DRIVE:
            logger->debug("DRIVE read8");
            return HW_ATAPI_DRIVE;
        case IoAddress::IO_ADDRESS_STATUS:
            logger->debug("STATUS read8");
            // This should clear interrupts
            return HW_ATAPI_STATUS.raw;
        case IoAddress::IO_ADDRESS_ALTSTAT:
            logger->debug("ALTSTAT read8");
            return HW_ATAPI_STATUS.raw;
        default:
            logger->error("Unmapped read8 @ {:08X}", addr);
            exit(1);
    }
}

static u16 read16(const u32 addr) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_DATA:
            logger->debug("DATA read16");
            return get_out_data();
        default:
            logger->error("Unmapped read16 @ {:08X}", addr);
            exit(1);
    }
}

static void ahb_write(const u32 addr, const u32 data) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_AHB_CONTROL:
            logger->debug("AHB_CONTROL write32 = {:08X}", data);
            break;
        case IoAddress::IO_ADDRESS_AHB_RESET:
            logger->debug("AHB_RESET write32 = {:08X}", data);
            break;
        case IoAddress::IO_ADDRESS_AHB_PIODLY:
            logger->debug("AHB_PIODLY write32 = {:08X}", data);
            break;
        case IoAddress::IO_ADDRESS_AHB_MDMADLY:
            logger->debug("AHB_MDMADLY write32 = {:08X}", data);
            break;
        case IoAddress::IO_ADDRESS_AHB_PIOCTRL:
            logger->debug("AHB_PIOCTRL write32 = {:08X}", data);

            HW_ATAPI_AHB_PIOCTRL = data;
            break;
        case IoAddress::IO_ADDRESS_AHB_INTR:
            logger->debug("AHB_INTR write32 = {:08X}", data);

            HW_ATAPI_AHB_INTR = data;
            break;
        case IoAddress::IO_ADDRESS_AHB_DEVSTAT:
            logger->debug("AHB_DEVSTAT write32 = {:08X}", data);

            HW_ATAPI_AHB_DEVSTAT = data;
            break;
        default:
            logger->error("Unmapped AHB write32 @ {:08X} = {:08X}", addr, data);
            exit(1);
    }
}

static void write8(const u32 addr, const u8 data) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_FEATURES:
            logger->debug("FEATURES write8 = {:02X}", data);

            HW_ATAPI_FEATURES = data;
            break;
        case IoAddress::IO_ADDRESS_REASON:
            logger->debug("REASON write8 = {:02X}", data);

            // This probably clears the current interrupt bits?
            HW_ATAPI_REASON.raw &= data;
            break;
        case IoAddress::IO_ADDRESS_LBA_LO:
            logger->debug("LBA_LO write8 = {:02X}", data);

            HW_ATAPI_LBA_LO = data;
            break;
        case IoAddress::IO_ADDRESS_LBA_MID:
            logger->debug("LBA_MID write8 = {:02X}", data);

            HW_ATAPI_LBA_MID = data;
            break;
        case IoAddress::IO_ADDRESS_LBA_HI:
            logger->debug("LBA_HI write8 = {:02X}", data);

            HW_ATAPI_LBA_HI = data;
            break;
        case IoAddress::IO_ADDRESS_DRIVE:
            logger->debug("DRIVE write8 = {:02X}", data);

            HW_ATAPI_DRIVE = data;
            break;
        case IoAddress::IO_ADDRESS_COMMAND:
            logger->debug("COMMAND write8 = {:02X}", data);
            start_ata_command(data);
            break;
        case IoAddress::IO_ADDRESS_PKTEND:
            logger->debug("PKTEND write8 = {:02X}", data);
            start_scsi_command();
            break;
        case IoAddress::IO_ADDRESS_DEVCTRL:
            logger->debug("DEVCTRL write8 = {:02X}", data);

            HW_ATAPI_DEVCTRL.raw = data;

            if (HW_ATAPI_DEVCTRL.software_reset) {
                logger->error("Unimplemented soft reset");
                exit(1);
            }
            break;
        default:
            logger->error("Unmapped write8 @ {:08X} = {:02X}", addr, data);
            exit(1);
    }
}

static void write16(const u32 addr, const u16 data) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_DATA:
            logger->debug("DATA write16 = {:04X}", data);
            in_fifo.push(data);
            break;
        default:
            logger->error("Unmapped write16 @ {:08X} = {:04X}", addr, data);
            exit(1);
    }
}

void initialize() {
    logger = spdlog::stdout_color_st("ATAPI");

    std::memset(&ctx, 0, sizeof(ctx));
}

void soft_reset() {
    // Properly reset all status bits
    HW_ATAPI_STATUS.raw = 0;
    HW_ATAPI_STATUS.device_ready = 1;

    // Set packet device signature
    HW_ATAPI_REASON.raw = 1;
    HW_ATAPI_LBA = 0xEB1401;
}

void hard_reset() {
    const bus::PageDescriptor page_desc {
        .read8_func   = read8,
        .read16_func  = read16,
        .write8_func  = write8,
        .write16_func = write16,
    };

    const bus::PageDescriptor ahb_page_desc {
        .read32_func  = ahb_read,
        .write32_func = ahb_write,
    };

    soft_reset();

    kanacore::get_sc_bus_ptr()->map(ATAPI_ADDR, ATAPI_SIZE, page_desc);
    kanacore::get_sc_bus_ptr()->map(ATAPI_AHB_ADDR, ATAPI_SIZE, ahb_page_desc);
}

void shutdown() {

}

};
