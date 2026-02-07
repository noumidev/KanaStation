/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* frontends/sdl/main.cpp - SDL3 frontend */

#include <spdlog/spdlog.h>

#include <core/config.hpp>
#include <core/kanacore.hpp>

// Replace this with SDL_App* functions later on
int main(const int argc, const char** argv) {
    // All KanaCore loggers rely on this
    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%n] [%^%l%$] %v");

    kanacore::Configuration config = kanacore::parse_args(argc, argv);
    
    kanacore::initialize(config);
    kanacore::hard_reset();
    kanacore::run();
    kanacore::shutdown();

    return 0;
}
