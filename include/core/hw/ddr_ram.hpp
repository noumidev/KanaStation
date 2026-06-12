/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/ddr_ram.cpp - DDR memory (32 MB) */

#pragma once

namespace kanacore::hw::ddr_ram {

void initialize();
void soft_reset();
void hard_reset();
void shutdown();

};
