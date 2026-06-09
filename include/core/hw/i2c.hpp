/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/i2c.hpp - I2C controller */

#pragma once

#include <common/types.hpp>

namespace kanacore::hw::i2c {

void initialize();
void soft_reset();
void hard_reset();
void shutdown();

};
