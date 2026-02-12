/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/nand.hpp - NAND interface */

#pragma once

namespace kanacore::hw::nand {

void initialize(const char* nand_path);
void soft_reset();
void hard_reset();
void shutdown();

};
