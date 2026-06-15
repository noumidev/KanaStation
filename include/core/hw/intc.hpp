/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/intc.hpp - Interrupt controller registers */

#pragma once

#include <common/types.hpp>

namespace kanacore::hw::intc {

void initialize();
void soft_reset();
void hard_reset();
void shutdown();

void assert_interrupt(const int intr_num);

};
