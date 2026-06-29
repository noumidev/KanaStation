/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/dmacplus.hpp - DMACplus */

#pragma once

#include <common/types.hpp>

namespace kanacore::hw::dmacplus {

void initialize();
void soft_reset();
void hard_reset();
void shutdown();

void scanout();

common::u8* get_framebuffer_ptr();

};
