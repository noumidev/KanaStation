/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/dmac.hpp - ARM PrimeCell PL080 DMA controllers */

#pragma once

namespace kanacore::hw::dmac {

void initialize();
void soft_reset();
void hard_reset();
void shutdown();

void assert_audio_dma_request();
void clear_audio_dma_request();

void assert_ms_dma_request();
void clear_ms_dma_request();

};
