/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/clockdiv.hpp - CPU and bus clock divider */

#pragma once

namespace kanacore::hw::clockdiv {

void initialize();
void soft_reset();
void hard_reset();
void shutdown();

};
