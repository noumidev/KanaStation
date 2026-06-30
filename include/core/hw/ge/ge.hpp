/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/ge/ge.hpp - GraphicsEngine interface */

#pragma once

#include <common/types.hpp>

namespace kanacore::hw::ge {

void initialize();
void soft_reset();
void hard_reset();
void shutdown();

};
