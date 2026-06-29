/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* frontends/sdl/main.cpp - SDL3 frontend */

#define SDL_MAIN_USE_CALLBACKS

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <spdlog/spdlog.h>

#include <common/types.hpp>
#include <core/config.hpp>
#include <core/kanacore.hpp>
#include <core/hw/dmacplus.hpp>

using namespace common;

constexpr int SCREEN_WIDTH  = 480;
constexpr int SCREEN_HEIGHT = 272;

static struct {
    SDL_Renderer* renderer;
    SDL_Window* window;
    SDL_Texture* texture;
} screen;

SDL_AppResult SDL_AppInit(void**, int argc, char** argv) {
    if (
        !SDL_CreateWindowAndRenderer(
            "KanaStation",
            SCREEN_WIDTH,
            SCREEN_HEIGHT,
            0,
            &screen.window,
            &screen.renderer
        )
    ) {
        SDL_Log("Failed to create window and renderer: %s", SDL_GetError());

        return SDL_APP_FAILURE;
    }

    screen.texture = SDL_CreateTexture(
        screen.renderer,
        SDL_PIXELFORMAT_XBGR8888,
        SDL_TEXTUREACCESS_STREAMING,
        SCREEN_WIDTH,
        SCREEN_HEIGHT
    );

    if (screen.texture == nullptr) {
        SDL_Log("Failed to create texture: %s", SDL_GetError());

        return SDL_APP_FAILURE;
    }

    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");

    // All KanaCore loggers rely on this
    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%n] [%^%l%$] %v");

    kanacore::Configuration config = kanacore::parse_args(argc, argv);
    
    kanacore::initialize(config);
    kanacore::hard_reset();

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void*, SDL_Event* event) {
    switch (event->type) {
        case SDL_EVENT_QUIT:
            return SDL_APP_SUCCESS;
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void*) {
    // Run emulator for a frame
    while (kanacore::run()) {}

    SDL_UpdateTexture(screen.texture, nullptr, kanacore::hw::dmacplus::get_framebuffer_ptr(), sizeof(u32) * SCREEN_WIDTH);
    SDL_RenderClear(screen.renderer);
    SDL_RenderTexture(screen.renderer, screen.texture, nullptr, nullptr);
    SDL_RenderPresent(screen.renderer);

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void*, SDL_AppResult) {
    SDL_DestroyTexture(screen.texture);
    SDL_DestroyRenderer(screen.renderer);
    SDL_DestroyWindow(screen.window);

    kanacore::shutdown();
}
