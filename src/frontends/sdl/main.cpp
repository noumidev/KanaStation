/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* frontends/sdl/main.cpp - SDL3 frontend */

#define SDL_MAIN_USE_CALLBACKS

#include <unordered_map>

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

static const std::unordered_map<SDL_Scancode, kanacore::Button> key_bindings = {
    { SDL_SCANCODE_W, kanacore::Button::BUTTON_UP       },
    { SDL_SCANCODE_D, kanacore::Button::BUTTON_RIGHT    },
    { SDL_SCANCODE_S, kanacore::Button::BUTTON_DOWN     },
    { SDL_SCANCODE_A, kanacore::Button::BUTTON_LEFT     },
    { SDL_SCANCODE_I, kanacore::Button::BUTTON_TRIANGLE },
    { SDL_SCANCODE_J, kanacore::Button::BUTTON_SQUARE   },
    { SDL_SCANCODE_K, kanacore::Button::BUTTON_CROSS    },
    { SDL_SCANCODE_L, kanacore::Button::BUTTON_CIRCLE   },
    { SDL_SCANCODE_U, kanacore::Button::BUTTON_SELECT   },
    { SDL_SCANCODE_Q, kanacore::Button::BUTTON_L        },
    { SDL_SCANCODE_E, kanacore::Button::BUTTON_R        },
    { SDL_SCANCODE_P, kanacore::Button::BUTTON_START    },
    { SDL_SCANCODE_Z, kanacore::Button::BUTTON_HOME     },
    { SDL_SCANCODE_N, kanacore::Button::BUTTON_HOLD     },
    { SDL_SCANCODE_M, kanacore::Button::BUTTON_WLAN_SW  },
    { SDL_SCANCODE_X, kanacore::Button::BUTTON_VOL_UP   },
    { SDL_SCANCODE_C, kanacore::Button::BUTTON_VOL_DOWN },
    { SDL_SCANCODE_V, kanacore::Button::BUTTON_SCREEN   },
    { SDL_SCANCODE_B, kanacore::Button::BUTTON_NOTE     },
};

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
        case SDL_EVENT_KEY_DOWN: {
            // Ignore key repeats
            if (event->key.repeat) {
                break;
            }

            auto it = key_bindings.find(event->key.scancode);

            if (it != key_bindings.end()) {
                kanacore::press_button(it->second);
            }
            break;
        }
        case SDL_EVENT_KEY_UP: {
            auto it = key_bindings.find(event->key.scancode);

            if (it != key_bindings.end()) {
                kanacore::release_button(it->second);
            }

            break;
        }
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
