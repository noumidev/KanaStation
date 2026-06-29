/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/kanacore.hpp - Emulator core */

#pragma once

#include <common/types.hpp>
#include <core/config.hpp>
#include <core/hw/bus.hpp>
#include <core/hw/allegrex/allegrex.hpp>

namespace kanacore {

enum Button {
    BUTTON_UP       = 1 << 0,
    BUTTON_RIGHT    = 1 << 1,
    BUTTON_DOWN     = 1 << 2,
    BUTTON_LEFT     = 1 << 3,
    BUTTON_TRIANGLE = 1 << 4,
    BUTTON_CIRCLE   = 1 << 5,
    BUTTON_CROSS    = 1 << 6,
    BUTTON_SQUARE   = 1 << 7,
    BUTTON_SELECT   = 1 << 8,
    BUTTON_L        = 1 << 9,
    BUTTON_R        = 1 << 10,
    BUTTON_START    = 1 << 11,
    BUTTON_HOME     = 1 << 16,
    BUTTON_HOLD     = 1 << 17,
    BUTTON_WLAN_SW  = 1 << 18,
    BUTTON_REMOTE   = 1 << 19,
    BUTTON_VOL_UP   = 1 << 20,
    BUTTON_VOL_DOWN = 1 << 21,
    BUTTON_SCREEN   = 1 << 22,
    BUTTON_NOTE     = 1 << 23,
    BUTTON_UMD      = 1 << 24,
    BUTTON_MS       = 1 << 25,
};

void initialize(const Configuration config);
void soft_reset();
void hard_reset();
void shutdown();

hw::allegrex::Allegrex* get_sc_ptr();
hw::allegrex::Allegrex* get_me_ptr();

hw::bus::Bus* get_sc_bus_ptr();
hw::bus::Bus* get_me_bus_ptr();

void press_button(const Button button);
void release_button(const Button button);

common::u32 get_button_state();

bool run();

};
