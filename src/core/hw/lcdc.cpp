/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/lcdc.cpp - LCD controller */

#include <core/hw/lcdc.hpp>

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <common/types.hpp>
#include <core/kanacore.hpp>
#include <core/scheduler.hpp>
#include <core/hw/bus.hpp>
#include <core/hw/display.hpp>
#include <core/hw/dmacplus.hpp>

namespace kanacore::hw::lcdc {

using namespace common;

constexpr u64 LCDC_ADDR = 0x1E140000;
constexpr u64 LCDC_SIZE = 0x1000;

enum IoAddress {
    IO_ADDRESS_CONTROL  = LCDC_ADDR + 0x000,
    IO_ADDRESS_SYNCDIFF = LCDC_ADDR + 0x004,
    IO_ADDRESS_MODE     = LCDC_ADDR + 0x008,
    IO_ADDRESS_XBP      = LCDC_ADDR + 0x010,
    IO_ADDRESS_XSYNC    = LCDC_ADDR + 0x014,
    IO_ADDRESS_XFP      = LCDC_ADDR + 0x018,
    IO_ADDRESS_XRES     = LCDC_ADDR + 0x01C,
    IO_ADDRESS_YBP      = LCDC_ADDR + 0x020,
    IO_ADDRESS_YSYNC    = LCDC_ADDR + 0x024,
    IO_ADDRESS_YFP      = LCDC_ADDR + 0x028,
    IO_ADDRESS_YRES     = LCDC_ADDR + 0x02C,
    IO_ADDRESS_YSHIFT   = LCDC_ADDR + 0x040,
    IO_ADDRESS_XSHIFT   = LCDC_ADDR + 0x044,
    IO_ADDRESS_XSRES    = LCDC_ADDR + 0x048,
    IO_ADDRESS_YSRES    = LCDC_ADDR + 0x04C,
};

#define HW_LCDC_CONTROL  ctx.control
#define HW_LCDC_SYNCDIFF ctx.sync_difference
#define HW_LCDC_MODE     ctx.mode
#define HW_LCDC_XBP      ctx.x.back_porch
#define HW_LCDC_XSYNC    ctx.x.sync_width
#define HW_LCDC_XFP      ctx.x.front_porch
#define HW_LCDC_XRES     ctx.x.resolution
#define HW_LCDC_YBP      ctx.y.back_porch
#define HW_LCDC_YSYNC    ctx.y.sync_width
#define HW_LCDC_YFP      ctx.y.front_porch
#define HW_LCDC_YRES     ctx.y.resolution
#define HW_LCDC_YSHIFT   ctx.y.shift
#define HW_LCDC_XSHIFT   ctx.x.shift
#define HW_LCDC_XSRES    ctx.x.scaled_resolution
#define HW_LCDC_YSRES    ctx.y.scaled_resolution

static std::shared_ptr<spdlog::logger> logger;

static struct {
    u32 control;
    u32 sync_difference;
    u32 mode;

    struct {
        u32 back_porch;
        u32 sync_width;
        u32 front_porch;
        u32 resolution, scaled_resolution;
        u32 shift;
    } x, y;

    u32 scanline;
} ctx;

static void reschedule_hsync(const bool regs_dirty = false);

static void hsync(const int) {
    ctx.scanline++;

    display::hsync();

    const u32 actdisp_start = HW_LCDC_YSYNC + HW_LCDC_YBP;
    const u32 actdisp_end   = actdisp_start + HW_LCDC_YRES;

    const u32 total_scanlines = actdisp_end + HW_LCDC_YFP;

    if (ctx.scanline < actdisp_start) {
        // VBLANK
    } else if ((ctx.scanline >= actdisp_start) && (ctx.scanline < actdisp_end)) {
        // Active display
        // We could scan out the framebuffer line-by-line here...
    } else if (ctx.scanline == actdisp_end) {
        // Start of VBLANK
        display::vsync();
        dmacplus::scanout();
    } else if (ctx.scanline >= total_scanlines) {
        // End of frame
        ctx.scanline = 0;
    }

    reschedule_hsync();
}

static void reschedule_hsync(const bool regs_dirty) {
    static u32 event_id = scheduler::NO_EVENT_ID;

    if (event_id == scheduler::NO_EVENT_ID) {
        event_id = scheduler::register_event("LCDC_HSYNC");
    }

    if (regs_dirty) {
        ctx.scanline = 0;

        scheduler::cancel_event(event_id);
    }

    if (((HW_LCDC_CONTROL & 3) != 3) || (HW_LCDC_XRES == 0)) {
        return;
    }

    scheduler::schedule_event(
        event_id,
        hsync,
        0,
        scheduler::to_scheduler_cycles(
            scheduler::PIXEL_CLOCKRATE,
            // While I want to emulate vertical timings more accurately, we
            // don't really need accurate HBLANKs
            HW_LCDC_XBP + HW_LCDC_XSYNC + HW_LCDC_XFP + HW_LCDC_XRES
        )
    );
}

static u32 read(const u32 addr) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_CONTROL:
            logger->debug("CONTROL read32");
            return HW_LCDC_CONTROL;
        case IoAddress::IO_ADDRESS_SYNCDIFF:
            logger->debug("SYNCDIFF read32");
            return HW_LCDC_SYNCDIFF;
        case IoAddress::IO_ADDRESS_MODE:
            logger->debug("MODE read32");
            return HW_LCDC_MODE;
        case IoAddress::IO_ADDRESS_XBP:
            logger->debug("XBP read32");
            return HW_LCDC_XBP;
        case IoAddress::IO_ADDRESS_XSYNC:
            logger->debug("XSYNC read32");
            return HW_LCDC_XSYNC;
        case IoAddress::IO_ADDRESS_XFP:
            logger->debug("XFP read32");
            return HW_LCDC_XFP;
        case IoAddress::IO_ADDRESS_XRES:
            logger->debug("XRES read32");
            return HW_LCDC_XRES;
        case IoAddress::IO_ADDRESS_YBP:
            logger->debug("YBP read32");
            return HW_LCDC_YBP;
        case IoAddress::IO_ADDRESS_YSYNC:
            logger->debug("YSYNC read32");
            return HW_LCDC_YSYNC;
        case IoAddress::IO_ADDRESS_YFP:
            logger->debug("YFP read32");
            return HW_LCDC_YFP;
        case IoAddress::IO_ADDRESS_YRES:
            logger->debug("YRES read32");
            return HW_LCDC_YRES;
        case IoAddress::IO_ADDRESS_YSHIFT:
            logger->debug("YSHIFT read32");
            return HW_LCDC_YSHIFT;
        case IoAddress::IO_ADDRESS_XSHIFT:
            logger->debug("XSHIFT read32");
            return HW_LCDC_XSHIFT;
        case IoAddress::IO_ADDRESS_XSRES:
            logger->debug("XSRES read32");
            return HW_LCDC_XSRES;
        case IoAddress::IO_ADDRESS_YSRES:
            logger->debug("YSRES read32");
            return HW_LCDC_YSRES;
        default:
            logger->error("Unmapped read32 @ {:08X}", addr);
            exit(1);
    }
}

static void write(const u32 addr, const u32 data) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_CONTROL:
            logger->debug("CONTROL write32 = {:08X}", data);
            
            HW_LCDC_CONTROL = data;
            break;
        case IoAddress::IO_ADDRESS_SYNCDIFF:
            logger->debug("SYNCDIFF write32 = {:08X}", data);

            HW_LCDC_SYNCDIFF = data;
            break;
        case IoAddress::IO_ADDRESS_MODE:
            logger->debug("MODE write32 = {:08X}", data);

            HW_LCDC_MODE = data;
            break;
        case IoAddress::IO_ADDRESS_XBP:
            logger->debug("XBP write32 = {:08X}", data);

            HW_LCDC_XBP = data;
            break;
        case IoAddress::IO_ADDRESS_XSYNC:
            logger->debug("XSYNC write32 = {:08X}", data);

            HW_LCDC_XSYNC = data;
            break;
        case IoAddress::IO_ADDRESS_XFP:
            logger->debug("XFP write32 = {:08X}", data);

            HW_LCDC_XFP = data;
            break;
        case IoAddress::IO_ADDRESS_XRES:
            logger->debug("XRES write32 = {:08X}", data);

            assert(data == 480);

            HW_LCDC_XRES = data;
            break;
        case IoAddress::IO_ADDRESS_YBP:
            logger->debug("YBP write32 = {:08X}", data);

            HW_LCDC_YBP = data;
            break;
        case IoAddress::IO_ADDRESS_YSYNC:
            logger->debug("YSYNC write32 = {:08X}", data);

            HW_LCDC_YSYNC = data;
            break;
        case IoAddress::IO_ADDRESS_YFP:
            logger->debug("YFP write32 = {:08X}", data);

            HW_LCDC_YFP = data;
            break;
        case IoAddress::IO_ADDRESS_YRES:
            logger->debug("YRES write32 = {:08X}", data);

            assert(data == 272);

            HW_LCDC_YRES = data;
            break;
        case IoAddress::IO_ADDRESS_YSHIFT:
            logger->debug("YSHIFT write32 = {:08X}", data);

            HW_LCDC_YSHIFT = data;
            break;
        case IoAddress::IO_ADDRESS_XSHIFT:
            logger->debug("XSHIFT write32 = {:08X}", data);

            HW_LCDC_XSHIFT = data;
            break;
        case IoAddress::IO_ADDRESS_XSRES:
            logger->debug("XSRES write32 = {:08X}", data);

            HW_LCDC_XSRES = data;
            break;
        case IoAddress::IO_ADDRESS_YSRES:
            logger->debug("YSRES write32 = {:08X}", data);

            HW_LCDC_YSRES = data;
            break;
        case LCDC_ADDR + 0x070:
            logger->warn("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            break;
        default:
            logger->error("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            exit(1);
    }

    reschedule_hsync(true);
}

void initialize() {
    logger = spdlog::stdout_color_st("LCDC");

    std::memset(&ctx, 0, sizeof(ctx));
}

void soft_reset() {
    // Set sane defaults
    HW_LCDC_XBP   = 0x29;
    HW_LCDC_XSYNC = 2;
    HW_LCDC_XFP   = 2;
    HW_LCDC_XRES  = 0x1E0;
    HW_LCDC_YBP   = 2;
    HW_LCDC_YSYNC = 2;
    HW_LCDC_YFP   = 10;
    HW_LCDC_YRES  = 0x110;
}

void hard_reset() {
    const bus::PageDescriptor page_desc {
        // To my knowledge, LCDC I/O is never not read/written using 32-bit accesses
        .read32_func  = read,
        .write32_func = write,
    };

    kanacore::get_sc_bus_ptr()->map(LCDC_ADDR, LCDC_SIZE, page_desc);

    soft_reset();
}

void shutdown() {

}

};
