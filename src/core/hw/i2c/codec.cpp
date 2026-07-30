/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/i2c/codec.cpp - WM8750 CODEC */

#include <core/hw/i2c/codec.hpp>

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <core/kanacore.hpp>

namespace kanacore::hw::i2c::codec {

using namespace common;

static struct {
    // TODO: implement CODEC registers properly
    u16 regs[0x80];
} ctx;

static std::shared_ptr<spdlog::logger> logger;

void initialize() {
    logger = spdlog::stdout_color_st("CODEC");

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
    (void)length;

    // Unsure if you can even read these registers
    logger->error("Unimplemented transmission");
    exit(1);
}

void receive(const std::vector<u8>& data) {
    assert(data.size() == 2);

    const u8 reg_addr = data[0] >> 1;
    const u16 reg_data = ((data[0] & 1) << 8) | data[1];
    
    logger->warn("Unimplemented register write {:02X} = {:03X}", reg_addr, reg_data);

    ctx.regs[reg_addr] = reg_data;
}

};
