/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/spi.hpp - ARM PrimeCell PL022 SPI controller for SysCon */

#pragma once

#include <common/types.hpp>

namespace kanacore::hw::spi {

void initialize();
void soft_reset();
void hard_reset();
void shutdown();

void start_reception();
void end_reception();
void receive(const common::u16 data);

};
