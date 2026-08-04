/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/me/sysctrl.hpp - MediaEngine system control registers */

#pragma once

namespace kanacore::hw::me::sysctrl {

void initialize();
void soft_reset();
void hard_reset();
void shutdown();

};
