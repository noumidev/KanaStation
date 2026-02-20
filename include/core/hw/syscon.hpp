/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/syscon.hpp - SysCon HLE */

#pragma once

#include <common/types.hpp>

namespace kanacore::hw::syscon {

void initialize();
void soft_reset();
void hard_reset();
void shutdown();

// GPIO pin 3
void set_notify();
void clear_notify();

void receive(const common::u16 data);

};
