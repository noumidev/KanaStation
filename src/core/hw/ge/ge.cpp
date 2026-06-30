/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/ge/ge.cpp - GraphicsEngine interface */

#include <core/hw/ge/ge.hpp>

#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <core/kanacore.hpp>
#include <core/hw/bus.hpp>
#include <core/hw/intc.hpp>

namespace kanacore::hw::ge {

using namespace common;

constexpr u64 GE_ADDR = 0x1D400000;
constexpr u64 GE_SIZE = 0x1000;

constexpr int GE_INTERRUPT = 25;

constexpr u64 NUM_COMMANDS = 0x100;

enum IoAddress {
    IO_ADDRESS_HWSIZE    = GE_ADDR + 0x008,
    IO_ADDRESS_LISTSTAT  = GE_ADDR + 0x100,
    IO_ADDRESS_LISTADDR  = GE_ADDR + 0x108,
    IO_ADDRESS_STALLADDR = GE_ADDR + 0x10C,
    IO_ADDRESS_LINKADDR0 = GE_ADDR + 0x110,
    IO_ADDRESS_LINKADDR1 = GE_ADDR + 0x114,
    IO_ADDRESS_VTXADDR   = GE_ADDR + 0x118,
    IO_ADDRESS_IDXADDR   = GE_ADDR + 0x11C,
    IO_ADDRESS_ORGADDR0  = GE_ADDR + 0x120,
    IO_ADDRESS_ORGADDR1  = GE_ADDR + 0x124,
    IO_ADDRESS_ORGADDR2  = GE_ADDR + 0x128,
    IO_ADDRESS_CMDSTAT   = GE_ADDR + 0x304,
    IO_ADDRESS_INTRSTAT  = GE_ADDR + 0x308,
    IO_ADDRESS_INTRSWAP  = GE_ADDR + 0x30C,
    IO_ADDRESS_CMDSWAP   = GE_ADDR + 0x310,
    IO_ADDRESS_EDRAMSIZE = GE_ADDR + 0x400,
    IO_ADDRESS_COMMAND   = GE_ADDR + 0x800,
    IO_ADDRESS_BONEMTX   = GE_ADDR + 0xC00,
};

#define HW_GE_LISTSTAT  ctx.list_status
#define HW_GE_LISTADDR  ctx.list_addr
#define HW_GE_STALLADDR ctx.stall_addr
#define HW_GE_LINKADDR0 ctx.link_addrs[0]
#define HW_GE_LINKADDR1 ctx.link_addrs[1]
#define HW_GE_VTXADDR   ctx.vertex_addr
#define HW_GE_IDXADDR   ctx.index_addr
#define HW_GE_ORGADDR0  ctx.origin_addrs[0]
#define HW_GE_ORGADDR1  ctx.origin_addrs[1]
#define HW_GE_ORGADDR2  ctx.origin_addrs[2]
#define HW_GE_CMDSTAT   ctx.command_status
#define HW_GE_INTRSTAT  ctx.interrupt_status
#define HW_GE_EDRAMSIZE ctx.edram_size

union ListCommand {
    u32 raw;
    
    struct {
        u32 param   : 24;
        u32 command : 8;
    };
};

std::array<ListCommand, NUM_COMMANDS> commands;

static struct {
    union {
        u32 raw;

        struct {
            u32 busy      : 1;
            u32 condition : 1;
            u32           : 6;
            u32 depth     : 2;
            u32           : 22;
        };
    } list_status;

    u32 list_addr;
    u32 stall_addr;
    u32 link_addrs[2];
    u32 vertex_addr;
    u32 index_addr;
    u32 origin_addrs[3];
    u32 command_status;
    u32 interrupt_status;
    u32 edram_size;
} ctx;

static std::shared_ptr<spdlog::logger> logger;

static void check_pending_interrupts() {
    if (HW_GE_INTRSTAT != 0) {
        intc::assert_sc_interrupt(GE_INTERRUPT);
    } else {
        intc::clear_sc_interrupt(GE_INTERRUPT);
    }
}

static void assert_interrupt(const int intr_num) {
    HW_GE_INTRSTAT |= 1 << intr_num;
    HW_GE_CMDSTAT  |= 1 << intr_num;

    check_pending_interrupts();
}

static void start_list_exec() {
    bus::Bus* bus = kanacore::get_sc_bus_ptr();

    if (!HW_GE_LISTSTAT.busy) {
        logger->debug("List processing disabled");
        return;
    }

    while ((HW_GE_STALLADDR == 0) || (HW_GE_LISTADDR < HW_GE_STALLADDR)) {
        const ListCommand list_command = { .raw = bus->read<u32>(HW_GE_LISTADDR) };

        // Update command array
        commands[list_command.command] = list_command;

        HW_GE_LISTADDR += sizeof(list_command);

        logger->error("Unimplemented command {:02X} ({:08X})", list_command.command, list_command.raw);

        switch (list_command.command) {
            case 0x08:
                // JUMP
                HW_GE_LISTADDR = ((commands[0x10].param & 0xFF0000) << 8) | list_command.param;
                break;
            case 0x09:
                // BJUMP
                exit(1);
            case 0x0C:
                // END
                HW_GE_LISTSTAT.busy = false;

                assert_interrupt(1);
                return;
            case 0x0F:
                // FINISH
                assert_interrupt(2);
                break;
        }
    }
}

static u32 read(const u32 addr) {
    constexpr u32 HWSIZE = 0x200000 >> 10;

    if ((addr >= IoAddress::IO_ADDRESS_COMMAND) && (addr < IoAddress::IO_ADDRESS_BONEMTX)) {
        const u32 idx = (addr - IoAddress::IO_ADDRESS_COMMAND) / sizeof(u32);

        logger->debug("COMMAND{:02X} read32", idx);
        return commands[idx].raw;
    }

    switch (addr) {
        case IoAddress::IO_ADDRESS_HWSIZE:
            logger->debug("HWSIZE read32");
            return HWSIZE;
        case IoAddress::IO_ADDRESS_LISTSTAT:
            logger->debug("LISTSTAT read32");
            return HW_GE_LISTSTAT.raw;
        case IoAddress::IO_ADDRESS_LISTADDR:
            logger->debug("LISTADDR read32");
            return HW_GE_LISTADDR;
        case IoAddress::IO_ADDRESS_STALLADDR:
            logger->debug("STALLADDR read32");
            return HW_GE_STALLADDR;
        case IoAddress::IO_ADDRESS_LINKADDR0:
            logger->debug("LINKADDR0 read32");
            return HW_GE_LINKADDR0;
        case IoAddress::IO_ADDRESS_LINKADDR1:
            logger->debug("LINKADDR1 read32");
            return HW_GE_LINKADDR1;
        case IoAddress::IO_ADDRESS_VTXADDR:
            logger->debug("VTXADDR read32");
            return HW_GE_VTXADDR;
        case IoAddress::IO_ADDRESS_IDXADDR:
            logger->debug("IDXADDR read32");
            return HW_GE_IDXADDR;
        case IoAddress::IO_ADDRESS_ORGADDR0:
            logger->debug("ORGADDR0 read32");
            return HW_GE_ORGADDR0;
        case IoAddress::IO_ADDRESS_ORGADDR1:
            logger->debug("ORGADDR1 read32");
            return HW_GE_ORGADDR1;
        case IoAddress::IO_ADDRESS_ORGADDR2:
            logger->debug("ORGADDR2 read32");
            return HW_GE_ORGADDR2;
        case IoAddress::IO_ADDRESS_CMDSTAT:
            logger->debug("CMDSTAT read32");
            return HW_GE_CMDSTAT;
        case IoAddress::IO_ADDRESS_INTRSTAT:
            logger->debug("INTRSTAT read32");
            return HW_GE_INTRSTAT;
        case IoAddress::IO_ADDRESS_EDRAMSIZE:
            logger->debug("EDRAMSIZE read32");
            return HW_GE_EDRAMSIZE;
        case GE_ADDR + 0x004:
            logger->warn("Unmapped read32 @ {:08X}", addr);
            return 0;
        default:
            logger->warn("Unmapped read32 @ {:08X}", addr);
            exit(1);
    }
}

static void write(const u32 addr, const u32 data) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_LISTSTAT:
            logger->debug("LISTSTAT write32 = {:08X}", data);

            HW_GE_LISTSTAT.raw = data;

            start_list_exec();
            break;
        case IoAddress::IO_ADDRESS_LISTADDR:
            logger->debug("LISTADDR write32 = {:08X}", data);

            HW_GE_LISTADDR = data;

            start_list_exec();
            break;
        case IoAddress::IO_ADDRESS_STALLADDR:
            logger->debug("STALLADDR write32 = {:08X}", data);

            HW_GE_STALLADDR = data;

            start_list_exec();
            break;
        case IoAddress::IO_ADDRESS_LINKADDR0:
            logger->debug("LINKADDR0 write32 = {:08X}", data);

            HW_GE_LINKADDR0 = data;
            break;
        case IoAddress::IO_ADDRESS_LINKADDR1:
            logger->debug("LINKADDR1 write32 = {:08X}", data);

            HW_GE_LINKADDR1 = data;
            break;
        case IoAddress::IO_ADDRESS_VTXADDR:
            logger->debug("VTXADDR write32 = {:08X}", data);

            HW_GE_VTXADDR = data;
            break;
        case IoAddress::IO_ADDRESS_IDXADDR:
            logger->debug("IDXADDR write32 = {:08X}", data);

            HW_GE_IDXADDR = data;
            break;
        case IoAddress::IO_ADDRESS_ORGADDR0:
            logger->debug("ORGADDR0 write32 = {:08X}", data);

            HW_GE_ORGADDR0 = data;
            break;
        case IoAddress::IO_ADDRESS_ORGADDR1:
            logger->debug("ORGADDR1 write32 = {:08X}", data);

            HW_GE_ORGADDR1 = data;
            break;
        case IoAddress::IO_ADDRESS_ORGADDR2:
            logger->debug("ORGADDR2 write32 = {:08X}", data);

            HW_GE_ORGADDR2 = data;
            break;
        case IoAddress::IO_ADDRESS_INTRSTAT:
            logger->debug("INTRSTAT write32 = {:08X}", data);

            HW_GE_INTRSTAT &= ~data;
            break;
        case IoAddress::IO_ADDRESS_INTRSWAP:
            logger->debug("INTRSWAP write32 = {:08X}", data);

            HW_GE_INTRSTAT ^= data;
            break;
        case IoAddress::IO_ADDRESS_CMDSWAP:
            logger->debug("CMDSWAP write32 = {:08X}", data);

            HW_GE_CMDSTAT  ^= data;
            HW_GE_INTRSTAT  = HW_GE_CMDSTAT;
            break;
        case IoAddress::IO_ADDRESS_EDRAMSIZE:
            logger->debug("EDRAMSIZE write32 = {:08X}", data);

            HW_GE_EDRAMSIZE = data;
            break;
        default:
            logger->warn("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            exit(1);
    }

    check_pending_interrupts();
}

void initialize() {
    logger = spdlog::stdout_color_st("GE");

    std::memset(&ctx, 0, sizeof(ctx));
}

void soft_reset() {
    
}

void hard_reset() {
    const bus::PageDescriptor page_desc {
        // To my knowledge, GE I/F I/O is never not read/written using 32-bit accesses
        .read32_func  = read,
        .write32_func = write,
    };

    kanacore::get_sc_bus_ptr()->map(GE_ADDR, GE_SIZE, page_desc);
}

void shutdown() {

}

};
