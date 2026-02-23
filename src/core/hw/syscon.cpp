/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/syscon.cpp - SysCon HLE */

#include <core/hw/syscon.hpp>

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <core/scheduler.hpp>
#include <core/hw/gpio.hpp>
#include <core/hw/spi.hpp>

namespace kanacore::hw::syscon {

using namespace common;

constexpr u64 BUFFER_SIZE = 16;

enum SysconCommand {
    SYSCON_COMMAND_GET_BARYON_VERSION   = 0x01,
    SYSCON_COMMAND_GET_BARYON_TIMESTAMP = 0x11,
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
    BARYON_STATUS_AC_POWER = 0,
    BARYON_STATUS_ALARM    = 3,
};

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
        scheduler::schedule_event("SYSCON TX", transmit_data, 0, scheduler::SPI_CLOCKRATE);
    } else {
        spi::end_reception();
    
        gpio::set_pin(gpio::Pin::PIN_SYSCON_ACKNOWLEDGE);
    }
}

static void start_transmission() {
    spi::start_reception();

    scheduler::schedule_event("SYSCON TX", transmit_data, 0, scheduler::SPI_CLOCKRATE);
}

static void write_transmit_data(const u8* data, const u64 size) {
    assert((size + BufferIndex::BUFFER_INDEX_TRANSMIT_DATA) < BUFFER_SIZE);

    u8* buf = transmit_buffer.buf;

    std::memcpy(&buf[BufferIndex::BUFFER_INDEX_TRANSMIT_DATA], data, size);

    // Set the response buffer size
    buf[BufferIndex::BUFFER_INDEX_SIZE] = size + 3;
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
        default:
            logger->error("Unimplemented command {:02X} (size: {})", command, size);
            exit(1);
    }

    buf = transmit_buffer.buf;

    // Build response header (size already set by command)
    buf[BufferIndex::BUFFER_INDEX_STATUS  ] = (1 << BaryonStatus::BARYON_STATUS_ALARM) | (1 << BaryonStatus::BARYON_STATUS_AC_POWER);
    buf[BufferIndex::BUFFER_INDEX_RESPONSE] = 0x82;

    // Calculate checksum
    const u64 response_size = buf[BufferIndex::BUFFER_INDEX_SIZE];

    buf[response_size] = calculate_checksum(buf, response_size);

    start_transmission();

    gpio::clear_pin(gpio::Pin::PIN_SYSCON_ACKNOWLEDGE);
}

void initialize() {
    logger = spdlog::stdout_color_st("SYSCON");
}

void soft_reset() {
    
}

void hard_reset() {
     
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
