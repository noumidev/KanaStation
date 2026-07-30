/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/i2c/i2c.cpp - I2C controller */

#include <core/hw/i2c/i2c.hpp>

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <queue>
#include <vector>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <core/kanacore.hpp>
#include <core/scheduler.hpp>
#include <core/hw/bus.hpp>
#include <core/hw/intc.hpp>
#include <core/hw/i2c/clockgen.hpp>
#include <core/hw/i2c/codec.hpp>

namespace kanacore::hw::i2c {

using namespace common;

constexpr u64 I2C_ADDR = 0x1E200000;
constexpr u64 I2C_SIZE = 0x1000;

constexpr u64 FIFO_SIZE = 256;

constexpr int I2C_INTERRUPT = 12;

enum IoAddress {
    IO_ADDRESS_CONTROL  = I2C_ADDR + 0x004,
    IO_ADDRESS_LENGTH   = I2C_ADDR + 0x008,
    IO_ADDRESS_DATA     = I2C_ADDR + 0x00C,
    IO_ADDRESS_INTRSTAT = I2C_ADDR + 0x028,
};

#define HW_I2C_CONTROL  ctx.control
#define HW_I2C_LENGTH   ctx.length
#define HW_I2C_INTRSTAT ctx.interrupt_status

enum TransferDirection {
    TRANSFER_DIRECTION_FROM_DEVICE = 1,
};

enum DeviceAddress {
    DEVICE_ADDRESS_CODEC    = 0x1A,
    DEVICE_ADDRESS_CLOCKGEN = 0x69,
};

static struct {
    union {
        u8 raw;

        struct {
            u32           : 3;
            u32 direction : 1; // 1 = read
            u32           : 3;
            u32 busy      : 1;
        };
    } control;

    u32 length;
    u32 interrupt_status;

    std::vector<u8> (*transmit)(const u8);
    void (*receive)(const std::vector<u8>&);
} ctx;

static std::shared_ptr<spdlog::logger> logger;

static std::queue<u8> transmit_fifo;
static std::queue<u8> receive_fifo;

static void end_transmission_reception(const int) {
    HW_I2C_CONTROL.busy = false;

    HW_I2C_INTRSTAT |= 1;

    intc::assert_sc_interrupt(I2C_INTERRUPT);
}

static void push_in_params(const std::vector<u8> data) {
    for (u64 i = 0; i < data.size(); i++) {
        receive_fifo.push(data[i]);
    }
}

static std::vector<u8> get_out_params() {
    std::vector<u8> out_params;

    while (!transmit_fifo.empty()) {
        out_params.push_back(transmit_fifo.front());
        transmit_fifo.pop();
    }

    return out_params;
}

static void bind_device(const u8 device_addr) {
    switch (device_addr) {
        case DeviceAddress::DEVICE_ADDRESS_CODEC:
            ctx.transmit = codec::transmit;
            ctx.receive  = codec::receive;
            break;
        case DeviceAddress::DEVICE_ADDRESS_CLOCKGEN:
            ctx.transmit = clockgen::transmit;
            ctx.receive  = clockgen::receive;
            break;
        default:
            logger->error("Unimplemented device (address: {:02X})", device_addr);
            exit(1);
    }
}

static void start_transmission() {
    assert(!transmit_fifo.empty());

    // TX sets the device address? That would explain TX with length = 0,
    // that only sets the address...
    const u8 device_addr = transmit_fifo.front() >> 1; transmit_fifo.pop();

    logger->debug("Device address: {:02X}", device_addr);

    bind_device(device_addr);

    if (HW_I2C_LENGTH > 0) {
        logger->debug("Starting transmission");
        
        ctx.receive(get_out_params());
    }
    
    scheduler::schedule_event(
        scheduler::EventType::I2C,
        end_transmission_reception,
        0,
        scheduler::from_microseconds(50 * HW_I2C_LENGTH)
    );
}

static void start_reception() {
    assert(ctx.transmit != nullptr);

    logger->debug("Starting reception");

    push_in_params(ctx.transmit(HW_I2C_LENGTH));

    scheduler::schedule_event(
        scheduler::EventType::I2C,
        end_transmission_reception,
        0,
        scheduler::from_microseconds(50 * HW_I2C_LENGTH)
    );
}

static u8 read_receive_fifo() {
    if (receive_fifo.empty()) {
        logger->warn("Receive FIFO is empty");
        exit(1);
    }

    const u8 data = receive_fifo.front(); receive_fifo.pop();

    return data;
}

static u32 read(const u32 addr) {
    // Note: it appears that every I2C I/O register has an "update ongoing" flag
    // in bit 15 that goes high upon write and low when the value in the register
    // is updated
    switch (addr) {
        case IoAddress::IO_ADDRESS_CONTROL:
            logger->debug("CONTROL read32", addr);
            return 0;
        case IoAddress::IO_ADDRESS_LENGTH:
            logger->debug("LENGTH read32", addr);
            return HW_I2C_LENGTH;
        case IoAddress::IO_ADDRESS_DATA:
            logger->debug("DATA read32", addr);
            return read_receive_fifo();
        case IoAddress::IO_ADDRESS_INTRSTAT:
            logger->debug("INTRSTAT read32", addr);
            return HW_I2C_INTRSTAT;
        case I2C_ADDR + 0x000:
            // This appears to be an error/status register, not sure
            // what the individual bits mean
        case I2C_ADDR + 0x010:
        case I2C_ADDR + 0x014:
        case I2C_ADDR + 0x01C:
            logger->warn("Unmapped read32 @ {:08X}", addr);
            return 0;
        default:
            logger->error("Unmapped read32 @ {:08X}", addr);
            exit(1);
    }
}

static void write_transmit_fifo(const u8 data) {
    assert(transmit_fifo.size() < FIFO_SIZE);

    transmit_fifo.push(data);
}

static void write(const u32 addr, const u32 data) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_CONTROL:
            assert(!HW_I2C_CONTROL.busy);

            // Bit 3 indicates the direction of the transfer(?).
            // Bit 2 is write-only, bit 1 appears to go low after the transfer.
            logger->debug("CONTROL write32 = {:08X}", data);

            HW_I2C_CONTROL.raw = data;

            if (HW_I2C_CONTROL.busy) {
                if (HW_I2C_CONTROL.direction == TransferDirection::TRANSFER_DIRECTION_FROM_DEVICE) {
                    start_reception();
                } else {
                    start_transmission();
                }
            }
            break;
        case IoAddress::IO_ADDRESS_LENGTH:
            // Accepted values are 0-255, this actually stands for (length + 1)
            logger->debug("LENGTH write32 = {:08X}", data);

            HW_I2C_LENGTH = data & (FIFO_SIZE - 1);
            break;
        case IoAddress::IO_ADDRESS_DATA:
            logger->debug("DATA write32 = {:08X}", data);

            write_transmit_fifo(data);
            break;
        case IoAddress::IO_ADDRESS_INTRSTAT:
            logger->debug("INTRSTAT write32 = {:08X}", data);

            HW_I2C_INTRSTAT &= ~data;

            intc::clear_sc_interrupt(I2C_INTERRUPT);
            break;
        case I2C_ADDR + 0x010:
        case I2C_ADDR + 0x014:
            // These two registers are related to the I2C transfer speed.
            // They accept values from 0-255, although a value of 0 in any of the
            // registers locks up the transfer.
            // From what little I've tested, the setting (1, 1) is the fastest (~1.4 Mb/s),
            // the standard setting (4, 4) quite a bit slower (~150 kb/s).
            // A big jump in speed happens at setting (3, 1)
        case I2C_ADDR + 0x01C:
        case I2C_ADDR + 0x02C:
            // The IPL I2C driver writes 1 to these registers before changing
            // the baud rate, but it looks like this isn't *strictly* needed
            // as far as the baud rate registers are concerned
            logger->warn("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            break;
        default:
            logger->error("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            exit(1);
    }
}

void initialize() {
    logger = spdlog::stdout_color_st("I2C");

    std::memset(&ctx, 0, sizeof(ctx));

    clockgen::initialize();
    codec::initialize();
}

void soft_reset() {
    clockgen::soft_reset();
    codec::soft_reset();
}

void hard_reset() {
    const bus::PageDescriptor page_desc {
        // To my knowledge, I2C I/O is never not read/written using 32-bit accesses
        .read32_func  = read,
        .write32_func = write,
    };

    kanacore::get_sc_bus_ptr()->map(I2C_ADDR, I2C_SIZE, page_desc);

    clockgen::hard_reset();
    codec::hard_reset();
}

void shutdown() {
    clockgen::shutdown();
    codec::shutdown();
}

};
