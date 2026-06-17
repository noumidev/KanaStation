/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/audio.hpp - Audio interface */

#pragma once

#include <common/types.hpp>

namespace kanacore::hw::audio {

void initialize();
void soft_reset();
void hard_reset();
void shutdown();

};
