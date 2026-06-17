/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/systime.hpp - System timer (48 MHz) registers */

#pragma once

namespace kanacore::hw::systime {

void initialize();
void soft_reset();
void hard_reset();
void shutdown();

};
