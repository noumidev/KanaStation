/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/uart/uart.cpp - ARM PrimeCell PL011 UART controller */

#include <core/hw/uart/uart.hpp>

#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <common/types.hpp>
#include <core/kanacore.hpp>
#include <core/hw/bus.hpp>

namespace kanacore::hw::uart {

using namespace common;

constexpr u64 UART4_ADDR = 0x1E4C0000;
constexpr u64 HP_REMOTE_ADDR = 0x1E500000;
constexpr u64 UART_SIZE = 0x1000;

constexpr u64 NUM_UARTS = 2;

// Not an actual UART base address
constexpr u64 IO_ADDR = 0x1E000000;

enum IoAddress {
    IO_ADDRESS_RXERROR  = IO_ADDR + 0x004,
    IO_ADDRESS_STATUS   = IO_ADDR + 0x018,
    IO_ADDRESS_INTBAUD  = IO_ADDR + 0x024,
    IO_ADDRESS_FRACBAUD = IO_ADDR + 0x028,
    IO_ADDRESS_LINECTRL = IO_ADDR + 0x02C,
    IO_ADDRESS_CONTROL  = IO_ADDR + 0x030,
    IO_ADDRESS_FIFOLVL  = IO_ADDR + 0x034,
    IO_ADDRESS_INTRMASK = IO_ADDR + 0x038,
    IO_ADDRESS_RAWINTR  = IO_ADDR + 0x03C,
    IO_ADDRESS_INTRSTAT = IO_ADDR + 0x040,
    IO_ADDRESS_INTRCLR  = IO_ADDR + 0x044,
};

#define HW_UART_RXERROR  uart->receive_error
#define HW_UART_STATUS   uart->status
#define HW_UART_INTBAUD  uart->latched_int_baudrate
#define HW_UART_FRACBAUD uart->latched_frac_baudrate
#define HW_UART_LINECTRL uart->line_control
#define HW_UART_CONTROL  uart->control
#define HW_UART_FIFOLVL  uart->fifo_level
#define HW_UART_INTRMASK uart->interrupt_mask
#define HW_UART_RAWINTR  uart->raw_interrupt_status
#define HW_UART_INTRSTAT uart->interrupt_status

struct Uart {
    std::shared_ptr<spdlog::logger> logger;

    u8 receive_error;

    union {
        u16 raw;

        struct {
            u16 clear_to_send       : 1;
            u16 data_set_ready      : 1;
            u16 data_carrier        : 1;
            u16 busy                : 1;
            u16 receive_fifo_empty  : 1;
            u16 transmit_fifo_full  : 1;
            u16 receive_fifo_full   : 1;
            u16 transmit_fifo_empty : 1;
            u16 ring_indicator      : 1;
            u16                     : 7;
        };
    } status;

    u16 int_baudrate, latched_int_baudrate;
    u8 frac_baudrate, latched_frac_baudrate;

    union {
        u8 raw;

        struct {
            u8 send_break    : 1;
            u8 parity_enable : 1;
            u8 even_parity   : 1;
            u8 two_stop      : 1;
            u8 fifo_enable   : 1;
            u8 word_length   : 2;
            u8 sticky_parity : 1;
        };
    } line_control;

    union {
        u16 raw;

        struct {
            u16 uart_enable         : 1;
            u16 sir_enable          : 1;
            u16 sir_low_power       : 1;
            u16                     : 4;
            u16 loop_back_mode      : 1;
            u16 transmit_enable     : 1;
            u16 receive_enable      : 1;
            u16 data_transmit_ready : 1;
            u16 request_to_send     : 1;
            u16 out                 : 2;
            u16 rts_enable          : 1;
            u16 cts_enable          : 1;
        };
    } control;

    union {
        u8 raw;

        struct {
            u8 transmit_level : 3;
            u8 receive_level  : 3;
            u8                : 2;
        };
    } fifo_level;

    u16 interrupt_mask;
    u16 raw_interrupt_status;
    u16 interrupt_status;

    void set_line_control(const u8 data) {
        line_control.raw = data;

        // Update baudrate registers
        int_baudrate = latched_int_baudrate;
        frac_baudrate = latched_frac_baudrate;
    }
};

static struct {} ctx;

static std::array<Uart, NUM_UARTS> uarts;

template<int uart_num>
static void update_status() {
    static_assert(uart_num < NUM_UARTS);
    
    Uart* uart = &uarts[uart_num];

    // These bits are all active low
    HW_UART_STATUS.clear_to_send  = 1;
    HW_UART_STATUS.data_set_ready = 1;
    HW_UART_STATUS.data_carrier   = 1;
    HW_UART_STATUS.ring_indicator = 1;

    HW_UART_STATUS.receive_fifo_empty  = 1;
    HW_UART_STATUS.transmit_fifo_full  = 0;
    HW_UART_STATUS.receive_fifo_full   = 0;
    HW_UART_STATUS.transmit_fifo_empty = 1;

    // Supposedly also set if UART isn't enabled
    HW_UART_STATUS.busy = !HW_UART_STATUS.transmit_fifo_empty;
}

template<int uart_num>
static u32 read(const u32 addr) {
    static_assert(uart_num < NUM_UARTS);
    
    Uart* uart = &uarts[uart_num];

    switch (addr & ~0xFF0000) {
        case IoAddress::IO_ADDRESS_RXERROR:
            uart->logger->debug("RXERROR read32");
            return HW_UART_RXERROR;
        case IoAddress::IO_ADDRESS_STATUS:
            uart->logger->debug("STATUS read32");
            return HW_UART_STATUS.raw;
        case IoAddress::IO_ADDRESS_LINECTRL:
            uart->logger->debug("LINECTRL read32");
            return HW_UART_LINECTRL.raw;
        case IoAddress::IO_ADDRESS_CONTROL:
            uart->logger->debug("CONTROL read32");
            return HW_UART_CONTROL.raw;
        case IoAddress::IO_ADDRESS_INTRCLR:
            uart->logger->warn("CONTROL read32");

            // Does this just return 0? The interrupt status?
            return 0;
        default:
            uart->logger->error("Unmapped read32 @ {:08X}", addr);
            exit(1);
    }
}

template<int uart_num>
static void write(const u32 addr, const u32 data) {
    static_assert(uart_num < NUM_UARTS);
    
    Uart* uart = &uarts[uart_num];

    switch (addr & ~0xFF0000) {
        case IoAddress::IO_ADDRESS_RXERROR:
            uart->logger->debug("RXERROR write32 = {:08X}", data);

            // This ALWAYS clears all receive errors
            HW_UART_RXERROR = 0;
            break;
        case IoAddress::IO_ADDRESS_INTBAUD:
            uart->logger->debug("INTBAUD write32 = {:08X}", data);

            HW_UART_INTBAUD = data;
            break;
        case IoAddress::IO_ADDRESS_FRACBAUD:
            uart->logger->debug("FRACBAUD write32 = {:08X}", data);

            HW_UART_FRACBAUD = data;
            break;
        case IoAddress::IO_ADDRESS_LINECTRL:
            uart->logger->debug("LINECTRL write32 = {:08X}", data);
            uart->set_line_control(data);
            break;
        case IoAddress::IO_ADDRESS_CONTROL:
            uart->logger->debug("CONTROL write32 = {:08X}", data);

            HW_UART_CONTROL.raw = data;
            break;
        case IoAddress::IO_ADDRESS_FIFOLVL:
            uart->logger->debug("FIFOLVL write32 = {:08X}", data);

            HW_UART_FIFOLVL.raw = data;
            break;
        case IoAddress::IO_ADDRESS_INTRMASK:
            uart->logger->debug("INTRMASK write32 = {:08X}", data);

            HW_UART_INTRMASK = data;
            break;
        case IoAddress::IO_ADDRESS_INTRCLR:
            uart->logger->debug("INTRCLR write32 = {:08X}", data);

            HW_UART_RAWINTR  &= ~data;
            HW_UART_INTRSTAT &= ~data;
            break;
        default:
            uart->logger->error("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            exit(1);
    }
}

template<int uart_num>
static void map(const u32 addr) {
    static_assert(uart_num < NUM_UARTS);

    // To my knowledge, UART I/O is never not read/written using 32-bit accesses.
    // Like SPI, UART registers are 16-bit
    const bus::PageDescriptor page_desc {
        .read32_func  = read<uart_num>,
        .write32_func = write<uart_num>,
    };

    kanacore::get_sc_bus_ptr()->map(addr, UART_SIZE, page_desc);
}

void initialize() {
    uarts[0].logger = spdlog::stdout_color_st("UART4");
    uarts[1].logger = spdlog::stdout_color_st("HP/Remote");

    std::memset(&ctx, 0, sizeof(ctx));
}

void soft_reset() {
    update_status<0>();
    update_status<1>();
}

void hard_reset() {
    map<0>(UART4_ADDR);
    map<1>(HP_REMOTE_ADDR);

    update_status<0>();
    update_status<1>();
}

void shutdown() {

}

};
