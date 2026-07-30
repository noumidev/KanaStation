/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/i2c/clockgen.cpp - CY27040 clock generator */

#include <core/hw/i2c/clockgen.hpp>

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <core/kanacore.hpp>

namespace kanacore::hw::i2c::clockgen {

using namespace common;

constexpr u8 REVISION = 4;

enum RegisterAddress {
    REGISTER_ADDRESS_ALL      = 0x00,
    REGISTER_ADDRESS_REVISION = 0x80,
    REGISTER_ADDRESS_CLKCTRL  = 0x81,
    REGISTER_ADDRESS_SSCTRL   = 0x82,
};

static struct {
    u8 clock_control;
    u8 ss_control;

    u8 reg_addr;
} ctx;

static std::shared_ptr<spdlog::logger> logger;

static void read_reg(std::vector<u8>& data) {
    switch (ctx.reg_addr) {
        case RegisterAddress::REGISTER_ADDRESS_ALL:
            logger->debug("ALL read");
            data.push_back(REVISION);
            data.push_back(ctx.clock_control);
            data.push_back(ctx.ss_control);
            break;
        case RegisterAddress::REGISTER_ADDRESS_REVISION:
            logger->debug("REVISION read");
            data.push_back(REVISION);
            break;
        case RegisterAddress::REGISTER_ADDRESS_CLKCTRL:
            logger->debug("CLKCTRL read");
            data.push_back(ctx.clock_control);
            break;
        case RegisterAddress::REGISTER_ADDRESS_SSCTRL:
            logger->debug("SSCTRL read");
            data.push_back(ctx.ss_control);
            break;
        default:
            logger->warn("Unimplemented register read {:02X}", ctx.reg_addr);
            data.push_back(0);
            break;
    }

    // Does this autoincrement the address...? The firmware doesn't seem to transmit
    // more than one register address per write
    ctx.reg_addr++;
}

static int write_reg(const u8* data) {
    int length = 1;

    switch (ctx.reg_addr) {
        case RegisterAddress::REGISTER_ADDRESS_CLKCTRL:
            ctx.clock_control = *data;

            logger->debug("CLKCTRL write = {:02X}", ctx.clock_control);
            break;
        default:
            logger->error("Unimplemented register write {:02X}", ctx.reg_addr);
            exit(1);
    }

    // Does this autoincrement the address...? The firmware doesn't seem to transmit
    // more than one register address per write
    ctx.reg_addr++;

    return length;
}

void initialize() {
    logger = spdlog::stdout_color_st("CLOCKGEN");

    std::memset(&ctx, 0, sizeof(ctx));
}

void soft_reset() {
    
}

void hard_reset() {
    soft_reset();
}

void shutdown() {

}

std::vector<u8> transmit(const u8 length) {
    std::vector<u8> data;

    while (data.size() <= length) {
        read_reg(data);
    }
    
    return data;
}

void receive(const std::vector<u8>& data) {
    assert(!data.empty());

    ctx.reg_addr = data[0];

    for (u64 i = 1; i < data.size();) {
        i += write_reg(&data[i]);
    }
}

};
