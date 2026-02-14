/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/kanacore.hpp - Emulator core */

#pragma once

#include <core/config.hpp>
#include <core/hw/allegrex/allegrex.hpp>

namespace kanacore {

void initialize(const Configuration config);
void soft_reset();
void hard_reset();
void shutdown();

hw::allegrex::Allegrex* get_sc_ptr();

void run();

};
