/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/syscon.cpp - SysCon HLE */

#include <core/hw/syscon.hpp>

#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <core/kanacore.hpp>
#include <core/scheduler.hpp>
#include <core/hw/gpio.hpp>
#include <core/hw/spi.hpp>

namespace kanacore::hw::syscon {

using namespace common;

constexpr u64 BUFFER_SIZE = 16;

constexpr u64 SCRATCHPAD_SIZE = 0x20;

// These values were taken from one of my PSPs
constexpr static u8 INITIAL_SCRATCHPAD[SCRATCHPAD_SIZE] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x2F, 0x00, 0x00, 0xEA, 0x3C, 0x91, 0x4B,
    0x4F, 0x5F, 0x52, 0x58, 0x1C, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

enum SysconCommand {
    SYSCON_COMMAND_GET_BARYON_VERSION            = 0x01,
    SYSCON_COMMAND_GET_TACHYON_TEMP              = 0x05,
    SYSCON_COMMAND_GET_KERNEL_DIGITAL_KEY        = 0x07,
    SYSCON_COMMAND_GET_KERNEL_DIGITAL_KEY_ANALOG = 0x08,
    SYSCON_COMMAND_READ_CLOCK                    = 0x09,
    SYSCON_COMMAND_READ_ALARM                    = 0x0A,
    SYSCON_COMMAND_GET_POWER_SUPPLY_STATUS       = 0x0B,
    SYSCON_COMMAND_GET_WAKE_UP_FACTOR            = 0x0E,
    SYSCON_COMMAND_GET_BARYON_TIMESTAMP          = 0x11,
    SYSCON_COMMAND_WRITE_CLOCK                   = 0x20,
    SYSCON_COMMAND_WRITE_ALARM                   = 0x22,
    SYSCON_COMMAND_WRITE_SCRATCHPAD              = 0x23,
    SYSCON_COMMAND_READ_SCRATCHPAD               = 0x24,
    SYSCON_COMMAND_SEND_SETPARAM                 = 0x25,
    SYSCON_COMMAND_CTRL_TACHYON_WDT              = 0x31,
    SYSCON_COMMAND_RESET_DEVICE                  = 0x32,
    SYSCON_COMMAND_CTRL_ANALOG_XY_POLLING        = 0x33,
    SYSCON_COMMAND_CTRL_HR_POWER                 = 0x34,
    SYSCON_COMMAND_GET_POMMEL_VERSION            = 0x40,
    SYSCON_COMMAND_CTRL_VOLTAGE                  = 0x42,
    SYSCON_COMMAND_GET_POWER_STATUS              = 0x46,
    SYSCON_COMMAND_CTRL_LED                      = 0x47,
    SYSCON_COMMAND_CTRL_LEPTON_POWER             = 0x4B,
    SYSCON_COMMAND_CTRL_MS_POWER                 = 0x4C,
};

enum BufferIndex {
    BUFFER_INDEX_COMMAND        = 0,
    BUFFER_INDEX_STATUS         = 0,
    BUFFER_INDEX_SIZE           = 1,
    BUFFER_INDEX_RECEIVE_DATA   = 2,
    BUFFER_INDEX_RESPONSE       = 2,
    BUFFER_INDEX_TRANSMIT_DATA  = 3,
};

enum BaryonStatus {
    BARYON_STATUS_AC_POWER = 1 << 0,
    BARYON_STATUS_ALARM    = 1 << 3,
    BARYON_STATUS_HR_POWER = 1 << 4,
};

static struct {
    std::array<u8, SCRATCHPAD_SIZE> scratchpad;

    u8 baryon_status;
} ctx;

static std::shared_ptr<spdlog::logger> logger;

static struct {
    u8 buf[BUFFER_SIZE];

    u64 ptr;
} receive_buffer, transmit_buffer;

static inline u8 calculate_checksum(const u8* buf, const u64 size) {
    assert((size < BUFFER_SIZE) && (buf[BufferIndex::BUFFER_INDEX_SIZE] == size));

    u8 checksum = 0;

    // The checksum is the negated sum of all bytes in the command buffer
    // (the checksum byte is ignored)
    for (u64 i = 0; i < size; i++) {
        checksum += buf[i];
    }

    return ~checksum;
}

static bool verify_checksum(const u8* buf, const u64 size, const u8 expected_checksum) {
    return calculate_checksum(buf, size) == expected_checksum;
}

static void transmit_data(const int) {
    u8*  buf = transmit_buffer.buf;
    u64& ptr = transmit_buffer.ptr;

    assert(ptr < BUFFER_SIZE);

    spi::receive((buf[ptr] << 8) | buf[ptr + 1]);

    ptr += 2;

    if (ptr < (buf[BufferIndex::BUFFER_INDEX_SIZE] + 1)) {
        scheduler::schedule_event(
            scheduler::EventType::SYSCON_TX,
            transmit_data,
            0,
            scheduler::SPI_CLOCKRATE
        );
    } else {
        spi::end_reception();
    
        gpio::set_pin(gpio::Pin::PIN_SYSCON_ACKNOWLEDGE);
    }
}

static void start_transmission() {
    spi::start_reception();

    scheduler::schedule_event(
        scheduler::EventType::SYSCON_TX,
        transmit_data,
        0,
        scheduler::SPI_CLOCKRATE
    );
}

static void write_transmit_data(const u8* data, const u64 size) {
    assert((size + BufferIndex::BUFFER_INDEX_TRANSMIT_DATA) < BUFFER_SIZE);

    u8* buf = transmit_buffer.buf;

    std::memcpy(&buf[BufferIndex::BUFFER_INDEX_TRANSMIT_DATA], data, size);

    // Set the response buffer size
    buf[BufferIndex::BUFFER_INDEX_SIZE] = size + 3;
}

static void common_read(const u8 command) {
    u32 data;

    switch (command) {
        case SysconCommand::SYSCON_COMMAND_GET_TACHYON_TEMP:
            logger->debug("GET_TACHYON_TEMP");
            
            // Seems to be the/an expected value
            data = 13094;
            break;
        case SysconCommand::SYSCON_COMMAND_GET_KERNEL_DIGITAL_KEY:
        case SysconCommand::SYSCON_COMMAND_GET_KERNEL_DIGITAL_KEY_ANALOG:
            // TODO: return analog stick data
            logger->debug("GET_KERNEL_DIGITAL_KEY");
            
            data = kanacore::get_button_state();
            break;
        case SysconCommand::SYSCON_COMMAND_READ_CLOCK:
            logger->debug("READ_CLOCK");
            
            // Not sure what this is
            data = 0;
            break;
        case SysconCommand::SYSCON_COMMAND_READ_ALARM:
            logger->debug("READ_ALARM");
            
            // See above
            data = 0;
            break;
        case SysconCommand::SYSCON_COMMAND_GET_POWER_SUPPLY_STATUS:
            logger->debug("GET_POWER_SUPPLY_STATUS");

            // What is this? Bit 1 seems to indicate that a battery is present
            data = 0xC0;
            break;
        case SysconCommand::SYSCON_COMMAND_GET_WAKE_UP_FACTOR:
            logger->debug("GET_WAKE_UP_FACTOR");
            
            // I don't know what this is. After boot, this value is 0x4C0, but IPL crashes
            // if bit 7 is set.
            data = 0x440;
            break;
        case SysconCommand::SYSCON_COMMAND_GET_POMMEL_VERSION:
            logger->debug("GET_POMMEL_VERSION");
            
            data = 0x112;
            break;
        case SysconCommand::SYSCON_COMMAND_GET_POWER_STATUS:
            logger->debug("GET_POWER_STATUS");

            // I don't yet know what this is. This appears to return the power status
            // for certain peripherals, but I dunno why SYSCON has access to this
            data = 0;
            break;
        default:
            logger->error("Unhandled common read for command {:02X}", command);
            exit(1);
    }

    write_transmit_data((u8*)&data, sizeof(data));
}

static void common_write(const u8 command) {
    const u8* buf = receive_buffer.buf;

    i32 data = 0;

    switch (buf[BufferIndex::BUFFER_INDEX_SIZE] - 2) {
        case sizeof(i8):
            data = (i8)buf[BufferIndex::BUFFER_INDEX_RECEIVE_DATA];
            break;
        case sizeof(i16):
            std::memcpy((u8*)&data, &buf[BufferIndex::BUFFER_INDEX_RECEIVE_DATA], sizeof(i16));

            data = (i16)data;
            break;
        case sizeof(i16) + sizeof(u8): // i24? Multiple values?
            std::memcpy((u8*)&data, &buf[BufferIndex::BUFFER_INDEX_RECEIVE_DATA], sizeof(i16) + sizeof(u8));

            data = (data << 8) >> 8;
            break;
        case sizeof(i32):
            std::memcpy((u8*)&data, &buf[BufferIndex::BUFFER_INDEX_RECEIVE_DATA], sizeof(i32));
            break;
        default:
            logger->error("Invalid common write size");
            exit(1);
    }

    switch (command) {
        case SysconCommand::SYSCON_COMMAND_WRITE_CLOCK:
            logger->debug("WRITE_CLOCK: {}", data);
            break;
        case SysconCommand::SYSCON_COMMAND_WRITE_ALARM:
            logger->debug("WRITE_ALARM: {}", data);
            break;
        case SysconCommand::SYSCON_COMMAND_CTRL_TACHYON_WDT:
            logger->debug("CTRL_TACHYON_WDT: {}", data);
            break;
        case SysconCommand::SYSCON_COMMAND_RESET_DEVICE:
            logger->debug("RESET_DEVICE: {}", data);
            break;
        case SysconCommand::SYSCON_COMMAND_CTRL_ANALOG_XY_POLLING:
            logger->debug("CTRL_ANALOG_XY_POLLING: {}", data);
            break;
        case SysconCommand::SYSCON_COMMAND_CTRL_VOLTAGE:
            logger->debug("CTRL_VOLTAGE: {}", data);
            break;
        case SysconCommand::SYSCON_COMMAND_CTRL_LED:
            logger->debug("CTRL_LED: {}", data);
            break;
        case SysconCommand::SYSCON_COMMAND_CTRL_LEPTON_POWER:
            logger->debug("CTRL_LEPTON_POWER: {}", data);
            break;
        case SysconCommand::SYSCON_COMMAND_CTRL_MS_POWER:
            logger->debug("CTRL_MS_POWER: {}", data);
            break;
        default:
            logger->error("Unhandled common write for command {:02X}", command);
            exit(1);
    }

    write_transmit_data(nullptr, 0);
}

static void command_ctrl_hr_power() {
    const bool hr_power = (receive_buffer.buf[BUFFER_INDEX_RECEIVE_DATA] & 1) != 0;

    logger->debug("CTRL_HR_POWER (HR power: {})", hr_power);

    if (hr_power) {
        ctx.baryon_status |= BaryonStatus::BARYON_STATUS_HR_POWER;
    } else {
        ctx.baryon_status &= ~BaryonStatus::BARYON_STATUS_HR_POWER;
    }

    write_transmit_data(nullptr, 0);
}

static void command_get_baryon_timestamp() {
    constexpr const char* BARYON_TIMESTAMP = "200509260441";

    logger->debug("GET_BARYON_TIMESTAMP");

    write_transmit_data((u8*)BARYON_TIMESTAMP, std::strlen(BARYON_TIMESTAMP));
}

static void command_get_baryon_version() {
    constexpr u32 BARYON_VERSION = 0x00114000;

    logger->debug("GET_BARYON_VERSION");

    write_transmit_data((u8*)&BARYON_VERSION, sizeof(BARYON_VERSION));
}

static void command_read_scratchpad() {
    const u8 idx  = receive_buffer.buf[BUFFER_INDEX_RECEIVE_DATA] >> 2;
    const u8 size = receive_buffer.buf[BUFFER_INDEX_RECEIVE_DATA] & 3;

    logger->debug("READ_SCRATCHPAD (idx: {}, size: {})", idx, size);

    assert((idx + size) < SCRATCHPAD_SIZE);

    write_transmit_data(&ctx.scratchpad[idx], size);
}

static void command_send_setparam() {
    u64 setparam;

    std::memcpy(&setparam, &receive_buffer.buf[BUFFER_INDEX_RECEIVE_DATA], sizeof(setparam));

    logger->debug("SEND_SETPARAM: {:016X}", setparam);

    write_transmit_data(nullptr, 0);
}

static void command_write_scratchpad() {
    const u8 idx  = receive_buffer.buf[BUFFER_INDEX_RECEIVE_DATA] >> 2;
    const u8 size = receive_buffer.buf[BUFFER_INDEX_RECEIVE_DATA] & 3;

    logger->debug("WRITE_SCRATCHPAD (idx: {}, size: {})", idx, size);

    assert((idx + size) < SCRATCHPAD_SIZE);

    for (u8 i = 0; i < size; i++) {
        ctx.scratchpad[idx + i] = receive_buffer.buf[BUFFER_INDEX_RECEIVE_DATA + i];
    }

    write_transmit_data(nullptr, 0);
}

static void start_command() {
    u8* buf = receive_buffer.buf;

    const u8 command = buf[BufferIndex::BUFFER_INDEX_COMMAND];
    const u8 size    = buf[BufferIndex::BUFFER_INDEX_SIZE];

    assert(size < (BUFFER_SIZE - 1));

    if (!verify_checksum(buf, size, buf[size])) {
        logger->error("Command checksum is invalid");
        exit(1);
    }

    switch (command) {
        case SysconCommand::SYSCON_COMMAND_GET_BARYON_VERSION:
            command_get_baryon_version();
            break;
        case SysconCommand::SYSCON_COMMAND_GET_BARYON_TIMESTAMP:
            command_get_baryon_timestamp();
            break;
        case SysconCommand::SYSCON_COMMAND_WRITE_SCRATCHPAD:
            command_write_scratchpad();
            break;
        case SysconCommand::SYSCON_COMMAND_READ_SCRATCHPAD:
            command_read_scratchpad();
            break;
        case SysconCommand::SYSCON_COMMAND_SEND_SETPARAM:
            command_send_setparam();
            break;
        case SysconCommand::SYSCON_COMMAND_CTRL_HR_POWER:
            command_ctrl_hr_power();
            break;
        case SysconCommand::SYSCON_COMMAND_WRITE_CLOCK:
        case SysconCommand::SYSCON_COMMAND_WRITE_ALARM:
        case SysconCommand::SYSCON_COMMAND_CTRL_TACHYON_WDT:
        case SysconCommand::SYSCON_COMMAND_RESET_DEVICE:
        case SysconCommand::SYSCON_COMMAND_CTRL_ANALOG_XY_POLLING:
        case SysconCommand::SYSCON_COMMAND_CTRL_VOLTAGE:
        case SysconCommand::SYSCON_COMMAND_CTRL_LED:
        case SysconCommand::SYSCON_COMMAND_CTRL_LEPTON_POWER:
        case SysconCommand::SYSCON_COMMAND_CTRL_MS_POWER:
            common_write(command);
            break;
        case SysconCommand::SYSCON_COMMAND_GET_TACHYON_TEMP:
        case SysconCommand::SYSCON_COMMAND_GET_KERNEL_DIGITAL_KEY:
        case SysconCommand::SYSCON_COMMAND_GET_KERNEL_DIGITAL_KEY_ANALOG:
        case SysconCommand::SYSCON_COMMAND_READ_CLOCK:
        case SysconCommand::SYSCON_COMMAND_READ_ALARM:
        case SysconCommand::SYSCON_COMMAND_GET_POWER_SUPPLY_STATUS:
        case SysconCommand::SYSCON_COMMAND_GET_WAKE_UP_FACTOR:
        case SysconCommand::SYSCON_COMMAND_GET_POMMEL_VERSION:
        case SysconCommand::SYSCON_COMMAND_GET_POWER_STATUS:
            common_read(command);
            break;
        default:
            logger->error("Unimplemented command {:02X} (size: {})", command, size);
            exit(1);
    }

    buf = transmit_buffer.buf;

    // Build response header (size already set by command)
    buf[BufferIndex::BUFFER_INDEX_STATUS  ] = ctx.baryon_status;
    buf[BufferIndex::BUFFER_INDEX_RESPONSE] = 0x82;

    // Calculate checksum
    const u64 response_size = buf[BufferIndex::BUFFER_INDEX_SIZE];

    buf[response_size] = calculate_checksum(buf, response_size);

    start_transmission();

    gpio::clear_pin(gpio::Pin::PIN_SYSCON_ACKNOWLEDGE);
}

void initialize() {
    logger = spdlog::stdout_color_st("SYSCON");

    std::memcpy(ctx.scratchpad.data(), INITIAL_SCRATCHPAD, SCRATCHPAD_SIZE);

    ctx.baryon_status = BaryonStatus::BARYON_STATUS_ALARM | BaryonStatus::BARYON_STATUS_AC_POWER;
}

void soft_reset() {
    
}

void hard_reset() {
    constexpr bool BOOT_SERVICE_MODE = false;

    if (BOOT_SERVICE_MODE) {
        gpio::set_pin(gpio::Pin::PIN_SYSCON_ACKNOWLEDGE);
    }
}

void shutdown() {

}

void clear_notify() {
    logger->debug("Clear notification");

    // Reset command state
    std::memset(receive_buffer.buf, 0, BUFFER_SIZE);
    receive_buffer.ptr  = 0;

    std::memset(transmit_buffer.buf, 0, BUFFER_SIZE);
    transmit_buffer.ptr = 0;
}

void set_notify() {
    logger->debug("Notify");

    // Ideally, this is what kicks off command processing
}

void receive(const u16 data) {
    logger->debug("Receiving data {:04X}", data);

    u8*  buf = receive_buffer.buf;
    u64& ptr = receive_buffer.ptr;

    assert(ptr < BUFFER_SIZE);
    
    // Reverse endianness
    buf[ptr++] = data >> 8;
    buf[ptr++] = data;

    if (ptr > buf[BufferIndex::BUFFER_INDEX_SIZE]) {
        start_command();
    }
}

};
