/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/sysctrl.hpp - System control registers */

#pragma once

#include <common/types.hpp>

namespace kanacore::hw::sysctrl {

void initialize();
void soft_reset();
void hard_reset();
void shutdown();

common::u32 get_gpio_enable();

common::u64 get_fuseid();

void set_ms0_connected();
void clear_ms0_connected();

};
