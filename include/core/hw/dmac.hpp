/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/dmac.hpp - ARM PrimeCell PL080 DMA controllers */

#pragma once

namespace kanacore::hw::dmac {

void initialize();
void soft_reset();
void hard_reset();
void shutdown();

};
