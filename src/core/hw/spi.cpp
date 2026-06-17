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

#include <core/scheduler.hpp>
#include <core/hw/bus.hpp>
#include <core/hw/syscon.hpp>

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
    IO_ADDRESS_RAWISTAT = SPI_ADDR + 0x018,
    IO_ADDRESS_INTRSTAT = SPI_ADDR + 0x01C,
    IO_ADDRESS_INTRCLR  = SPI_ADDR + 0x020,
    IO_ADDRESS_DMACTRL  = SPI_ADDR + 0x024,
};

#define HW_SPI_CONTROL  ctx.control
#define HW_SPI_CONTROL0 ctx.control.raw[0]
#define HW_SPI_CONTROL1 ctx.control.raw[1]
#define HW_SPI_STATUS   ctx.status
#define HW_SPI_INTRMASK ctx.interrupt_mask
#define HW_SPI_RAWISTAT ctx.raw_interrupt_status
#define HW_SPI_INTRSTAT ctx.interrupt_status
#define HW_SPI_DMACTRL  ctx.dma_control

enum FrameFormat {
    FRAME_FORMAT_MOTOROLA,
};

enum SpiInterrupt {
    SPI_INTERRUPT_RX_HALF_FULL  = 2,
    SPI_INTERRUPT_TX_HALF_EMPTY = 3,
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
    u16 raw_interrupt_status;
    u16 interrupt_status;

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
    HW_SPI_STATUS.transmit_empty    = transmit_fifo.empty();
    HW_SPI_STATUS.transmit_not_full = transmit_fifo.size() != FIFO_SIZE;

    HW_SPI_STATUS.receive_not_empty = !receive_fifo.empty();
    HW_SPI_STATUS.receive_full      = receive_fifo.size() == FIFO_SIZE;

    if (transmit_fifo.size() <= (FIFO_SIZE / 2)) {
        // TX half empty interrupt
        HW_SPI_RAWISTAT |= 1 << SpiInterrupt::SPI_INTERRUPT_TX_HALF_EMPTY;
    } else {
        HW_SPI_RAWISTAT &= ~(1 << SpiInterrupt::SPI_INTERRUPT_TX_HALF_EMPTY);
    }

    if (receive_fifo.size() >= (FIFO_SIZE / 2)) {
        // RX half full interrupt
        HW_SPI_RAWISTAT |= 1 << SpiInterrupt::SPI_INTERRUPT_RX_HALF_FULL;
    } else {
        HW_SPI_RAWISTAT &= ~(1 << SpiInterrupt::SPI_INTERRUPT_RX_HALF_FULL);
    }

    HW_SPI_INTRSTAT = HW_SPI_RAWISTAT & HW_SPI_INTRMASK;
}

static void transmit_data(const int) {
    assert(!transmit_fifo.empty());
    assert(HW_SPI_CONTROL.serial_enable);

    const u16 data = transmit_fifo.front(); transmit_fifo.pop();

    if (!transmit_fifo.empty()) {
        // Repeat transmission
        logger->debug("Continuing transmission");

        scheduler::schedule_event(
            scheduler::EventType::SPI_TX,
            transmit_data,
            0,
            scheduler::SPI_CLOCKRATE
        );
    } else {
        HW_SPI_STATUS.busy = false;
    }

    syscon::receive(data);

    update_fifo_status();
}

static void start_transmission() {
    assert(!transmit_fifo.empty());
    assert(HW_SPI_CONTROL.serial_enable);

    logger->debug("Starting transmission");

    HW_SPI_STATUS.busy = true;

    scheduler::schedule_event(
        scheduler::EventType::SPI_TX,
        transmit_data,
        0,
        scheduler::from_microseconds(2)
    );
}

static u32 read_receive_fifo() {
    assert(!receive_fifo.empty());

    const u32 data = receive_fifo.front(); receive_fifo.pop();

    update_fifo_status();

    return data;
}

static u32 read(const u32 addr) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_DATA:
            logger->debug("DATA read32");
            return read_receive_fifo();
        case IoAddress::IO_ADDRESS_STATUS:
            logger->debug("STATUS read32");
            return HW_SPI_STATUS.raw;
        case IoAddress::IO_ADDRESS_RAWISTAT:
            logger->debug("RAWISTAT read32");
            return HW_SPI_RAWISTAT;
        default:
            logger->error("Unmapped read32 @ {:08X}", addr);
            exit(1);
    }
}

static void write_transmit_fifo(const u16 data) {
    assert(transmit_fifo.size() < FIFO_SIZE);

    transmit_fifo.push(data);

    update_fifo_status();

    if (HW_SPI_CONTROL.serial_enable) {
        start_transmission();
    }
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

            if (HW_SPI_CONTROL.serial_enable && !HW_SPI_STATUS.busy && !transmit_fifo.empty()) {
                start_transmission();
            }
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
            
            // Clear FIFO overrun/timeout interrupts
            // Use named constants for this
            HW_SPI_RAWISTAT &= ~3;
            HW_SPI_INTRSTAT &= ~3;
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

void start_reception() {
    assert(receive_fifo.size() < FIFO_SIZE);
    assert(HW_SPI_CONTROL.serial_enable);
    assert(!HW_SPI_STATUS.busy);

    logger->debug("Starting reception");

    HW_SPI_STATUS.busy = true;
}

void end_reception() {
    assert(HW_SPI_CONTROL.serial_enable);
    assert(HW_SPI_STATUS.busy);

    logger->debug("Ending reception");

    HW_SPI_STATUS.busy = false;

    if (!transmit_fifo.empty()) {
        start_transmission();
    }
}

void receive(const u16 data) {
    assert(receive_fifo.size() < FIFO_SIZE);
    assert(HW_SPI_CONTROL.serial_enable);

    logger->debug("Receiving data {:04X}", data);

    receive_fifo.push(data);

    update_fifo_status();
}

};
