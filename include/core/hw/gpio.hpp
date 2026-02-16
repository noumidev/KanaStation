/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/gpio.hpp - General-purpose I/O */

#pragma once

namespace kanacore::hw::gpio {

void initialize();
void soft_reset();
void hard_reset();
void shutdown();

};
