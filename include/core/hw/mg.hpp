/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/mg.hpp - MagicGate crypto engine */

#pragma once

namespace kanacore::hw::mg {

void initialize();
void soft_reset();
void hard_reset();
void shutdown();

};
