/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/i2c/codec.hpp - WM8750 CODEC */

#pragma once

#include <vector>

#include <common/types.hpp>

namespace kanacore::hw::i2c::codec {

void initialize();
void soft_reset();
void hard_reset();
void shutdown();

std::vector<common::u8> transmit(const common::u8 length);
void receive(const std::vector<common::u8>& data);

};
