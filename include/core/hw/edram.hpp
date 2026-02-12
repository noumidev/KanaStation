/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/edram.hpp - Graphics Engine EDRAM (2 MB) */

#pragma once

// Move this to a "ge::" namespace
namespace kanacore::hw::edram {

void initialize();
void soft_reset();
void hard_reset();
void shutdown();

};
