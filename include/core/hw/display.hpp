/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/display.hpp - Display timer? */

#pragma once

namespace kanacore::hw::display {

void initialize();
void soft_reset();
void hard_reset();
void shutdown();

void hsync();
void vsync();

};
