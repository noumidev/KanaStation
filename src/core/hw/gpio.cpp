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

#include <core/kanacore.hpp>
#include <core/hw/bus.hpp>
#include <core/hw/intc.hpp>
#include <core/hw/syscon.hpp>
#include <core/hw/sysctrl.hpp>

namespace kanacore::hw::gpio {

using namespace common;

constexpr u64 GPIO_ADDR = 0x1E240000;
constexpr u64 GPIO_SIZE = 0x1000;

constexpr int GPIO_INTERRUPT = 4;

enum IoAddress {
    IO_ADDRESS_OUTEN    = GPIO_ADDR + 0x000,
    IO_ADDRESS_READ     = GPIO_ADDR + 0x004,
    IO_ADDRESS_SET      = GPIO_ADDR + 0x008,
    IO_ADDRESS_CLEAR    = GPIO_ADDR + 0x00C,
    IO_ADDRESS_EDGEINTR = GPIO_ADDR + 0x010,
    IO_ADDRESS_FALLINTR = GPIO_ADDR + 0x014,
    IO_ADDRESS_RISEINTR = GPIO_ADDR + 0x018,
    IO_ADDRESS_INTRMASK = GPIO_ADDR + 0x01C,
    IO_ADDRESS_INTRSTAT = GPIO_ADDR + 0x020,
    IO_ADDRESS_INTRCLR  = GPIO_ADDR + 0x024,
    IO_ADDRESS_CPTEN    = GPIO_ADDR + 0x030,
    IO_ADDRESS_TMRCPTEN = GPIO_ADDR + 0x034,
    IO_ADDRESS_INEN     = GPIO_ADDR + 0x040,
};

#define HW_GPIO_OUTEN    ctx.output_enable
#define HW_GPIO_EDGEINTR ctx.edge_interrupt_enable
#define HW_GPIO_FALLINTR ctx.falling_edge_enable
#define HW_GPIO_RISEINTR ctx.rising_edge_enable
#define HW_GPIO_INTRMASK ctx.interrupt_mask
#define HW_GPIO_INTRSTAT ctx.interrupt_status
#define HW_GPIO_CPTEN    ctx.capture_enable
#define HW_GPIO_TMRCPTEN ctx.timer_capture_enable
#define HW_GPIO_INEN     ctx.input_enable

static struct {
    u32 inputs;

    u32 output_enable;
    u32 edge_interrupt_enable;
    u32 falling_edge_enable;
    u32 rising_edge_enable;
    u32 interrupt_mask;
    u32 interrupt_status;
    u32 capture_enable;
    u32 timer_capture_enable;
    u32 input_enable;
} ctx;

static std::shared_ptr<spdlog::logger> logger;

static std::array<void (*)(void), Pin::PIN_NUM> clear_funcs;
static std::array<void (*)(void), Pin::PIN_NUM> set_funcs;

static const char* get_pin_name(const u32 pin) {
    switch (pin) {
        case Pin::PIN_SYSCON_NOTIFY:
            return "SYSCON_NOTIFY";
        case Pin::PIN_SYSCON_ACKNOWLEDGE:
            return "SYSCON_ACKNOWLEDGE";
        default:
            return "N/A";
    }
}

static void check_pending_interrupts() {
    if ((HW_GPIO_INTRMASK & HW_GPIO_INTRSTAT) != 0) {
        intc::assert_sc_interrupt(GPIO_INTERRUPT);
    } else {
        intc::clear_sc_interrupt(GPIO_INTERRUPT);
    }
}

static void clear_pins(const u32 data) {
    const u32 gpio_enable = sysctrl::get_gpio_enable();

    for (u32 i = 0; i < Pin::PIN_NUM; i++) {
        if ((data & gpio_enable & HW_GPIO_OUTEN & (1 << i)) != 0) {
            logger->debug("Clearing pin {}", get_pin_name(i));

            if (clear_funcs[i] != nullptr) {
                clear_funcs[i]();
            } else {
                logger->warn("No clear handler installed for pin {}", i);
            }
        }
    }
}

static u32 read_pins() {
    const u32 gpio_enable = sysctrl::get_gpio_enable();

    u32 data = 0;

    for (u32 i = 0; i < Pin::PIN_NUM; i++) {
        if ((gpio_enable & HW_GPIO_INEN & (1 << i)) != 0) {
            logger->debug("Reading pin {}", get_pin_name(i));

            data |= ctx.inputs << i;
        }
    }

    return data;
}

static void set_pins(const u32 data) {
    const u32 gpio_enable = sysctrl::get_gpio_enable();

    for (u32 i = 0; i < Pin::PIN_NUM; i++) {
        if ((data & gpio_enable & HW_GPIO_OUTEN & (1 << i)) != 0) {
            logger->debug("Setting pin {}", get_pin_name(i));

            if (set_funcs[i] != nullptr) {
                set_funcs[i]();
            } else {
                logger->warn("No set handler installed for pin {}", i);
            }
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
        case IoAddress::IO_ADDRESS_CPTEN:
            // The PSPdev wiki says this is what it is. Unsure how it works
            logger->debug("CPTEN write32 = {:08X}", data);

            HW_GPIO_CPTEN = data;
            break;
        case IoAddress::IO_ADDRESS_TMRCPTEN:
            // The PSPdev wiki says this is what it is. Unsure how it works
            logger->debug("TMRCPTEN write32 = {:08X}", data);

            HW_GPIO_TMRCPTEN = data;
            break;
        case IoAddress::IO_ADDRESS_INEN:
            logger->debug("INEN write32 = {:08X}", data);

            HW_GPIO_INEN = data;
            break;
        default:
            logger->error("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            exit(1);
    }

    check_pending_interrupts();
}

void initialize() {
    logger = spdlog::stdout_color_st("GPIO");

    clear_funcs.fill(nullptr);
    clear_funcs[Pin::PIN_SYSCON_NOTIFY] = syscon::clear_notify;

    set_funcs.fill(nullptr);
    set_funcs[Pin::PIN_SYSCON_NOTIFY] = syscon::set_notify;

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

    kanacore::get_sc_bus_ptr()->map(GPIO_ADDR, GPIO_SIZE, page_desc);
}

void shutdown() {

}

void clear_pin(const u32 pin) {
    assert(pin < Pin::PIN_NUM);

    logger->debug("Clearing pin {}", get_pin_name(pin));

    const u32 old_pin = (ctx.inputs >> pin) & 1;

    ctx.inputs &= ~(1 << pin);

    if ((HW_GPIO_INTRMASK & HW_GPIO_FALLINTR & (1 << pin)) != 0) {
        const bool is_edge_triggered = (HW_GPIO_EDGEINTR & (1 << pin)) != 0;
    
        if ((is_edge_triggered && old_pin) || !is_edge_triggered) {
            HW_GPIO_INTRSTAT |= 1 << pin;
        }
    }

    check_pending_interrupts();
}

void set_pin(const u32 pin) {
    assert(pin < Pin::PIN_NUM);

    logger->debug("Setting pin {}", get_pin_name(pin));

    const u32 old_pin = (ctx.inputs >> pin) & 1;

    ctx.inputs |= 1 << pin;

    if ((HW_GPIO_INTRMASK & HW_GPIO_RISEINTR & (1 << pin)) != 0) {
        const bool is_edge_triggered = (HW_GPIO_EDGEINTR & (1 << pin)) != 0;
    
        if ((is_edge_triggered && !old_pin) || !is_edge_triggered) {
            HW_GPIO_INTRSTAT |= 1 << pin;
        }
    }

    check_pending_interrupts();
}

};
