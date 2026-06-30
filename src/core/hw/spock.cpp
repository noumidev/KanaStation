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
};

#define HW_SPOCK_RESET    ctx.reset
#define HW_SPOCK_COMMAND  ctx.command
#define HW_SPOCK_STATUS   ctx.status
#define HW_SPOCK_INTRSTAT ctx.interrupt_status
#define HW_SPOCK_RESULT   ctx.result
#define HW_SPOCK_TADDR    ctx.transfer_addr
#define HW_SPOCK_TLENGTH  ctx.transfer_length

enum SpockCommand {
    SPOCK_COMMAND_LEPTON_CHALLENGE = 0x01,
    SPOCK_COMMAND_AUTHENTICATE     = 0x02,
    SPOCK_COMMAND_RESET            = 0x0B,
};

enum SpockError {
    SPOCK_ERROR_OK,
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
} ctx;

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

    assert_interrupt(0);
}

static i32 command_lepton_challenge() {
    logger->debug("LEPTON_CHALLENGE");

    // umdman waits for this pin to go high after sending the LEPTON_CHALLENGE command
    gpio::set_pin(gpio::PIN_LEPTON_ALIVE);

    return SPOCK_ERROR_OK;
}

static i32 command_authenticate() {
    logger->debug("AUTHENTICATE");
    return SPOCK_ERROR_OK;
}

static i32 command_reset() {
    logger->debug("RESET");
    return SPOCK_ERROR_OK;
}

static void start_command() {
    assert(!HW_SPOCK_STATUS.busy);
    assert(HW_SPOCK_COMMAND.flag == 1);

    i32 result;

    const u8 command = HW_SPOCK_COMMAND.command;

    switch (command) {
        case SpockCommand::SPOCK_COMMAND_LEPTON_CHALLENGE:
            result = command_lepton_challenge();
            break;
        case SpockCommand::SPOCK_COMMAND_AUTHENTICATE:
            result = command_authenticate();
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
        scheduler::from_microseconds(5),
        true
    );

    HW_SPOCK_STATUS.busy = 1;
}

static u32 read(const u32 addr) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_RESET:
            logger->debug("RESET read32");
            return HW_SPOCK_RESET;
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
        case SPOCK_ADDR + 0x038:
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
