/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/spi.hpp - ARM PrimeCell PL022 SPI controller for SysCon */

#pragma once

namespace kanacore::hw::spi {

void initialize();
void soft_reset();
void hard_reset();
void shutdown();

};
