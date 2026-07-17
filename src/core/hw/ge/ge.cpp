/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/ge/ge.cpp - GraphicsEngine interface */

#include <core/hw/ge/ge.hpp>

#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <core/kanacore.hpp>
#include <core/hw/bus.hpp>
#include <core/hw/intc.hpp>
#include <core/hw/ge/rasterizer.hpp>

namespace kanacore::hw::ge {

using namespace common;

constexpr u64 GE_ADDR = 0x1D400000;
constexpr u64 GE_SIZE = 0x1000;

constexpr int GE_INTERRUPT = 25;

constexpr u64 NUM_COMMANDS = 0x100;
constexpr u64 NUM_BONES = 8;

constexpr u64 BONE_SIZE  = 12;
constexpr u64 WORLD_SIZE = 12;
constexpr u64 VIEW_SIZE  = 12;
constexpr u64 PROJ_SIZE  = 16;
constexpr u64 TGEN_SIZE  = 12;

enum IoAddress {
    IO_ADDRESS_HWSIZE    = GE_ADDR + 0x008,
    IO_ADDRESS_LISTSTAT  = GE_ADDR + 0x100,
    IO_ADDRESS_LISTADDR  = GE_ADDR + 0x108,
    IO_ADDRESS_STALLADDR = GE_ADDR + 0x10C,
    IO_ADDRESS_LINKADDR0 = GE_ADDR + 0x110,
    IO_ADDRESS_LINKADDR1 = GE_ADDR + 0x114,
    IO_ADDRESS_VTXADDR   = GE_ADDR + 0x118,
    IO_ADDRESS_IDXADDR   = GE_ADDR + 0x11C,
    IO_ADDRESS_ORGADDR0  = GE_ADDR + 0x120,
    IO_ADDRESS_ORGADDR1  = GE_ADDR + 0x124,
    IO_ADDRESS_ORGADDR2  = GE_ADDR + 0x128,
    IO_ADDRESS_CMDSTAT   = GE_ADDR + 0x304,
    IO_ADDRESS_INTRSTAT  = GE_ADDR + 0x308,
    IO_ADDRESS_INTRSWAP  = GE_ADDR + 0x30C,
    IO_ADDRESS_CMDSWAP   = GE_ADDR + 0x310,
    IO_ADDRESS_EDRAMSIZE = GE_ADDR + 0x400,
    IO_ADDRESS_COMMAND   = GE_ADDR + 0x800,
    IO_ADDRESS_BONEMTX   = GE_ADDR + 0xC00,
};

#define HW_GE_LISTSTAT  ctx.list_status
#define HW_GE_LISTADDR  ctx.list_addr
#define HW_GE_STALLADDR ctx.stall_addr
#define HW_GE_LINKADDR0 ctx.link_addrs[0]
#define HW_GE_LINKADDR1 ctx.link_addrs[1]
#define HW_GE_ORGADDR0  ctx.origin_addrs[0]
#define HW_GE_ORGADDR1  ctx.origin_addrs[1]
#define HW_GE_ORGADDR2  ctx.origin_addrs[2]
#define HW_GE_CMDSTAT   ctx.command_status
#define HW_GE_INTRSTAT  ctx.interrupt_status
#define HW_GE_EDRAMSIZE ctx.edram_size

enum GeCommand {
    GE_COMMAND_NOP       = 0x00,
    GE_COMMAND_VADR      = 0x01,
    GE_COMMAND_IADR      = 0x02,
    GE_COMMAND_PRIM      = 0x04,
    GE_COMMAND_BEZIER    = 0x05,
    GE_COMMAND_SPLINE    = 0x06,
    GE_COMMAND_JUMP      = 0x08,
    GE_COMMAND_END       = 0x0C,
    GE_COMMAND_FINISH    = 0x0F,
    GE_COMMAND_BASE      = 0x10,
    GE_COMMAND_VTYPE     = 0x12,
    GE_COMMAND_OFFSET    = 0x13,
    GE_COMMAND_ORIGIN    = 0x14,
    GE_COMMAND_REGION1   = 0x15,
    GE_COMMAND_REGION2   = 0x16,
    GE_COMMAND_LTE       = 0x17,
    GE_COMMAND_LE0       = 0x18,
    GE_COMMAND_LE1       = 0x19,
    GE_COMMAND_LE2       = 0x1A,
    GE_COMMAND_LE3       = 0x1B,
    GE_COMMAND_CLE       = 0x1C,
    GE_COMMAND_BCE       = 0x1D,
    GE_COMMAND_TME       = 0x1E,
    GE_COMMAND_FGE       = 0x1F,
    GE_COMMAND_DTE       = 0x20,
    GE_COMMAND_ABE       = 0x21,
    GE_COMMAND_ATE       = 0x22,
    GE_COMMAND_ZTE       = 0x23,
    GE_COMMAND_STE       = 0x24,
    GE_COMMAND_AAE       = 0x25,
    GE_COMMAND_PCE       = 0x26,
    GE_COMMAND_CTE       = 0x27,
    GE_COMMAND_LOE       = 0x28,
    GE_COMMAND_BONEN     = 0x2A,
    GE_COMMAND_BONED     = 0x2B,
    GE_COMMAND_WEIGHT0   = 0x2C,
    GE_COMMAND_WEIGHT1   = 0x2D,
    GE_COMMAND_WEIGHT2   = 0x2E,
    GE_COMMAND_WEIGHT3   = 0x2F,
    GE_COMMAND_WEIGHT4   = 0x30,
    GE_COMMAND_WEIGHT5   = 0x31,
    GE_COMMAND_WEIGHT6   = 0x32,
    GE_COMMAND_WEIGHT7   = 0x33,
    GE_COMMAND_DIVIDE    = 0x36,
    GE_COMMAND_PPM       = 0x37,
    GE_COMMAND_PFACE     = 0x38,
    GE_COMMAND_WORLDN    = 0x3A,
    GE_COMMAND_WORLDD    = 0x3B,
    GE_COMMAND_VIEWN     = 0x3C,
    GE_COMMAND_VIEWD     = 0x3D,
    GE_COMMAND_PROJN     = 0x3E,
    GE_COMMAND_PROJD     = 0x3F,
    GE_COMMAND_TGENN     = 0x40,
    GE_COMMAND_TGEND     = 0x41,
    GE_COMMAND_SX        = 0x42,
    GE_COMMAND_SY        = 0x43,
    GE_COMMAND_SZ        = 0x44,
    GE_COMMAND_TX        = 0x45,
    GE_COMMAND_TY        = 0x46,
    GE_COMMAND_TZ        = 0x47,
    GE_COMMAND_SU        = 0x48,
    GE_COMMAND_SV        = 0x49,
    GE_COMMAND_TU        = 0x4A,
    GE_COMMAND_TV        = 0x4B,
    GE_COMMAND_OFFSETX   = 0x4C,
    GE_COMMAND_OFFSETY   = 0x4D,
    GE_COMMAND_SHADE     = 0x50,
    GE_COMMAND_NREV      = 0x51,
    GE_COMMAND_MATERIAL  = 0x53,
    GE_COMMAND_MEC       = 0x54,
    GE_COMMAND_MAC       = 0x55,
    GE_COMMAND_MDC       = 0x56,
    GE_COMMAND_MSC       = 0x57,
    GE_COMMAND_MAA       = 0x58,
    GE_COMMAND_MK        = 0x5B,
    GE_COMMAND_AC        = 0x5C,
    GE_COMMAND_AA        = 0x5D,
    GE_COMMAND_LMODE     = 0x5E,
    GE_COMMAND_LTYPE0    = 0x5F,
    GE_COMMAND_LTYPE1    = 0x60,
    GE_COMMAND_LTYPE2    = 0x61,
    GE_COMMAND_LTYPE3    = 0x62,
    GE_COMMAND_LX0       = 0x63,
    GE_COMMAND_LY0       = 0x64,
    GE_COMMAND_LZ0       = 0x65,
    GE_COMMAND_LX1       = 0x66,
    GE_COMMAND_LY1       = 0x67,
    GE_COMMAND_LZ1       = 0x68,
    GE_COMMAND_LX2       = 0x69,
    GE_COMMAND_LY2       = 0x6A,
    GE_COMMAND_LZ2       = 0x6B,
    GE_COMMAND_LX3       = 0x6C,
    GE_COMMAND_LY3       = 0x6D,
    GE_COMMAND_LZ3       = 0x6E,
    GE_COMMAND_LDX0      = 0x6F,
    GE_COMMAND_LDY0      = 0x70,
    GE_COMMAND_LDZ0      = 0x71,
    GE_COMMAND_LDX1      = 0x72,
    GE_COMMAND_LDY1      = 0x73,
    GE_COMMAND_LDZ1      = 0x74,
    GE_COMMAND_LDX2      = 0x75,
    GE_COMMAND_LDY2      = 0x76,
    GE_COMMAND_LDZ2      = 0x77,
    GE_COMMAND_LDX3      = 0x78,
    GE_COMMAND_LDY3      = 0x79,
    GE_COMMAND_LDZ3      = 0x7A,
    GE_COMMAND_LKA0      = 0x7B,
    GE_COMMAND_LKB0      = 0x7C,
    GE_COMMAND_LKC0      = 0x7D,
    GE_COMMAND_LKA1      = 0x7E,
    GE_COMMAND_LKB1      = 0x7F,
    GE_COMMAND_LKC1      = 0x80,
    GE_COMMAND_LKA2      = 0x81,
    GE_COMMAND_LKB2      = 0x82,
    GE_COMMAND_LKC2      = 0x83,
    GE_COMMAND_LKA3      = 0x84,
    GE_COMMAND_LKB3      = 0x85,
    GE_COMMAND_LKC3      = 0x86,
    GE_COMMAND_LKS0      = 0x87,
    GE_COMMAND_LKS1      = 0x88,
    GE_COMMAND_LKS2      = 0x89,
    GE_COMMAND_LKS3      = 0x8A,
    GE_COMMAND_LKO0      = 0x8B,
    GE_COMMAND_LKO1      = 0x8C,
    GE_COMMAND_LKO2      = 0x8D,
    GE_COMMAND_LKO3      = 0x8E,
    GE_COMMAND_LAC0      = 0x8F,
    GE_COMMAND_LDC0      = 0x90,
    GE_COMMAND_LSC0      = 0x91,
    GE_COMMAND_LAC1      = 0x92,
    GE_COMMAND_LDC1      = 0x93,
    GE_COMMAND_LSC1      = 0x94,
    GE_COMMAND_LAC2      = 0x95,
    GE_COMMAND_LDC2      = 0x96,
    GE_COMMAND_LSC2      = 0x97,
    GE_COMMAND_LAC3      = 0x98,
    GE_COMMAND_LDC3      = 0x99,
    GE_COMMAND_LSC3      = 0x9A,
    GE_COMMAND_CULL      = 0x9B,
    GE_COMMAND_FBP       = 0x9C,
    GE_COMMAND_FBW       = 0x9D,
    GE_COMMAND_ZBP       = 0x9E,
    GE_COMMAND_ZBW       = 0x9F,
    GE_COMMAND_TBP0      = 0xA0,
    GE_COMMAND_TBP1      = 0xA1,
    GE_COMMAND_TBP2      = 0xA2,
    GE_COMMAND_TBP3      = 0xA3,
    GE_COMMAND_TBP4      = 0xA4,
    GE_COMMAND_TBP5      = 0xA5,
    GE_COMMAND_TBP6      = 0xA6,
    GE_COMMAND_TBP7      = 0xA7,
    GE_COMMAND_TBW0      = 0xA8,
    GE_COMMAND_TBW1      = 0xA9,
    GE_COMMAND_TBW2      = 0xAA,
    GE_COMMAND_TBW3      = 0xAB,
    GE_COMMAND_TBW4      = 0xAC,
    GE_COMMAND_TBW5      = 0xAD,
    GE_COMMAND_TBW6      = 0xAE,
    GE_COMMAND_TBW7      = 0xAF,
    GE_COMMAND_CBP       = 0xB0,
    GE_COMMAND_CBW       = 0xB1,
    GE_COMMAND_XBP1      = 0xB2,
    GE_COMMAND_XBW1      = 0xB3,
    GE_COMMAND_XBP2      = 0xB4,
    GE_COMMAND_XBW2      = 0xB5,
    GE_COMMAND_TSIZE0    = 0xB8,
    GE_COMMAND_TSIZE1    = 0xB9,
    GE_COMMAND_TSIZE2    = 0xBA,
    GE_COMMAND_TSIZE3    = 0xBB,
    GE_COMMAND_TSIZE4    = 0xBC,
    GE_COMMAND_TSIZE5    = 0xBD,
    GE_COMMAND_TSIZE6    = 0xBE,
    GE_COMMAND_TSIZE7    = 0xBF,
    GE_COMMAND_TMAP      = 0xC0,
    GE_COMMAND_TSHADE    = 0xC1,
    GE_COMMAND_TMODE     = 0xC2,
    GE_COMMAND_TPF       = 0xC3,
    GE_COMMAND_CLOAD     = 0xC4,
    GE_COMMAND_CLUT      = 0xC5,
    GE_COMMAND_TFILTER   = 0xC6,
    GE_COMMAND_TWRAP     = 0xC7,
    GE_COMMAND_TLEVEL    = 0xC8,
    GE_COMMAND_TFUNC     = 0xC9,
    GE_COMMAND_TEC       = 0xCA,
    GE_COMMAND_TFLUSH    = 0xCB,
    GE_COMMAND_TSYNC     = 0xCC,
    GE_COMMAND_FOG1      = 0xCD,
    GE_COMMAND_FOG2      = 0xCE,
    GE_COMMAND_FC        = 0xCF,
    GE_COMMAND_TSLOPE    = 0xD0,
    GE_COMMAND_FPF       = 0xD2,
    GE_COMMAND_CMODE     = 0xD3,
    GE_COMMAND_SCISSOR1  = 0xD4,
    GE_COMMAND_SCISSOR2  = 0xD5,
    GE_COMMAND_MINZ      = 0xD6,
    GE_COMMAND_MAXZ      = 0xD7,
    GE_COMMAND_CTEST     = 0xD8,
    GE_COMMAND_CREF      = 0xD9,
    GE_COMMAND_CMSK      = 0xDA,
    GE_COMMAND_ATEST     = 0xDB,
    GE_COMMAND_STEST     = 0xDC,
    GE_COMMAND_SOP       = 0xDD,
    GE_COMMAND_ZTEST     = 0xDE,
    GE_COMMAND_BLEND     = 0xDF,
    GE_COMMAND_FIXA      = 0xE0,
    GE_COMMAND_FIXB      = 0xE1,
    GE_COMMAND_DITH1     = 0xE2,
    GE_COMMAND_DITH2     = 0xE3,
    GE_COMMAND_DITH3     = 0xE4,
    GE_COMMAND_DITH4     = 0xE5,
    GE_COMMAND_LOP       = 0xE6,
    GE_COMMAND_ZMSK      = 0xE7,
    GE_COMMAND_PMSK1     = 0xE8,
    GE_COMMAND_PMSK2     = 0xE9,
    GE_COMMAND_XPOS1     = 0xEB,
    GE_COMMAND_XPOS2     = 0xEC,
    GE_COMMAND_XSIZE     = 0xEE,
    GE_COMMAND_DUMMY     = 0xFF,
};

union ListCommand {
    u32 raw;
    
    struct {
        u32 param   : 24;
        u32 command : 8;
    };
};

static std::array<ListCommand, NUM_COMMANDS> commands;

static struct {
    union {
        u32 raw;

        struct {
            u32 busy      : 1;
            u32 condition : 1;
            u32           : 6;
            u32 depth     : 2;
            u32           : 22;
        };
    } list_status;

    u32 list_addr;
    u32 stall_addr;
    u32 link_addrs[2];
    u32 offset_addr;
    u32 origin_addrs[3];
    u32 command_status;
    u32 interrupt_status;
    u32 edram_size;

    struct {
        u32 idx;
        f32 data[NUM_BONES][BONE_SIZE];
    } bone_matrices;

    struct {
        u32 idx;
        f32 data[WORLD_SIZE];
    } world_matrix;

    struct {
        u32 idx;
        f32 data[VIEW_SIZE];
    } view_matrix;

    struct {
        u32 idx;
        f32 data[VIEW_SIZE];
    } perspective_matrix;

    struct {
        u32 idx;
        f32 data[TGEN_SIZE];
    } texgen_matrix;
} ctx;

static std::shared_ptr<spdlog::logger> logger;

static void check_pending_interrupts() {
    if (HW_GE_INTRSTAT != 0) {
        intc::assert_sc_interrupt(GE_INTERRUPT);
    } else {
        intc::clear_sc_interrupt(GE_INTERRUPT);
    }
}

static void assert_interrupt(const int intr_num) {
    HW_GE_INTRSTAT |= 1 << intr_num;
    HW_GE_CMDSTAT  |= 1 << intr_num;

    check_pending_interrupts();
}

static void start_list_exec() {
    bus::Bus* bus = kanacore::get_sc_bus_ptr();

    if (!HW_GE_LISTSTAT.busy) {
        logger->debug("List processing disabled");
        return;
    }

    while ((HW_GE_STALLADDR == 0) || (HW_GE_LISTADDR < HW_GE_STALLADDR)) {
        const ListCommand list_command = { .raw = bus->read<u32>(HW_GE_LISTADDR) };

        // Update command array
        commands[list_command.command] = list_command;

        HW_GE_LISTADDR += sizeof(list_command);

        switch (list_command.command) {
            case GeCommand::GE_COMMAND_NOP:
                logger->debug("NOP");
                break;
            case GeCommand::GE_COMMAND_VADR:
                rasterizer::set_vertex_addr(list_command.param | rasterizer::get_base());

                logger->debug("VADR (address: {:08X})", rasterizer::get_vertex_addr());
                break;
            case GeCommand::GE_COMMAND_IADR:
                rasterizer::set_index_addr(list_command.param | rasterizer::get_base());

                logger->debug("IADR (address: {:08X})", rasterizer::get_index_addr());
                break;
            case GeCommand::GE_COMMAND_PRIM: {
                const u32 count = (list_command.param >> 0) & 0xFFFF;
                const u32 prim_type = (list_command.param >> 16) & 7;

                logger->debug("PRIM (count: {})", count);

                rasterizer::draw_primitive(count, prim_type);
                break;
            }
            case GeCommand::GE_COMMAND_BEZIER: {
                const u32 u_count = (list_command.param >> 0) & 0xFF;
                const u32 v_count = (list_command.param >> 8) & 0xFF;

                logger->debug("BEZIER (U count: {}, V count: {})", u_count, v_count);

                rasterizer::draw_bezier(u_count, v_count);
                break;
            }
            case GeCommand::GE_COMMAND_SPLINE: {
                const u32 u_count = (list_command.param >> 0) & 0xFF;
                const u32 v_count = (list_command.param >> 8) & 0xFF;

                const u32 u_knot_type = (list_command.param >> 16) & 3;
                const u32 v_knot_type = (list_command.param >> 18) & 3;

                logger->debug("SPLINE (U count: {}, V count: {})", list_command.param & 0xFF, (list_command.param >> 8) & 0xFF);
                
                rasterizer::draw_spline(u_count, v_count, u_knot_type, v_knot_type);
                break;
            }
            case GeCommand::GE_COMMAND_JUMP:
                HW_GE_LISTADDR = rasterizer::get_base() + list_command.param;

                logger->debug("JUMP (address: {:08X})", HW_GE_LISTADDR);
                break;
            case GeCommand::GE_COMMAND_END:
                logger->debug("END");

                HW_GE_LISTSTAT.busy = false;

                assert_interrupt(1);
                return;
            case GeCommand::GE_COMMAND_FINISH:
                logger->debug("FINISH");

                assert_interrupt(2);
                break;;
            case GeCommand::GE_COMMAND_BASE:
                rasterizer::set_base((list_command.param & 0xFF0000) << 8);

                logger->debug("BASE (address: {:08X})", rasterizer::get_base());
                break;
            case GeCommand::GE_COMMAND_VTYPE:
                logger->debug("VTYPE");
                rasterizer::set_vertex_type(list_command.param);
                break;
            case GeCommand::GE_COMMAND_OFFSET:
                ctx.offset_addr = list_command.param << 8;

                logger->debug("OFFSET (address: {:08X})", ctx.offset_addr);
                break;
            case GeCommand::GE_COMMAND_ORIGIN:
                ctx.offset_addr = HW_GE_LISTADDR - sizeof(u32);
                ctx.origin_addrs[0] = ctx.offset_addr;

                logger->debug("ORIGIN (address: {:08X})", ctx.offset_addr);
                break;
            case GeCommand::GE_COMMAND_REGION1:
                logger->debug("REGION1");
                rasterizer::set_region_upper(list_command.param);
                break;
            case GeCommand::GE_COMMAND_REGION2:
                logger->debug("REGION2");
                rasterizer::set_region_lower(list_command.param);
                break;
            case GeCommand::GE_COMMAND_LTE:
                logger->debug("LTE");
                rasterizer::set_lighting_enable((list_command.param & 1) != 0);
                break;
            case GeCommand::GE_COMMAND_LE0:
            case GeCommand::GE_COMMAND_LE1:
            case GeCommand::GE_COMMAND_LE2:
            case GeCommand::GE_COMMAND_LE3: {
                const int idx = list_command.command - GeCommand::GE_COMMAND_LE0;

                logger->debug("LE{}", idx);
                rasterizer::set_light_enable(idx, (list_command.param & 1) != 0);
                break;
            }
            case GeCommand::GE_COMMAND_CLE:
                logger->debug("CLE");
                rasterizer::set_clipping_enable((list_command.param & 1) != 0);
                break;
            case GeCommand::GE_COMMAND_BCE:
                logger->debug("BCE");
                rasterizer::set_backface_culling_enable((list_command.param & 1) != 0);
                break;
            case GeCommand::GE_COMMAND_TME:
                logger->debug("TME");
                rasterizer::set_texture_mapping_enable((list_command.param & 1) != 0);
                break;
            case GeCommand::GE_COMMAND_FGE:
                logger->debug("FGE");
                rasterizer::set_fogging_enable((list_command.param & 1) != 0);
                break;
            case GeCommand::GE_COMMAND_DTE:
                logger->debug("DTE");
                rasterizer::set_dithering_enable((list_command.param & 1) != 0);
                break;
            case GeCommand::GE_COMMAND_ABE:
                logger->debug("ABE");
                rasterizer::set_alpha_blending_enable((list_command.param & 1) != 0);
                break;
            case GeCommand::GE_COMMAND_ATE:
                logger->debug("ATE");
                rasterizer::set_alpha_test_enable((list_command.param & 1) != 0);
                break;
            case GeCommand::GE_COMMAND_ZTE:
                logger->debug("ZTE");
                rasterizer::set_depth_test_enable((list_command.param & 1) != 0);
                break;
            case GeCommand::GE_COMMAND_STE:
                logger->debug("STE");
                rasterizer::set_stencil_test_enable((list_command.param & 1) != 0);
                break;
            case GeCommand::GE_COMMAND_AAE:
                logger->debug("AAE");
                rasterizer::set_antialiasing_enable((list_command.param & 1) != 0);
                break;
            case GeCommand::GE_COMMAND_PCE:
                logger->debug("PCE");
                rasterizer::set_patch_culling_enable((list_command.param & 1) != 0);
                break;
            case GeCommand::GE_COMMAND_CTE:
                logger->debug("CTE");
                rasterizer::set_color_test_enable((list_command.param & 1) != 0);
                break;
            case GeCommand::GE_COMMAND_LOE:
                logger->debug("LOE");
                rasterizer::set_logic_operation_enable((list_command.param & 1) != 0);
                break;
            case GeCommand::GE_COMMAND_BONEN:
                logger->debug("BONEN");

                ctx.bone_matrices.idx = list_command.param & 0x3F;
                break;
            case GeCommand::GE_COMMAND_BONED: {
                assert(ctx.bone_matrices.idx < (NUM_BONES * BONE_SIZE));

                const u32 bone_num = ctx.bone_matrices.idx / 12;
                const u32 bone_idx = ctx.bone_matrices.idx % 12;

                ctx.bone_matrices.data[bone_num][bone_idx] = from_u32(list_command.param << 8);
                ctx.bone_matrices.idx++;

                logger->debug("BONED (BONE{}{}: {})", (char)('A' + bone_num), bone_idx, ctx.bone_matrices.data[bone_num][bone_idx]);
                break;
            }
            case GeCommand::GE_COMMAND_WEIGHT0:
            case GeCommand::GE_COMMAND_WEIGHT1:
            case GeCommand::GE_COMMAND_WEIGHT2:
            case GeCommand::GE_COMMAND_WEIGHT3:
            case GeCommand::GE_COMMAND_WEIGHT4:
            case GeCommand::GE_COMMAND_WEIGHT5:
            case GeCommand::GE_COMMAND_WEIGHT6:
            case GeCommand::GE_COMMAND_WEIGHT7: {
                const int idx = list_command.command - GeCommand::GE_COMMAND_WEIGHT0;

                logger->debug("WEIGHT{}", idx);
                rasterizer::set_morph_weight(idx, from_u32(list_command.param << 8));
                break;
            }
            case GeCommand::GE_COMMAND_DIVIDE: {
                const u32 u_div = (list_command.param >> 0) & 0x7F;
                const u32 v_div = (list_command.param >> 8) & 0x7F;

                logger->debug("DIVIDE (U division: {}, V division: {})", u_div, v_div);
                rasterizer::set_patch_division(u_div, v_div);
                break;
            }
            case GeCommand::GE_COMMAND_PPM:
                logger->debug("PPM");
                rasterizer::set_patch_primitive(list_command.param & 3);
                break;
            case GeCommand::GE_COMMAND_PFACE:
                logger->debug("PFACE");
                rasterizer::set_patch_face((list_command.param & 1) != 0);
                break;
            case GeCommand::GE_COMMAND_WORLDN:
                logger->debug("WORLDN");

                ctx.world_matrix.idx = list_command.param & 0xF;
                break;
            case GeCommand::GE_COMMAND_WORLDD:
                assert(ctx.world_matrix.idx < WORLD_SIZE);

                ctx.world_matrix.data[ctx.world_matrix.idx++] = from_u32(list_command.param << 8);

                logger->debug("WORLDD (WORLD{}: {})", ctx.world_matrix.idx - 1, ctx.world_matrix.data[ctx.world_matrix.idx - 1]);
                break;
            case GeCommand::GE_COMMAND_VIEWN:
                logger->debug("VIEWN");

                ctx.view_matrix.idx = list_command.param & 0xF;
                break;
            case GeCommand::GE_COMMAND_VIEWD:
                assert(ctx.view_matrix.idx < VIEW_SIZE);

                ctx.view_matrix.data[ctx.view_matrix.idx++] = from_u32(list_command.param << 8);

                logger->debug("VIEWD (VIEW{}: {})", ctx.view_matrix.idx - 1, ctx.view_matrix.data[ctx.view_matrix.idx - 1]);
                break;
            case GeCommand::GE_COMMAND_PROJN:
                logger->debug("PROJN");

                ctx.perspective_matrix.idx = list_command.param & 0xF;
                break;
            case GeCommand::GE_COMMAND_PROJD:
                assert(ctx.perspective_matrix.idx < PROJ_SIZE);

                ctx.perspective_matrix.data[ctx.perspective_matrix.idx++] = from_u32(list_command.param << 8);

                logger->debug("PROJD (PROJ{}: {})", ctx.perspective_matrix.idx - 1, ctx.perspective_matrix.data[ctx.perspective_matrix.idx - 1]);
                break;
            case GeCommand::GE_COMMAND_TGENN:
                logger->debug("TGENN");

                ctx.texgen_matrix.idx = list_command.param & 0xF;
                break;
            case GeCommand::GE_COMMAND_TGEND:
                assert(ctx.texgen_matrix.idx < TGEN_SIZE);

                ctx.texgen_matrix.data[ctx.texgen_matrix.idx++] = from_u32(list_command.param << 8);

                logger->debug("TGEND (TGEN{}: {})", ctx.texgen_matrix.idx - 1, ctx.texgen_matrix.data[ctx.texgen_matrix.idx - 1]);
                break;
            case GeCommand::GE_COMMAND_SX:
            case GeCommand::GE_COMMAND_SY:
            case GeCommand::GE_COMMAND_SZ: {
                const int idx = list_command.command - GeCommand::GE_COMMAND_SX;

                logger->debug("S{}", (char)('X' + idx));
                rasterizer::set_viewport_scale(idx, from_u32(list_command.param << 8));
                break;
            }
            case GeCommand::GE_COMMAND_TX:
            case GeCommand::GE_COMMAND_TY:
            case GeCommand::GE_COMMAND_TZ: {
                const int idx = list_command.command - GeCommand::GE_COMMAND_TX;

                logger->debug("T{}", (char)('X' + idx));
                rasterizer::set_viewport_offset(idx, from_u32(list_command.param << 8));
                break;
            }
            case GeCommand::GE_COMMAND_SU:
            case GeCommand::GE_COMMAND_SV: {
                const int idx = list_command.command - GeCommand::GE_COMMAND_SU;

                logger->debug("S{}", (char)('U' + idx));
                rasterizer::set_texture_scale(idx, from_u32(list_command.param << 8));
                break;
            }
            case GeCommand::GE_COMMAND_TU:
            case GeCommand::GE_COMMAND_TV: {
                const int idx = list_command.command - GeCommand::GE_COMMAND_TU;

                logger->debug("T{}", (char)('U' + idx));
                rasterizer::set_texture_offset(idx, from_u32(list_command.param << 8));
                break;
            }
            case GeCommand::GE_COMMAND_OFFSETX:
                logger->debug("OFFSETX");
                rasterizer::set_offset_x((f32)(u16)list_command.param / 16);
                break;
            case GeCommand::GE_COMMAND_OFFSETY:
                logger->debug("OFFSETY");
                rasterizer::set_offset_y((f32)(u16)list_command.param / 16);
                break;
            case GeCommand::GE_COMMAND_SHADE:
                logger->debug("SHADE");
                rasterizer::set_gouraud_shading_enable((list_command.param & 1) != 0);
                break;
            case GeCommand::GE_COMMAND_MEC:
            case GeCommand::GE_COMMAND_MAC:
            case GeCommand::GE_COMMAND_MDC:
            case GeCommand::GE_COMMAND_MSC: {
                constexpr char MODEL_COLOR[] = { 'E', 'A', 'D', 'S' };

                const int idx = list_command.command - GeCommand::GE_COMMAND_MEC;

                logger->debug("M{}C: {:06X}", MODEL_COLOR[idx], list_command.param);
                rasterizer::set_model_color(idx, list_command.param);
                break;
            }
            case GeCommand::GE_COMMAND_MAA:
                logger->debug("MAA: {:02X}", list_command.param & 0xFF);
                rasterizer::set_model_alpha(list_command.param & 0xFF);
                break;
            case GeCommand::GE_COMMAND_AC:
                logger->debug("AC: {:06X}", list_command.param);
                rasterizer::set_ambient_color(list_command.param);
                break;
            case GeCommand::GE_COMMAND_AA:
                logger->debug("AA: {:02X}", list_command.param & 0xFF);
                rasterizer::set_ambient_alpha(list_command.param & 0xFF);
                break;
            case GeCommand::GE_COMMAND_FBP:
                logger->debug("FBP (address: {:06X})", list_command.param);
                rasterizer::set_framebuffer_base(list_command.param);
                break;
            case GeCommand::GE_COMMAND_FBW: {
                const u32 addr_hi = (list_command.param & 0xFF0000) << 8;
                const u32 width = list_command.param & 0x3C0;

                logger->debug("FBW (address: {:08X}, width: {})", addr_hi, width);
                rasterizer::set_framebuffer_width(width);
                break;
            }
            case GeCommand::GE_COMMAND_ZBP:
                logger->debug("ZBP (address: {:06X})", list_command.param);
                rasterizer::set_depth_buffer_base(list_command.param);
                break;
            case GeCommand::GE_COMMAND_ZBW: {
                const u32 addr_hi = (list_command.param & 0xFF0000) << 8;
                const u32 width = list_command.param & 0x3C0;

                logger->debug("ZBW (address: {:08X}, width: {})", addr_hi, width);
                rasterizer::set_depth_buffer_width(width);
                break;
            }
            case GeCommand::GE_COMMAND_TBP0:
            case GeCommand::GE_COMMAND_TBP1:
            case GeCommand::GE_COMMAND_TBP2:
            case GeCommand::GE_COMMAND_TBP3:
            case GeCommand::GE_COMMAND_TBP4:
            case GeCommand::GE_COMMAND_TBP5:
            case GeCommand::GE_COMMAND_TBP6:
            case GeCommand::GE_COMMAND_TBP7: {
                const int idx = list_command.command - GeCommand::GE_COMMAND_TBP0;

                logger->debug("TBP{} (address: {:06X})", idx, list_command.param);
                rasterizer::set_texture_base(idx, list_command.param);
                break;
            }
            case GeCommand::GE_COMMAND_TBW0:
            case GeCommand::GE_COMMAND_TBW1:
            case GeCommand::GE_COMMAND_TBW2:
            case GeCommand::GE_COMMAND_TBW3:
            case GeCommand::GE_COMMAND_TBW4:
            case GeCommand::GE_COMMAND_TBW5:
            case GeCommand::GE_COMMAND_TBW6:
            case GeCommand::GE_COMMAND_TBW7: {
                const int idx = list_command.command - GeCommand::GE_COMMAND_TBW0;

                const u32 addr_hi = (list_command.param & 0xFF0000) << 8;
                const u32 width = list_command.param & 0x3FF;

                logger->debug("TBW{} (address: {:08X}, width: {})", idx, addr_hi, width);
                rasterizer::set_texture_buffer_width(idx, addr_hi, width);
                break;
            }
            case GeCommand::GE_COMMAND_CBP:
                logger->debug("CBP (address: {:06X})", list_command.param);
                rasterizer::set_clut_base_lo(list_command.param);
                break;
            case GeCommand::GE_COMMAND_CBW: {
                const u32 addr_hi = (list_command.param & 0xFF0000) << 8;

                logger->debug("CBW (address: {:08X})", addr_hi);
                rasterizer::set_clut_base_hi(addr_hi);
                break;
            }
            case GeCommand::GE_COMMAND_TSIZE0:
            case GeCommand::GE_COMMAND_TSIZE1:
            case GeCommand::GE_COMMAND_TSIZE2:
            case GeCommand::GE_COMMAND_TSIZE3:
            case GeCommand::GE_COMMAND_TSIZE4:
            case GeCommand::GE_COMMAND_TSIZE5:
            case GeCommand::GE_COMMAND_TSIZE6:
            case GeCommand::GE_COMMAND_TSIZE7: {
                const int idx = list_command.command - GeCommand::GE_COMMAND_TSIZE0;

                const u32 width  = (list_command.param >> 0) & 0xF;
                const u32 height = (list_command.param >> 8) & 0xF;

                logger->debug("TSIZE{} (width: {}, height: {})", idx, width, height);
                rasterizer::set_texture_size(idx, width, height);
                break;
            }
            case GeCommand::GE_COMMAND_TMAP:
                logger->debug("TMAP");

                assert((list_command.param & 3) != 1);
                break;
            case GeCommand::GE_COMMAND_TPF:
                logger->debug("TPF");
                rasterizer::set_texture_format(list_command.param & 0xF);
                break;
            case GeCommand::GE_COMMAND_CLOAD: {
                const u32 num_palettes = list_command.param & 0x3F;

                logger->debug("CLOAD (NP: {})", num_palettes);
                rasterizer::load_clut(num_palettes);
                break;
            }
            case GeCommand::GE_COMMAND_CLUT:
                logger->debug("CLUT");
                rasterizer::set_clut(list_command.param);
                break;
            case GeCommand::GE_COMMAND_TFUNC:
                logger->debug("TFUNC");
                rasterizer::set_texture_blend_params(list_command.param);
                break;
            case GeCommand::GE_COMMAND_TFLUSH:
                logger->debug("TFLUSH");
                break;
            case GeCommand::GE_COMMAND_TSYNC:
                logger->debug("TSYNC");
                break;
            case GeCommand::GE_COMMAND_SCISSOR1:
                logger->debug("SCISSOR1");
                rasterizer::set_scissor_upper(list_command.param);
                break;
            case GeCommand::GE_COMMAND_SCISSOR2:
                logger->debug("SCISSOR2");
                rasterizer::set_scissor_lower(list_command.param);
                break;
            case GeCommand::GE_COMMAND_MINZ:
                logger->debug("MINZ");
                rasterizer::set_minimum_depth(list_command.param & 0xFFFF);
                break;
            case GeCommand::GE_COMMAND_MAXZ:
                logger->debug("MAXZ");
                rasterizer::set_maximum_depth(list_command.param & 0xFFFF);
                break;
            case GeCommand::GE_COMMAND_ATEST:
                logger->debug("ATEST");
                rasterizer::set_alpha_test(list_command.param);
                break;
            case GeCommand::GE_COMMAND_ZTEST:
                logger->debug("ZTEST");
                rasterizer::set_depth_test(list_command.param & 7);
                break;
            case GeCommand::GE_COMMAND_BLEND:
                logger->debug("BLEND");
                rasterizer::set_blend_params(list_command.param);
                break;
            case GeCommand::GE_COMMAND_FIXA:
                logger->debug("FIXA");
                rasterizer::set_fixed_color_a(list_command.param);
                break;
            case GeCommand::GE_COMMAND_FIXB:
                logger->debug("FIXB");
                rasterizer::set_fixed_color_b(list_command.param);
                break;
            case GeCommand::GE_COMMAND_PMSK1:
                logger->debug("PMSK1");
                rasterizer::set_color_mask(list_command.param);
                break;
            case GeCommand::GE_COMMAND_PMSK2:
                logger->debug("PMSK2");
                rasterizer::set_alpha_mask(list_command.param & 0xFF);
                break;
            case GeCommand::GE_COMMAND_DUMMY:
                logger->debug("DUMMY");
                break;
            default:
                logger->warn("Unimplemented command {:02X} ({:08X})", list_command.command, list_command.raw);
                break;
        }
    }
}

static u32 read(const u32 addr) {
    constexpr u32 HWSIZE = 0x200000 >> 10;

    if ((addr >= IoAddress::IO_ADDRESS_COMMAND) && (addr < IoAddress::IO_ADDRESS_BONEMTX)) {
        const u32 idx = (addr - IoAddress::IO_ADDRESS_COMMAND) / sizeof(u32);

        logger->debug("COMMAND{:02X} read32", idx);
        return commands[idx].raw;
    }

    switch (addr) {
        case IoAddress::IO_ADDRESS_HWSIZE:
            logger->debug("HWSIZE read32");
            return HWSIZE;
        case IoAddress::IO_ADDRESS_LISTSTAT:
            logger->debug("LISTSTAT read32");
            return HW_GE_LISTSTAT.raw;
        case IoAddress::IO_ADDRESS_LISTADDR:
            logger->debug("LISTADDR read32");
            return HW_GE_LISTADDR;
        case IoAddress::IO_ADDRESS_STALLADDR:
            logger->debug("STALLADDR read32");
            return HW_GE_STALLADDR;
        case IoAddress::IO_ADDRESS_LINKADDR0:
            logger->debug("LINKADDR0 read32");
            return HW_GE_LINKADDR0;
        case IoAddress::IO_ADDRESS_LINKADDR1:
            logger->debug("LINKADDR1 read32");
            return HW_GE_LINKADDR1;
        case IoAddress::IO_ADDRESS_VTXADDR:
            logger->debug("VTXADDR read32");
            return rasterizer::get_vertex_addr();
        case IoAddress::IO_ADDRESS_IDXADDR:
            logger->debug("IDXADDR read32");
            return rasterizer::get_index_addr();
        case IoAddress::IO_ADDRESS_ORGADDR0:
            logger->debug("ORGADDR0 read32");
            return HW_GE_ORGADDR0;
        case IoAddress::IO_ADDRESS_ORGADDR1:
            logger->debug("ORGADDR1 read32");
            return HW_GE_ORGADDR1;
        case IoAddress::IO_ADDRESS_ORGADDR2:
            logger->debug("ORGADDR2 read32");
            return HW_GE_ORGADDR2;
        case IoAddress::IO_ADDRESS_CMDSTAT:
            logger->debug("CMDSTAT read32");
            return HW_GE_CMDSTAT;
        case IoAddress::IO_ADDRESS_INTRSTAT:
            logger->debug("INTRSTAT read32");
            return HW_GE_INTRSTAT;
        case IoAddress::IO_ADDRESS_EDRAMSIZE:
            logger->debug("EDRAMSIZE read32");
            return HW_GE_EDRAMSIZE;
        case GE_ADDR + 0x004:
            logger->warn("Unmapped read32 @ {:08X}", addr);
            return 0;
        default:
            logger->warn("Unmapped read32 @ {:08X}", addr);
            exit(1);
    }
}

static void write(const u32 addr, const u32 data) {
    switch (addr) {
        case IoAddress::IO_ADDRESS_LISTSTAT:
            logger->debug("LISTSTAT write32 = {:08X}", data);

            HW_GE_LISTSTAT.raw = data;

            start_list_exec();
            break;
        case IoAddress::IO_ADDRESS_LISTADDR:
            logger->debug("LISTADDR write32 = {:08X}", data);

            HW_GE_LISTADDR = data;

            start_list_exec();
            break;
        case IoAddress::IO_ADDRESS_STALLADDR:
            logger->debug("STALLADDR write32 = {:08X}", data);

            HW_GE_STALLADDR = data;

            start_list_exec();
            break;
        case IoAddress::IO_ADDRESS_LINKADDR0:
            logger->debug("LINKADDR0 write32 = {:08X}", data);

            HW_GE_LINKADDR0 = data;
            break;
        case IoAddress::IO_ADDRESS_LINKADDR1:
            logger->debug("LINKADDR1 write32 = {:08X}", data);

            HW_GE_LINKADDR1 = data;
            break;
        case IoAddress::IO_ADDRESS_VTXADDR:
            logger->debug("VTXADDR write32 = {:08X}", data);
            rasterizer::set_vertex_addr(data);
            break;
        case IoAddress::IO_ADDRESS_IDXADDR:
            logger->debug("IDXADDR write32 = {:08X}", data);
            rasterizer::set_index_addr(data);
            break;
        case IoAddress::IO_ADDRESS_ORGADDR0:
            logger->debug("ORGADDR0 write32 = {:08X}", data);

            HW_GE_ORGADDR0 = data;
            break;
        case IoAddress::IO_ADDRESS_ORGADDR1:
            logger->debug("ORGADDR1 write32 = {:08X}", data);

            HW_GE_ORGADDR1 = data;
            break;
        case IoAddress::IO_ADDRESS_ORGADDR2:
            logger->debug("ORGADDR2 write32 = {:08X}", data);

            HW_GE_ORGADDR2 = data;
            break;
        case IoAddress::IO_ADDRESS_INTRSTAT:
            logger->debug("INTRSTAT write32 = {:08X}", data);

            HW_GE_INTRSTAT &= ~data;

            check_pending_interrupts();
            break;
        case IoAddress::IO_ADDRESS_INTRSWAP:
            logger->debug("INTRSWAP write32 = {:08X}", data);

            HW_GE_INTRSTAT ^= data;

            check_pending_interrupts();
            break;
        case IoAddress::IO_ADDRESS_CMDSWAP:
            logger->debug("CMDSWAP write32 = {:08X}", data);

            HW_GE_CMDSTAT  ^= data;
            HW_GE_INTRSTAT  = HW_GE_CMDSTAT;

            check_pending_interrupts();
            break;
        case IoAddress::IO_ADDRESS_EDRAMSIZE:
            logger->debug("EDRAMSIZE write32 = {:08X}", data);

            HW_GE_EDRAMSIZE = data;
            break;
        default:
            logger->warn("Unmapped write32 @ {:08X} = {:08X}", addr, data);
            exit(1);
    }
}

void initialize() {
    logger = spdlog::stdout_color_st("GE");

    std::memset(&ctx, 0, sizeof(ctx));
}

void soft_reset() {
    
}

void hard_reset() {
    const bus::PageDescriptor page_desc {
        // To my knowledge, GE I/F I/O is never not read/written using 32-bit accesses
        .read32_func  = read,
        .write32_func = write,
    };

    kanacore::get_sc_bus_ptr()->map(GE_ADDR, GE_SIZE, page_desc);
}

void shutdown() {

}

const f32* get_world_matrix() {
    return ctx.world_matrix.data;
}

const f32* get_view_matrix() {
    return ctx.view_matrix.data;
}

const f32* get_perspective_matrix() {
    return ctx.perspective_matrix.data;
}

};
