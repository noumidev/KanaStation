/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/spi.cpp - ARM PrimeCell PL022 SPI controller for SysCon */

#include <core/hw/spi.hpp>

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <queue>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <common/types.hpp>
#include <core/hw/bus.hpp>

namespace kanacore::hw::spi {

using namespace common;

constexpr u64 SPI_ADDR = 0x1E580000;
constexpr u64 SPI_SIZE = 0x1000;

constexpr u64 FIFO_SIZE = 8;

enum IoAddress {
    IO_ADDRESS_CONTROL0 = SPI_ADDR + 0x000,
    IO_ADDRESS_CONTROL1 = SPI_ADDR + 0x004,
    IO_ADDRESS_DATA     = SPI_ADDR + 0x008,
    IO_ADDRESS_STATUS   = SPI_ADDR + 0x00C,
    IO_ADDRESS_INTRMASK = SPI_ADDR + 0x014,
    IO_ADDRESS_INTRCLR  = SPI_ADDR + 0x020,
    IO_ADDRESS_DMACTRL  = SPI_ADDR + 0x024,
};

#define HW_SPI_CONTROL  ctx.control
#define HW_SPI_CONTROL0 ctx.control.raw[0]
#define HW_SPI_CONTROL1 ctx.control.raw[1]
#define HW_SPI_STATUS   ctx.status
#define HW_SPI_INTRMASK ctx.interrupt_mask
#define HW_SPI_DMACTRL  ctx.dma_control

enum FrameFormat {
    FRAME_FORMAT_MOTOROLA,
};

static struct {
    union {
        u16 raw[2];

        struct {
            u32 data_size            : 4;
            u32 frame_format         : 2;
            u32 clock_out_phase      : 1;
            u32 clock_out_polarity   : 1;
            u32 clock_rate           : 8;
            u32 loop_back_mode       : 1;
            u32 serial_enable        : 1;
            u32 slave_mode           : 1;
            u32 slave_output_disable : 1;
            u32                      : 12;
        };
    } control;

    union {
        u16 raw;

        struct {
            u16 transmit_empty    : 1;
            u16 transmit_not_full : 1;
            u16 receive_not_empty : 1;
            u16 receive_full      : 1;
            u16 busy              : 1;
            u16                   : 11;
        };
    } status;

    u16 interrupt_mask;

    union {
        u16 raw;

        struct {
            u16 receive_enable  : 1;
            u16 transmit_enable : 1;
            u16                 : 14;
        };
    } dma_control;
} ctx;

static std::shared_ptr<spdlog::logger> logger;

static std::queue<u16> transmit_fifo;
static std::queue<u16> receive_fifo;

static inline void update_fifo_status() {
    HW_SPI_STATUS.transmit_empty    = transmit_fifo.size() == 0;
    HW_SPI_STATUS.transmit_not_full = transmit_fifo.size() != FIFO_SIZE;

    HW_SPI_STATUS.receive_not_empty = receive_fifo.size() != 0;
    HW_SPI_STATUS.receive_full      = receive_fifo.size() == FIFO_SIZE;
}

static u32 read(const u32 addr) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_STATUS:
            logger->debug("STATUS read32");
            return HW_SPI_STATUS.raw;
        default:
            logger->error("Unmapped read32 @ {:08X}", addr);
            exit(1);
    }
}

static void write_transmit_fifo(const u16 data) {
    assert(transmit_fifo.size() < FIFO_SIZE);

    transmit_fifo.push(data);

    update_fifo_status();
}

static void write(const u32 addr, const u32 data) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_CONTROL0:
            logger->debug("CONTROL0 write32 = {:08X}", data);

            HW_SPI_CONTROL0 = data;

            assert(HW_SPI_CONTROL.data_size == 15);
            assert(HW_SPI_CONTROL.frame_format == FrameFormat::FRAME_FORMAT_MOTOROLA);
            break;
        case IoAddress::IO_ADDRESS_CONTROL1:
            logger->debug("CONTROL1 write32 = {:08X}", data);

            HW_SPI_CONTROL1 = data;

            assert(!HW_SPI_CONTROL.loop_back_mode);
            assert(HW_SPI_CONTROL.slave_mode);
            assert(!HW_SPI_CONTROL.slave_output_disable);
            break;
        case IoAddress::IO_ADDRESS_DATA:
            logger->debug("DATA write32 = {:08X}", data);
            write_transmit_fifo(data);
            break;
        case IoAddress::IO_ADDRESS_INTRMASK:
            logger->debug("INTRMASK write32 = {:08X}", data);

            HW_SPI_INTRMASK = data;
            break;
        case IoAddress::IO_ADDRESS_INTRCLR:
            logger->debug("INTRCLR write32 = {:08X}", data);
            
            // This should clear the receive overrun and timeout interrupts
            break;
        case IoAddress::IO_ADDRESS_DMACTRL:
            logger->debug("DMACTRL write32 = {:08X}", data);

            HW_SPI_DMACTRL.raw = data;

            assert(!HW_SPI_DMACTRL.receive_enable);
            assert(!HW_SPI_DMACTRL.transmit_enable);
            break;
        default:
            logger->error("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            exit(1);
    }
}

void initialize() {
    logger = spdlog::stdout_color_st("SPI");

    std::memset(&ctx, 0, sizeof(ctx));
}

void soft_reset() {
    
}

void hard_reset() {
    const bus::PageDescriptor page_desc {
        // To my knowledge, SPI I/O is never not read/written using 32-bit accesses.
        // This is funny because SPI registers are 16-bit wide
        .read32_func  = read,
        .write32_func = write,
    };

    bus::map(SPI_ADDR, SPI_SIZE, page_desc);

    update_fifo_status();
}

void shutdown() {

}

};
