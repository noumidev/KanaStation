/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/kirk.hpp - KIRK crypto engine */

#pragma once

namespace kanacore::hw::kirk {

void initialize();
void soft_reset();
void hard_reset();
void shutdown();

};
