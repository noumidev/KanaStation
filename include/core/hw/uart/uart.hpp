/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/uart/uart.hpp - ARM PrimeCell PL011 UART controller */

#pragma once

namespace kanacore::hw::uart {

void initialize();
void soft_reset();
void hard_reset();
void shutdown();

};
