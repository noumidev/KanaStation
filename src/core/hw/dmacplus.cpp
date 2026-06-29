/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/dmacplus.cpp - DMACplus */

#include <core/hw/dmacplus.hpp>

#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <core/kanacore.hpp>
#include <core/hw/bus.hpp>

namespace kanacore::hw::dmacplus {

using namespace common;

constexpr u64 DMACPLUS_ADDR = 0x1C800000;
constexpr u64 DMACPLUS_SIZE = 0x1000;

enum IoAddress {
    IO_ADDRESS_LCDC_FBADDR   = DMACPLUS_ADDR + 0x100,
    IO_ADDRESS_LCDC_FBFORMAT = DMACPLUS_ADDR + 0x104,
    IO_ADDRESS_LCDC_FBWIDTH  = DMACPLUS_ADDR + 0x108,
    IO_ADDRESS_LCDC_FBSTRIDE = DMACPLUS_ADDR + 0x10C,
    IO_ADDRESS_LCDC_FBCTRL   = DMACPLUS_ADDR + 0x110,
};

#define HW_DMACPLUS_LCDC_FBADDR   ctx.framebuffer_addr
#define HW_DMACPLUS_LCDC_FBFORMAT ctx.framebuffer_format
#define HW_DMACPLUS_LCDC_FBWIDTH  ctx.framebuffer_width
#define HW_DMACPLUS_LCDC_FBSTRIDE ctx.framebuffer_stride
#define HW_DMACPLUS_LCDC_FBCTRL   ctx.framebuffer_control

std::array<u32, 480 * 272> framebuffer;

static struct {
    u32 framebuffer_addr;
    u32 framebuffer_format;
    u32 framebuffer_width;
    u32 framebuffer_stride;

    union {
        u32 raw;

        struct {
            u32 enable : 1;
            u32        : 31;
        };
    } framebuffer_control;
} ctx;

static std::shared_ptr<spdlog::logger> logger;

static u32 read(const u32 addr) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_LCDC_FBADDR:
            logger->debug("LCDC_FBADDR read32");
            return HW_DMACPLUS_LCDC_FBADDR;
        case IoAddress::IO_ADDRESS_LCDC_FBFORMAT:
            logger->debug("LCDC_FBFORMAT read32");
            return HW_DMACPLUS_LCDC_FBFORMAT;
        case IoAddress::IO_ADDRESS_LCDC_FBWIDTH:
            logger->debug("LCDC_FBWIDTH read32");
            return HW_DMACPLUS_LCDC_FBWIDTH;
        case IoAddress::IO_ADDRESS_LCDC_FBSTRIDE:
            logger->debug("LCDC_FBSTRIDE read32");
            return HW_DMACPLUS_LCDC_FBSTRIDE;
        case IoAddress::IO_ADDRESS_LCDC_FBCTRL:
            logger->debug("LCDC_FBCTRL read32");
            return HW_DMACPLUS_LCDC_FBCTRL.raw;
        default:
            logger->warn("Unmapped read32 @ {:08X}", addr);
            exit(1);
    }
}

static void write(const u32 addr, const u32 data) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_LCDC_FBADDR:
            logger->debug("LCDC_FBADDR write32 = {:08X}", data);

            HW_DMACPLUS_LCDC_FBADDR = data;
            break;
        case IoAddress::IO_ADDRESS_LCDC_FBFORMAT:
            logger->debug("LCDC_FBFORMAT write32 = {:08X}", data);

            HW_DMACPLUS_LCDC_FBFORMAT = data;
            break;
        case IoAddress::IO_ADDRESS_LCDC_FBWIDTH:
            logger->debug("LCDC_FBWIDTH write32 = {:08X}", data);

            HW_DMACPLUS_LCDC_FBWIDTH = data;
            break;
        case IoAddress::IO_ADDRESS_LCDC_FBSTRIDE:
            logger->debug("LCDC_FBSTRIDE write32 = {:08X}", data);

            HW_DMACPLUS_LCDC_FBSTRIDE = data;
            break;
        case IoAddress::IO_ADDRESS_LCDC_FBCTRL:
            logger->debug("LCDC_FBCTRL write32 = {:08X}", data);

            HW_DMACPLUS_LCDC_FBCTRL.raw = data;
            break;
        default:
            logger->warn("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            exit(1);
    }
}

void initialize() {
    logger = spdlog::stdout_color_st("DMACplus");

    std::memset(&ctx, 0, sizeof(ctx));
}

void soft_reset() {
    
}

void hard_reset() {
    const bus::PageDescriptor page_desc {
        // To my knowledge, DMACplus I/O is never not read/written using 32-bit accesses
        .read32_func  = read,
        .write32_func = write,
    };

    kanacore::get_sc_bus_ptr()->map(DMACPLUS_ADDR, DMACPLUS_SIZE, page_desc);
}

void shutdown() {

}

void scanout() {
    bus::Bus* bus = kanacore::get_sc_bus_ptr();

    logger->debug(
        "Scanout (address: {:08X}, width: {}, stride: {}, format: {}, control: {})",
        HW_DMACPLUS_LCDC_FBADDR,
        HW_DMACPLUS_LCDC_FBWIDTH,
        HW_DMACPLUS_LCDC_FBSTRIDE,
        HW_DMACPLUS_LCDC_FBFORMAT,
        HW_DMACPLUS_LCDC_FBCTRL.raw
    );

    if (!HW_DMACPLUS_LCDC_FBCTRL.enable || (HW_DMACPLUS_LCDC_FBSTRIDE == 0)) {
        framebuffer.fill(0);
        return;
    }

    assert(HW_DMACPLUS_LCDC_FBFORMAT == 0);
    assert(HW_DMACPLUS_LCDC_FBWIDTH  == 480);

    for (u32 y = 0; y < 272; y++) {
        for (u32 x = 0; x < 480; x++) {
            framebuffer[y * 480 + x] = bus->read<u32>(
                HW_DMACPLUS_LCDC_FBADDR + sizeof(u32) * (y * HW_DMACPLUS_LCDC_FBSTRIDE + x)
            );
        }
    }
}

u8* get_framebuffer_ptr() {
    return (u8*)framebuffer.data();
}

};
