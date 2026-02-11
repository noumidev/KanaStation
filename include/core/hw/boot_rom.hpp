/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/boot_rom.hpp - Boot ROM */

#pragma once

namespace kanacore::hw::boot_rom {

void initialize(const char* boot_path);
void soft_reset();
void hard_reset();
void shutdown();

};
