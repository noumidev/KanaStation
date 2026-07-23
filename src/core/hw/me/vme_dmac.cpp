/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/me/vme_dmac.cpp - Virtual Mobile Engine DMA controller */

#include <core/hw/me/vme_dmac.hpp>

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <common/types.hpp>
#include <core/kanacore.hpp>
#include <core/scheduler.hpp>
#include <core/hw/bus.hpp>
#include <core/hw/intc.hpp>

namespace kanacore::hw::me::vme_dmac {

using namespace common;

constexpr u64 VMEDMAC_ADDR = 0x040FF000;
constexpr u64 VMEDMAC_SIZE = 0x1000;

constexpr int VMEDMAC_INTERRUPT = 5;

/* 
 * Big thanks to m-c/d for their ME/VME reverse engineering work!
 * https://github.com/mcidclan/psp-media-engine-custom-core/blob/main/vme-lib.c
 */

enum IoAddress {
    IO_ADDRESS_STATUS        = VMEDMAC_ADDR + 0x000,
    IO_ADDRESS_CONFIG        = VMEDMAC_ADDR + 0x004,
    IO_ADDRESS_CONTROL       = VMEDMAC_ADDR + 0x008,
    IO_ADDRESS_AGUOUT_ADDR   = VMEDMAC_ADDR + 0x010,
    IO_ADDRESS_AGUOUT_LENGTH = VMEDMAC_ADDR + 0x014,
    IO_ADDRESS_AGUOUT_OFFSET = VMEDMAC_ADDR + 0x018,
};

#define HW_VMEDMAC_STATUS        ctx.status
#define HW_VMEDMAC_CONFIG        ctx.config
#define HW_VMEDMAC_CONTROL       ctx.control
#define HW_VMEDMAC_AGUOUT_ADDR   ctx.outer_agu.addr
#define HW_VMEDMAC_AGUOUT_LENGTH ctx.outer_agu.length
#define HW_VMEDMAC_AGUOUT_OFFSET ctx.outer_agu.offset

enum VmeDmacCommand {
    VMEDMAC_COMMAND_UPLOAD_BITSTREAM   = 0x1D,
    VMEDMAC_COMMAND_CLEAR_LOCAL_BUFFER = 0x21,
};

static std::shared_ptr<spdlog::logger> logger;

static struct {
    // TODO: figure out the exact layout of these registers
    u32 status;
    u32 config;
    u32 control;

    struct {
        u32 addr;
        u32 length;
        u32 offset;
    } outer_agu;
} ctx;

static void end_command(const int) {
    HW_VMEDMAC_STATUS &= ~0x800;

    intc::assert_me_interrupt(VMEDMAC_INTERRUPT);
}

static void end_transfer(const int vme_busy) {
    HW_VMEDMAC_STATUS &= ~0x200;

    intc::assert_me_interrupt(VMEDMAC_INTERRUPT);

    if (vme_busy != 0) {
        HW_VMEDMAC_STATUS |= 0x800;

        scheduler::schedule_event(scheduler::EventType::VME_DMA, end_command, vme_busy, scheduler::from_microseconds(5));
    }
}

static void start_command() {
    // It's probably not accurate to call this a command, but
    // until we know what all the bits mean, I will treat it as such
    const u32 command = HW_VMEDMAC_CONTROL;

    switch (command) {
        case VmeDmacCommand::VMEDMAC_COMMAND_UPLOAD_BITSTREAM:
            logger->debug("UPLOAD_BITSTREAM");
            break;
        case VmeDmacCommand::VMEDMAC_COMMAND_CLEAR_LOCAL_BUFFER:
            logger->debug("CLEAR_LOCAL_BUFFER (offset: {:04X}, length: {:04X})", HW_VMEDMAC_AGUOUT_OFFSET, HW_VMEDMAC_AGUOUT_LENGTH + 1);
            break;
        default:
            logger->error("Unimplemented command {:02X}", command);
            exit(1);
    }

    // There are commands that set VME_BUSY first I believe, but for now this should be fine
    HW_VMEDMAC_STATUS |= 0x200;

    const bool vme_busy = command == VmeDmacCommand::VMEDMAC_COMMAND_UPLOAD_BITSTREAM;

    scheduler::schedule_event(scheduler::EventType::VME_DMA, end_transfer, vme_busy, scheduler::from_microseconds(5));
}

static u32 read(const u32 addr) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_STATUS:
            logger->debug("STATUS read32");

            // Probably clears the interrupt after all
            intc::clear_me_interrupt(VMEDMAC_INTERRUPT);

            // The ME firmware will poll bits 9 and/or 11 after commands.
            // TODO: figure out what these bits mean
            return HW_VMEDMAC_STATUS;
        default:
            logger->error("Unmapped read32 @ {:08X}", addr);
            exit(1);
    }
}

static void write(const u32 addr, const u32 data) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_STATUS:
            logger->debug("STATUS write32 = {:08X}", data);

            HW_VMEDMAC_STATUS = data;
            break;
        case IoAddress::IO_ADDRESS_CONFIG:
            logger->debug("CONFIG write32 = {:08X}", data);

            HW_VMEDMAC_CONFIG = data;
            break;
        case IoAddress::IO_ADDRESS_CONTROL:
            logger->debug("CONTROL write32 = {:08X}", data);

            HW_VMEDMAC_CONTROL = data;

            start_command();
            break;
        case IoAddress::IO_ADDRESS_AGUOUT_ADDR:
            logger->debug("AGUOUT_ADDR write32 = {:08X}", data);

            HW_VMEDMAC_AGUOUT_ADDR = data;
            break;
        case IoAddress::IO_ADDRESS_AGUOUT_LENGTH:
            logger->debug("AGUOUT_LENGTH write32 = {:08X}", data);

            HW_VMEDMAC_AGUOUT_LENGTH = data;
            break;
        case IoAddress::IO_ADDRESS_AGUOUT_OFFSET:
            logger->debug("AGUOUT_OFFSET write32 = {:08X}", data);

            HW_VMEDMAC_AGUOUT_OFFSET = data;
            break;
        default:
            logger->error("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            exit(1);
    }
}

void initialize() {
    logger = spdlog::stdout_color_st("VME DMAC");

    std::memset(&ctx, 0, sizeof(ctx));
}

void soft_reset() {

}

void hard_reset() {
    const bus::PageDescriptor page_desc {
        .read32_func  = read,
        .write32_func = write,
    };

    kanacore::get_me_bus_ptr()->map(VMEDMAC_ADDR, VMEDMAC_SIZE, page_desc);
}

void shutdown() {

}

};
