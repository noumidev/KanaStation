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

const common::f32* get_world_matrix();
const common::f32* get_view_matrix();
const common::f32* get_perspective_matrix();

};
