/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/gpio.hpp - General-purpose I/O */

#pragma once

#include <common/types.hpp>

namespace kanacore::hw::gpio {

enum Pin {
    PIN_LCD                = 0,
    PIN_SYSCON_NOTIFY      = 3,
    PIN_SYSCON_ACKNOWLEDGE = 4,
    PIN_SYSCON_HP_ENABLE   = 5,
    PIN_SYSCON_MS_LED      = 6,
    PIN_SYSCON_WLAN_LED    = 7,
    PIN_LEPTON_ALIVE       = 24,
    PIN_LEPTON_WAKEUP      = 25,
    PIN_UMD_INSERTED       = 26,
    PIN_NUM                = 32,
};

void initialize();
void soft_reset();
void hard_reset();
void shutdown();

void set_pin(const common::u32 pin);
void clear_pin(const common::u32 pin);

};
