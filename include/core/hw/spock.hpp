/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/spock.hpp - SPOCK crypto engine */

#pragma once

namespace kanacore::hw::spock {

void initialize();
void soft_reset();
void hard_reset();
void shutdown();

};
