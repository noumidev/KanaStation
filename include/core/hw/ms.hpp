/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/ms.hpp - Memory Stick interface 0 */

#pragma once

namespace kanacore::hw::ms {

void initialize(const char* ms_path);
void soft_reset();
void hard_reset();
void shutdown();

};
