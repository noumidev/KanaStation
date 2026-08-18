/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/nand.hpp - NAND interface */

#pragma once

#include <core/config.hpp>

namespace kanacore::hw::nand {

void initialize(const Configuration config);
void soft_reset();
void hard_reset();
void shutdown();

};
