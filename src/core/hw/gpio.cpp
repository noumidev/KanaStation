/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/gpio.cpp - General-purpose I/O */

#include <core/hw/gpio.hpp>

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <common/types.hpp>
#include <core/hw/bus.hpp>
#include <core/hw/sysctrl.hpp>

namespace kanacore::hw::gpio {

using namespace common;

constexpr u64 GPIO_ADDR = 0x1E240000;
constexpr u64 GPIO_SIZE = 0x1000;

constexpr u64 NUM_PINS = 32;

enum IoAddress {
    IO_ADDRESS_OUTEN    = GPIO_ADDR + 0x000,
    IO_ADDRESS_READ     = GPIO_ADDR + 0x004,
    IO_ADDRESS_SET      = GPIO_ADDR + 0x008,
    IO_ADDRESS_CLEAR    = GPIO_ADDR + 0x00C,
    IO_ADDRESS_EDGEINTR = GPIO_ADDR + 0x010,
    IO_ADDRESS_RISEINTR = GPIO_ADDR + 0x014,
    IO_ADDRESS_FALLINTR = GPIO_ADDR + 0x018,
    IO_ADDRESS_INTRMASK = GPIO_ADDR + 0x01C,
    IO_ADDRESS_INTRSTAT = GPIO_ADDR + 0x020,
    IO_ADDRESS_INTRCLR  = GPIO_ADDR + 0x024,
    IO_ADDRESS_INEN     = GPIO_ADDR + 0x040,
};

#define HW_GPIO_OUTEN    ctx.output_enable
#define HW_GPIO_EDGEINTR ctx.edge_interrupt_enable
#define HW_GPIO_RISEINTR ctx.rising_edge_enable
#define HW_GPIO_FALLINTR ctx.falling_edge_enable
#define HW_GPIO_INTRMASK ctx.interrupt_mask
#define HW_GPIO_INTRSTAT ctx.interrupt_status
#define HW_GPIO_INEN     ctx.input_enable

static struct {
    u32 output_enable;
    u32 edge_interrupt_enable;
    u32 rising_edge_enable;
    u32 falling_edge_enable;
    u32 interrupt_mask;
    u32 interrupt_status;
    u32 input_enable;
} ctx;

static std::shared_ptr<spdlog::logger> logger;

static void clear_pins(const u32 data) {
    const u32 gpio_enable = sysctrl::get_gpio_enable();

    for (u32 i = 0; i < NUM_PINS; i++) {
        if ((data & gpio_enable & HW_GPIO_OUTEN & (1 << i)) != 0) {
            logger->warn("Unimplemented pin {} clear", i);
        }
    }
}

static u32 read_pins() {
    const u32 gpio_enable = sysctrl::get_gpio_enable();

    u32 data = 0;

    for (u32 i = 0; i < NUM_PINS; i++) {
        if ((gpio_enable & HW_GPIO_INEN & (1 << i)) != 0) {
            logger->warn("Unimplemented pin {} read", i);

            data |= 0 << i;
        }
    }

    return data;
}

static void set_pins(const u32 data) {
    const u32 gpio_enable = sysctrl::get_gpio_enable();

    for (u32 i = 0; i < NUM_PINS; i++) {
        if ((data & gpio_enable & HW_GPIO_OUTEN & (1 << i)) != 0) {
            logger->warn("Unimplemented pin {} set", i);
        }
    }
}

static u32 read(const u32 addr) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_OUTEN:
            logger->debug("OUTEN read32");
            return HW_GPIO_OUTEN;
        case IoAddress::IO_ADDRESS_READ:
            logger->debug("READ read32");
            return read_pins();
        case IoAddress::IO_ADDRESS_INTRMASK:
            logger->debug("INTRMASK read32");
            return HW_GPIO_INTRMASK;
        case IoAddress::IO_ADDRESS_INTRSTAT:
            logger->debug("INTRSTAT read32");
            return HW_GPIO_INTRSTAT;
        case IoAddress::IO_ADDRESS_INEN:
            logger->debug("INEN read32");
            return HW_GPIO_INEN;
        case GPIO_ADDR + 0x048:
            // TODO: figure out what this is
            logger->warn("Unmapped read32 @ {:08X}", addr);
            return 0;
        default:
            logger->error("Unmapped read32 @ {:08X}", addr);
            exit(1);
    }
}

static void write(const u32 addr, const u32 data) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_OUTEN:
            logger->debug("OUTEN write32 = {:08X}", data);

            HW_GPIO_OUTEN = data;
            break;
        case IoAddress::IO_ADDRESS_SET:
            logger->debug("SET write32 = {:08X}", data);
            set_pins(data);
            break;
        case IoAddress::IO_ADDRESS_CLEAR:
            logger->debug("CLEAR write32 = {:08X}", data);
            clear_pins(data);
            break;
        case IoAddress::IO_ADDRESS_EDGEINTR:
            logger->debug("EDGEINTR write32 = {:08X}", data);

            HW_GPIO_EDGEINTR = data;
            break;
        case IoAddress::IO_ADDRESS_RISEINTR:
            logger->debug("RISEINTR write32 = {:08X}", data);

            HW_GPIO_RISEINTR = data;
            break;
        case IoAddress::IO_ADDRESS_FALLINTR:
            logger->debug("FALLINTR write32 = {:08X}", data);

            HW_GPIO_FALLINTR = data;
            break;
        case IoAddress::IO_ADDRESS_INTRMASK:
            logger->debug("INTRMASK write32 = {:08X}", data);

            HW_GPIO_INTRMASK = data;
            break;
        case IoAddress::IO_ADDRESS_INTRCLR:
            logger->debug("INTRCLR write32 = {:08X}", data);

            HW_GPIO_INTRSTAT &= ~data;
            break;
        case IoAddress::IO_ADDRESS_INEN:
            logger->debug("INEN write32 = {:08X}", data);

            HW_GPIO_INEN = data;
            break;
        default:
            logger->error("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            exit(1);
    }
}

void initialize() {
    logger = spdlog::stdout_color_st("GPIO");

    std::memset(&ctx, 0, sizeof(ctx));
}

void soft_reset() {
    
}

void hard_reset() {
    const bus::PageDescriptor page_desc {
        // To my knowledge, GPIO I/O is never not read/written using 32-bit accesses
        .read32_func  = read,
        .write32_func = write,
    };

    bus::map(GPIO_ADDR, GPIO_SIZE, page_desc);
}

void shutdown() {

}

};
