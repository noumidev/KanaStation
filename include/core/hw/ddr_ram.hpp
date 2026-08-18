/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/ddr_ram.hpp - DDR memory (32 MB) */

#pragma once

#include <core/config.hpp>

namespace kanacore::hw::ddr_ram {

void initialize(const Configuration config);
void soft_reset();
void hard_reset();
void shutdown();

};
