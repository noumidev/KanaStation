/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/shared_ram.hpp - SC & ME shared RAM (2 MB) */

#pragma once

namespace kanacore::hw::shared_ram {

void initialize();
void soft_reset();
void hard_reset();
void shutdown();

};
