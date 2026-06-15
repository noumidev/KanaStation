/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/allegrex/interpreter.hpp - ALLEGREX interpreter */

#pragma once

#include <common/types.hpp>
#include <core/hw/allegrex/allegrex.hpp>

namespace kanacore::hw::allegrex::interpreter {

void initialize();
void soft_reset();
void hard_reset();
void shutdown();

void run(Allegrex* cpu, const common::i64 target_timestamp);

};
