/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/atapi.hpp - ATAPI for UMD */

#pragma once

#include <vector>

#include <common/types.hpp>

namespace kanacore::hw::atapi {

void initialize(const char* umd_path);
void soft_reset();
void hard_reset();
void shutdown();

void assert_transfer_end_interrupt();

void umd_initialize(const int);
void read_sectors(std::vector<common::u8>& sector_bytes);

};
