/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/sysctrl.hpp - System control registers */

#pragma once

namespace kanacore::hw::sysctrl {

void initialize();
void soft_reset();
void hard_reset();
void shutdown();

};
