/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/lcdc.hpp - LCD controller */

#pragma once

namespace kanacore::hw::lcdc {

void initialize();
void soft_reset();
void hard_reset();
void shutdown();

};
