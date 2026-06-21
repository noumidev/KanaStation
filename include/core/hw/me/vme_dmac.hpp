/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/me/dmac.hpp - Virtual Mobile Engine DMA controller */

#pragma once

namespace kanacore::hw::me::vme_dmac {

void initialize();
void soft_reset();
void hard_reset();
void shutdown();

};
