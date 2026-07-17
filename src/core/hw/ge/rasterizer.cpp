/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/ge/ge.cpp - Software rasterizer */

#include <core/hw/ge/rasterizer.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <core/kanacore.hpp>
#include <core/hw/bus.hpp>
#include <core/hw/ge/ge.hpp>

namespace kanacore::hw::ge::rasterizer {

using namespace common;

constexpr u64 NUM_LIGHTS = 4;
constexpr u64 NUM_MORPH_WEIGHTS = 8;
constexpr u64 NUM_VIEWPORT_COORDS = 3;

constexpr const char* VTYPE_TT_NAMES[] = {
    "none", "U8", "U16", "F32",
};

constexpr const char* VTYPE_CT_NAMES[] = {
    "none"  , "N/A"     , "N/A"     , "N/A"     ,
    "RGB565", "RGBA5551", "RGBA4444", "RGBA8888",
};

constexpr const char* VTYPE_NT_NAMES[] = {
    "none", "S8", "S16", "F32",
};

constexpr const char* VTYPE_VT_NAMES[] = {
    "none", "S8", "S16", "F32",
};

constexpr const char* VTYPE_WT_NAMES[] = {
    "none", "U8", "U16", "F32",
};

constexpr const char* VTYPE_IT_NAMES[] = {
    "none", "U8", "U16", "N/A",
};

constexpr const char* PRIMITIVE_NAMES[] = {
    "Point"         , "Line"        , "Line strip", "Triangle",
    "Triangle strip", "Triangle fan", "Rectangle" , "N/A"     ,
};

constexpr const char* PATCH_PRIMITIVE_NAMES[] = {
    "Triangle", "Line", "Point", "N/A",
};

constexpr const char* TEXEL_FORMAT_NAMES[] = {
    "RGB565", "RGBA5551", "RGBA4444", "RGBA8888",
    "IDX4"  , "IDX8"    , "IDX16"   , "IDX32"   ,
    "DXT1"  , "DXT3"    , "DXT5"    , "N/A"     ,
    "N/A"   , "N/A"     , "N/A"     , "N/A"     ,
};

constexpr const char* CLUT_FORMAT_NAMES[] = {
    "RGB565", "RGBA5551", "RGBA4444", "RGBA8888",
};

constexpr const char* BLEND_FUNC_NAMES[] = {
    "Add", "Subtract", "Reverse subtract", "Min",
    "Max", "Abs"     , "N/A"             , "N/A",
};

enum TexcoordType {
    TEXCOORD_TYPE_NONE = 0,
    TEXCOORD_TYPE_FP8  = 1,
    TEXCOORD_TYPE_FP16 = 2,
    TEXCOORD_TYPE_F32  = 3,
};

enum ColorType {
    COLOR_TYPE_NONE     = 0,
    COLOR_TYPE_RGB565   = 4,
    COLOR_TYPE_RGBA5551 = 5,
    COLOR_TYPE_RGBA4444 = 6,
    COLOR_TYPE_RGBA8888 = 7,
};

enum NormalType {
    NORMAL_TYPE_NONE = 0,
    NORMAL_TYPE_FP16 = 2,
    NORMAL_TYPE_F32  = 3,
};

enum ModcoordType {
    MODCOORD_TYPE_NONE = 0,
    MODCOORD_TYPE_FP16 = 2,
    MODCOORD_TYPE_F32  = 3,
};

enum WeightType {
    WEIGHT_TYPE_NONE = 0,
    WEIGHT_TYPE_F32  = 3,
};

enum IndexType {
    INDEX_TYPE_NONE = 0,
    INDEX_TYPE_U8   = 1,
    INDEX_TYPE_U16  = 2,
};

enum PatchPrimitive {
    PATCH_PRIMITIVE_TRIANGLE = 0,
};

enum KnotType {
    KNOT_TYPE_CLOSE_CLOSE = 0,
    KNOT_TYPE_OPEN_CLOSE  = 1,
    KNOT_TYPE_CLOSE_OPEN  = 2,
    KNOT_TYPE_OPEN_OPEN   = 3,
};

enum PrimType {
    PRIM_TYPE_POINT          = 0,
    PRIM_TYPE_LINE           = 1,
    PRIM_TYPE_LINE_STRIP     = 2,
    PRIM_TYPE_TRIANGLE       = 3,
    PRIM_TYPE_TRIANGLE_STRIP = 4,
    PRIM_TYPE_TRIANGLE_FAN   = 5,
    PRIM_TYPE_RECTANGLE      = 6,
};

enum TexelFormat {
    TEXEL_FORMAT_RGB565   = 0,
    TEXEL_FORMAT_RGBA5551 = 1,
    TEXEL_FORMAT_RGBA4444 = 2,
    TEXEL_FORMAT_RGBA8888 = 3,
    TEXEL_FORMAT_IDX4     = 4,
    TEXEL_FORMAT_IDX8     = 5,
    TEXEL_FORMAT_IDX16    = 6,
    TEXEL_FORMAT_IDX32    = 7,
    TEXEL_FORMAT_DXT1     = 8,
    TEXEL_FORMAT_DXT3     = 9,
    TEXEL_FORMAT_DXT5     = 10,
};

enum ClutFormat {
    CLUT_FORMAT_RGB565   = 0,
    CLUT_FORMAT_RGBA5551 = 1,
    CLUT_FORMAT_RGBA4444 = 2,
    CLUT_FORMAT_RGBA8888 = 3,
};

enum Test {
    TEST_NEVER         = 0,
    TEST_ALWAYS        = 1,
    TEST_EQUAL         = 2,
    TEST_NOT_EQUAL     = 3,
    TEST_LESS          = 4,
    TEST_LESS_EQUAL    = 5,
    TEST_GREATER       = 6,
    TEST_GREATER_EQUAL = 7,
};

enum BlendFunc {
    BLEND_FUNC_ADD              = 0,
    BLEND_FUNC_SUBTRACT         = 1,
    BLEND_FUNC_REVERSE_SUBTRACT = 2,
    BLEND_FUNC_MIN              = 3,
    BLEND_FUNC_MAX              = 4,
    BLEND_FUNC_ABS              = 5,
};

enum ModelColor {
    MODEL_COLOR_EMISSION = 0,
    MODEL_COLOR_AMBIENT  = 1,
    MODEL_COLOR_DIFFUSE  = 2,
    MODEL_COLOR_SPECULAR = 3,
};

struct Vertex {
    // For debugging purposes
    u32 addr;

    // Texture coordinates
    f32 s, t;
    bool has_texcoords;

    // Vertex colors
    f32 r, g, b, a;
    bool has_colors;

    f32 nx, ny, nz;
    bool has_normals;

    // Model coordinates
    f32 x, y, z, w;
};

union Color {
    u32 raw;

    struct {
        u8 r;
        u8 g;
        u8 b;
        u8 a;
    };
};

union ClutColor {
    u16 half[2];
    u32 full;
};

static struct {
    u32 base;
    u32 vertex_addr;
    u32 index_addr;

    union {
        u32 raw;

        struct {
            u32 texcoord_type : 2;
            u32 color_type    : 3;
            u32 normal_type   : 2;
            u32 modcoord_type : 2;
            u32 weight_type   : 2;
            u32 index_type    : 2;
            u32               : 1;
            u32 num_weights   : 3;
            u32               : 1;
            u32 num_morph     : 3;
            u32               : 2;
            u32 through_mode  : 1;
            u32               : 8;
        };
    } vertex_type;

    int vertex_size;

    // The drawing region is used for culling
    struct {
        u32 sx1, sx2;
        u32 sy1, sy2;
    } region, scissor;

    // A lot of these don't do anything yet, but they give me some
    // insight as to why graphics look the way they look
    bool lighting_enable;
    bool light_enable[NUM_LIGHTS];
    bool clipping_enable;
    bool backface_culling_enable;
    bool texture_mapping_enable;
    bool fogging_enable;
    bool dithering_enable;
    bool alpha_blending_enable;
    bool alpha_test_enable;
    bool depth_test_enable;
    bool stencil_test_enable;
    bool antialiasing_enable;
    bool patch_culling_enable;
    bool color_test_enable;
    bool logic_operation_enable;
    bool gouraud_shading_enable;

    f32 weights[NUM_MORPH_WEIGHTS];

    struct {
        Color model_colors[4];
        Color ambient_color;

        bool use_vertex_ambient;
        bool use_vertex_diffuse;
        bool use_vertex_specular;

        struct {
            f32 x, y, z;
            f32 dx, dy, dz;
            f32 attenuation_factors[3];
            f32 convergence_factor;
            f32 cutoff_coefficient;

            Color colors[3];
        } light[NUM_LIGHTS];
    } lighting;

    struct {
        u32 u_div, v_div;

        PatchPrimitive primitive;

        bool is_ccw;
    } patch_control;

    f32 viewport_scale[NUM_VIEWPORT_COORDS], viewport_offset[NUM_VIEWPORT_COORDS];
    f32 texture_scale[2], texture_offset[2];

    f32 offset_x, offset_y;

    struct {
        u32 addr;
        u32 width;
    } framebuffer;

    struct {
        u32 addr;
        u32 width;

        u16 minz, maxz;

        bool mask;
    } depth_buffer;

    struct {
        u32 addr;
        u32 buf_width;
        u32 width;
        u32 height;
    } texture[8];

    u32 texture_format;

    struct {
        u32 addr;
        ClutFormat format;

        // Index gen
        u32 shift;
        u32 mask;
        u32 start_addr;

        ClutColor data[256];
    } clut;

    // Pixel tests and parameters
    struct {
        Test test;

        u32 ref, mask;
    } alpha_test;

    Test depth_test;

    u32 alpha_test_reference, alpha_test_mask;

    struct {
        u32 src_input;
        u32 dst_input;
        u32 func;

        Color fixa, fixb;
    } blend_params;

    struct {
        bool use_tex_alpha;
        u32 func;
    } tex_blend_params;
} ctx;

static std::shared_ptr<spdlog::logger> logger;

static inline int align_up(const int data, const int alignment) {
    if ((data & (alignment - 1)) != 0) {
        return (data | (alignment - 1)) + 1;
    }

    return data;
}

static int get_vertex_size() {
    // This function is pretty ugly, that needs to be fixed at some point
    int size = 0;

    bool halfword_align = false;
    bool word_align = false;

    if (!ctx.vertex_type.through_mode) {
        const u32 weight_type = ctx.vertex_type.weight_type;

        size += ((weight_type != WeightType::WEIGHT_TYPE_F32) ? weight_type : 4) * ctx.vertex_type.num_weights;
    }

    const u32 texcoord_type = ctx.vertex_type.texcoord_type;

    if (texcoord_type != TexcoordType::TEXCOORD_TYPE_NONE) {
        if (texcoord_type == TexcoordType::TEXCOORD_TYPE_FP16) {
            size = align_up(size, sizeof(u16));

            halfword_align = true;
        } else if (texcoord_type == TexcoordType::TEXCOORD_TYPE_F32) {
            size = align_up(size, sizeof(u32));

            word_align = true;
        }

        size += 2 * ((texcoord_type != TexcoordType::TEXCOORD_TYPE_F32) ? texcoord_type : 4);
    }

    const u32 color_type = ctx.vertex_type.color_type;

    if (color_type != ColorType::COLOR_TYPE_NONE) {
        if (color_type == ColorType::COLOR_TYPE_RGBA8888) {
            size = align_up(size, sizeof(u32));

            word_align = true;
        } else {
            size = align_up(size, sizeof(u16));

            halfword_align = true;
        }

        size += (color_type == ColorType::COLOR_TYPE_RGBA8888) ? 4 : 2;
    }

    if (!ctx.vertex_type.through_mode) {
        const u32 normal_type = ctx.vertex_type.normal_type;

        if (normal_type == NormalType::NORMAL_TYPE_FP16) {
            size = align_up(size, sizeof(u16));

            halfword_align = true;
        } else if (normal_type == NormalType::NORMAL_TYPE_F32) {
            size = align_up(size, sizeof(u32));

            word_align = true;
        }

        size += 3 * ((normal_type != NormalType::NORMAL_TYPE_F32) ? normal_type : 4);
    }

    const u32 modcoord_type = ctx.vertex_type.modcoord_type;

    if (modcoord_type == ModcoordType::MODCOORD_TYPE_FP16) {
        size = align_up(size, sizeof(u16));

        halfword_align = true;
    } else if (modcoord_type == ModcoordType::MODCOORD_TYPE_F32) {
        size = align_up(size, sizeof(u32));

        word_align = true;
    }

    size += 3 * ((modcoord_type != ModcoordType::MODCOORD_TYPE_F32) ? modcoord_type : 4);

    if (word_align) {
        size = align_up(size, sizeof(u32));
    } else if (halfword_align) {
        size = align_up(size, sizeof(u16));
    }

    return size;
}

void initialize() {
    logger = spdlog::stdout_color_st("Rasterizer");

    std::memset(&ctx, 0, sizeof(ctx));
}

void soft_reset() {
    hard_reset();
}

void hard_reset() {
    ctx.framebuffer.addr  = 0x04000000;
    ctx.depth_buffer.addr = 0x04000000;
}

void shutdown() {

}

void set_base(const u32 data) {
    ctx.base = data;
}

void set_vertex_addr(const u32 data) {
    ctx.vertex_addr = data;
}

void set_index_addr(const u32 data) {
    ctx.index_addr = data;
}

void set_vertex_type(const u32 data) {
    ctx.vertex_type.raw = data;

    logger->debug(
        "New vertex type (TT: {}, CT: {}, NT: {}, VT: {}, WT: {}, IT: {}, WC: {}, MC: {}, TRU: {})",
        VTYPE_TT_NAMES[ctx.vertex_type.texcoord_type],
        VTYPE_CT_NAMES[ctx.vertex_type.color_type],
        VTYPE_NT_NAMES[ctx.vertex_type.normal_type],
        VTYPE_VT_NAMES[ctx.vertex_type.modcoord_type],
        VTYPE_WT_NAMES[ctx.vertex_type.weight_type],
        VTYPE_IT_NAMES[ctx.vertex_type.index_type],
        ctx.vertex_type.num_weights + 1,
        (ctx.vertex_type.num_morph > 0) ? ctx.vertex_type.num_morph + 1 : 0,
        (bool)ctx.vertex_type.through_mode
    );

    // Precalculate the size of a single vertex according to the new format
    ctx.vertex_size = get_vertex_size();

    logger->debug("Vertex size: {}", ctx.vertex_size);
}

void set_region_upper(const u32 data) {
    ctx.region.sx1 = data & 0x3FF;
    ctx.region.sy1 = (data >> 10) & 0x3FF;

    logger->debug(
        "New region (SX1: {}, SX2: {}, SY1: {}, SY2: {})",
        ctx.region.sx1,
        ctx.region.sx2,
        ctx.region.sy1,
        ctx.region.sy2
    );
}

void set_region_lower(const u32 data) {
    ctx.region.sx2 = data & 0x3FF;
    ctx.region.sy2 = (data >> 10) & 0x3FF;

    logger->debug(
        "New region (SX1: {}, SX2: {}, SY1: {}, SY2: {})",
        ctx.region.sx1,
        ctx.region.sx2,
        ctx.region.sy1,
        ctx.region.sy2
    );
}

void set_lighting_enable(const bool enable) {
    ctx.lighting_enable = enable;

    logger->debug("Lighting enabled: {}", enable);
}

void set_light_enable(const u32 idx, const bool enable) {
    assert(idx < NUM_LIGHTS);

    ctx.light_enable[idx] = enable;

    logger->debug("Light {} enabled: {}", idx, enable);
}

void set_clipping_enable(const bool enable) {
    ctx.clipping_enable = enable;

    logger->debug("Clipping enabled: {}", enable);
}

void set_backface_culling_enable(const bool enable) {
    ctx.backface_culling_enable = enable;

    logger->debug("Backface culling enabled: {}", enable);
}

void set_texture_mapping_enable(const bool enable) {
    ctx.texture_mapping_enable = enable;

    logger->debug("Texture mapping enabled: {}", enable);
}

void set_fogging_enable(const bool enable) {
    ctx.fogging_enable = enable;

    logger->debug("Fogging enabled: {}", enable);
}

void set_dithering_enable(const bool enable) {
    ctx.dithering_enable = enable;

    logger->debug("Dithering enabled: {}", enable);
}

void set_alpha_blending_enable(const bool enable) {
    ctx.alpha_blending_enable = enable;

    logger->debug("Alpha blending enabled: {}", enable);
}

void set_alpha_test_enable(const bool enable) {
    ctx.alpha_test_enable = enable;

    logger->debug("Alpha test enabled: {}", enable);
}

void set_depth_test_enable(const bool enable) {
    ctx.depth_test_enable = enable;

    logger->debug("Depth test enabled: {}", enable);
}

void set_stencil_test_enable(const bool enable) {
    ctx.stencil_test_enable = enable;

    logger->debug("Stencil test enabled: {}", enable);

    assert(!enable);
}

void set_antialiasing_enable(const bool enable) {
    ctx.antialiasing_enable = enable;

    logger->debug("Antialiasing enabled: {}", enable);
}

void set_patch_culling_enable(const bool enable) {
    ctx.patch_culling_enable = enable;

    logger->debug("Patch culling enabled: {}", enable);
}

void set_color_test_enable(const bool enable) {
    ctx.color_test_enable = enable;

    logger->debug("Color test enabled: {}", enable);

    assert(!enable);
}

void set_logic_operation_enable(const bool enable) {
    ctx.logic_operation_enable = enable;

    logger->debug("Logic operation enabled: {}", enable);

    assert(!enable);
}

void set_morph_weight(const u32 idx, const f32 data) {
    assert(idx < NUM_MORPH_WEIGHTS);

    ctx.weights[idx] = data;

    logger->debug("Morph weight {}: {}", idx, data);
}

void set_patch_division(const u32 u_div, const u32 v_div) {
    ctx.patch_control.u_div = u_div;
    ctx.patch_control.v_div = v_div;
}

void set_patch_primitive(const u32 primitive) {
    ctx.patch_control.primitive = PatchPrimitive(primitive & 3);

    logger->debug("Patch primitive: {}", PATCH_PRIMITIVE_NAMES[ctx.patch_control.primitive]);
}

void set_patch_face(const bool is_ccw) {
    ctx.patch_control.is_ccw = is_ccw;

    logger->debug("Patch is CCW: {}", is_ccw);
}

void set_viewport_scale(const u32 idx, const f32 data) {
    assert(idx < NUM_VIEWPORT_COORDS);

    ctx.viewport_scale[idx] = data;

    logger->debug("Viewport S{}: {}", (char)('X' + idx), data);
}

void set_viewport_offset(const u32 idx, const f32 data) {
    assert(idx < NUM_VIEWPORT_COORDS);

    ctx.viewport_offset[idx] = data;

    logger->debug("Viewport T{}: {}", (char)('X' + idx), data);
}

void set_texture_scale(const u32 idx, const f32 data) {
    assert(idx < 2);

    ctx.texture_scale[idx] = data;

    logger->debug("Texture {} scale: {}", (char)('U' + idx), data);
}

void set_texture_offset(const u32 idx, const f32 data) {
    assert(idx < 2);

    ctx.texture_offset[idx] = data;

    logger->debug("Texture {} offset: {}", (char)('U' + idx), data);
}

void set_offset_x(const f32 data) {
    ctx.offset_x = data;

    logger->debug("X offset: {}", data);
}

void set_offset_y(const f32 data) {
    ctx.offset_y = data;

    logger->debug("Y offset: {}", data);
}

void set_gouraud_shading_enable(const bool enable) {
    ctx.gouraud_shading_enable = enable;

    logger->debug("Gouraud shading enabled: {}", enable);
}

void set_model_color(const u32 idx, const u32 data) {
    constexpr const char* MODEL_COLOR_NAMES[] = {
        "Emission", "Ambient", "Diffuse", "Specular",
    };

    assert(idx <= ModelColor::MODEL_COLOR_SPECULAR);

    Color& model_color = ctx.lighting.model_colors[idx];

    model_color.raw &= ~0xFFFFFF;
    model_color.raw |= data;

    logger->debug("Model {} color: {:08X}", MODEL_COLOR_NAMES[idx], model_color.raw);
}

void set_model_alpha(const u32 data) {
    Color& model_color = ctx.lighting.model_colors[ModelColor::MODEL_COLOR_AMBIENT];

    model_color.a = data;

    logger->debug("Model Ambient alpha: {:02X}", model_color.a);
}

void set_ambient_color(const u32 data) {
    Color& ambient_color = ctx.lighting.ambient_color;

    ambient_color.raw &= ~0xFFFFFF;
    ambient_color.raw |= data;

    logger->debug("Global Ambient color: {:08X}", ambient_color.raw);
}

void set_ambient_alpha(const u32 data) {
    Color& ambient_color = ctx.lighting.ambient_color;

    ambient_color.a = data;

    logger->debug("Global Ambient alpha: {:02X}", ambient_color.a);
}

void set_framebuffer_base(const u32 addr_lo) {
    assert((addr_lo & 0x1FFF) == 0);

    u32& addr = ctx.framebuffer.addr;

    addr &= ~0xFFFFFF;
    addr |= addr_lo;

    logger->debug("Framebuffer address: {:08X}", addr);
}

void set_framebuffer_width(const u32 width) {
    if (width == 0) {
        // On boot, all GE regs are cleared to 0. We ignore this here
        return;
    }

    assert((width >= 64) && (width <= 1024));

    auto& framebuffer = ctx.framebuffer;

    u32& addr = framebuffer.addr;

    // The XMB never sets the high byte, so the framebuffer is presumably always
    // in EDRAM
    addr |= 0x04000000;

    framebuffer.width = width;

    logger->debug("Framebuffer address: {:08X}", addr);
    logger->debug("Framebuffer width: {}", framebuffer.width);
}

void set_depth_buffer_base(const u32 addr_lo) {
    assert((addr_lo & 0x1FFF) == 0);

    u32& addr = ctx.depth_buffer.addr;

    addr &= ~0xFFFFFF;
    addr |= addr_lo;

    logger->debug("Depth buffer address: {:08X}", addr);
}

void set_depth_buffer_width(const u32 width) {
    if (width == 0) {
        // On boot, all GE regs are cleared to 0. We ignore this here
        return;
    }

    assert((width >= 64) && (width <= 1024));

    auto& depth_buffer = ctx.depth_buffer;

    u32& addr = depth_buffer.addr;

    // The XMB never sets the high byte, so the depth buffer is presumably always
    // in EDRAM
    addr |= 0x04000000;

    depth_buffer.width = width;

    logger->debug("Depth buffer address: {:08X}", addr);
    logger->debug("Depth buffer width: {}", depth_buffer.width);
}

void set_texture_base(const u32 idx, const u32 addr_lo) {
    assert(idx < 8);

    u32& addr = ctx.texture[idx].addr;

    addr &= ~0xFFFFFF;
    addr |= addr_lo;

    logger->debug("Texture {} address: {:08X}", idx, addr);
}

void set_texture_buffer_width(const u32 idx, const u32 addr_hi, const u32 width) {
    assert(idx < 8);

    auto& texture = ctx.texture[idx];

    u32& addr = texture.addr;

    addr &= ~0xFF000000;
    addr |= addr_hi;

    texture.buf_width = width;

    logger->debug("Texture {} address: {:08X}", idx, addr);
    logger->debug("Texture {} buffer width: {}", idx, texture.buf_width);
}

void set_clut_base_lo(const u32 addr_lo) {
    u32& addr = ctx.clut.addr;

    addr &= ~0xFFFFFF;
    addr |= addr_lo;

    logger->debug("Color LUT address; {:08X}", addr);
}

void set_clut_base_hi(const u32 addr_hi) {
    u32& addr = ctx.clut.addr;

    addr &= ~0xFF000000;
    addr |= addr_hi;

    logger->debug("Color LUT address; {:08X}", addr);
}

void set_texture_size(const u32 idx, const u32 width, const u32 height) {
    assert(idx < 8);
    assert((width <= 9) && (height <= 9));

    auto& texture = ctx.texture[idx];

    texture.width  = 1 << width;
    texture.height = 1 << height;

    logger->debug("Texture {} dimensions: ({}, {})", idx, texture.width, texture.height);
}

void set_texture_format(const u32 format) {
    ctx.texture_format = format;

    logger->debug("Texture format: {}", format);
}

void load_clut(const u32 num_palettes) {
    bus::Bus* bus = kanacore::get_sc_bus_ptr();

    if (num_palettes == 0) {
        return;
    }

    auto& clut = ctx.clut;

    const bool is_full_color = clut.format == ClutFormat::CLUT_FORMAT_RGBA8888;

    u32 clut_addr = clut.addr;

    // Not exactly sure if this is how the CLUT is supposed to work,
    // but the colors *have* to be occupying the same space in palette memory, so
    // full[0] is half[0] and half[1], and so on...?
    for (u32 palette = 0; palette < num_palettes; palette++) {
        for (u32 i = 0; i < 8; i++) {
            if (is_full_color) {
                clut.data[8 * palette + i].full = bus->read<u32>(clut_addr + sizeof(u32) * i);
            } else {
                clut.data[8 * palette + i].half[0] = bus->read<u16>(clut_addr + sizeof(u32) * i + 0);
                clut.data[8 * palette + i].half[1] = bus->read<u16>(clut_addr + sizeof(u32) * i + 2);
            }
        }

        clut_addr += 0x20;
    }
}

void set_clut(const u32 data) {
    auto& clut = ctx.clut;

    clut.format = (ClutFormat)(data & 3);

    clut.shift = (data >> 2) & 0x1F;
    clut.mask  = (data >> 8) & 0xFF;
    clut.start_addr = (data >> 16) & 0x1F;

    logger->debug(
        "Color LUT format: {}, shift: {}, mask: {:02X}, start address: {}",
        CLUT_FORMAT_NAMES[clut.format],
        clut.shift,
        clut.mask,
        clut.start_addr << 4
    );
}

void set_texture_blend_params(const u32 data) {
    auto& blend_params = ctx.tex_blend_params;

    blend_params.func = data & 7;
    blend_params.use_tex_alpha = (data & 0x80) != 0;
}

void set_scissor_upper(const u32 data) {
    ctx.region.sx1 = data & 0x3FF;
    ctx.region.sy1 = (data >> 10) & 0x3FF;

    logger->debug(
        "New scissor (SX1: {}, SX2: {}, SY1: {}, SY2: {})",
        ctx.scissor.sx1,
        ctx.scissor.sx2,
        ctx.scissor.sy1,
        ctx.scissor.sy2
    );
}

void set_scissor_lower(const u32 data) {
    ctx.scissor.sx2 = data & 0x3FF;
    ctx.scissor.sy2 = (data >> 10) & 0x3FF;

    logger->debug(
        "New scissor (SX1: {}, SX2: {}, SY1: {}, SY2: {})",
        ctx.scissor.sx1,
        ctx.scissor.sx2,
        ctx.scissor.sy1,
        ctx.scissor.sy2
    );
}

void set_minimum_depth(const u16 minz) {
    ctx.depth_buffer.minz = minz;

    logger->debug("Minimum depth: {}", minz);
}

void set_maximum_depth(const u16 maxz) {
    ctx.depth_buffer.maxz = maxz;

    logger->debug("Maximum depth: {}", maxz);
}

void set_alpha_test(const u32 data) {
    ctx.alpha_test.test = (Test)(data & 7);
    ctx.alpha_test.ref  = (data >>  8) & 0xFF;
    ctx.alpha_test.mask = (data >> 12) & 0xFF;
}

void set_depth_test(const u32 data) {
    assert(data < 8);

    ctx.depth_test = (Test)data;
}

void set_blend_params(const u32 data) {
    auto& blend_params = ctx.blend_params;

    const u32 func = (data >> 8) & 7;

    assert(func <= BlendFunc::BLEND_FUNC_ABS);

    blend_params.src_input = (data >> 0) & 0xF;
    blend_params.dst_input = (data >> 4) & 0xF;
    blend_params.func = (BlendFunc)func;
}

void set_fixed_color_a(const u32 data) {
    Color& fixa = ctx.blend_params.fixa;

    fixa.raw = data;
    fixa.a = 0xFF;
}

void set_fixed_color_b(const u32 data) {
    Color& fixb = ctx.blend_params.fixb;

    fixb.raw = data;
    fixb.a = 0xFF;
}

void set_color_mask(const u32 data) {
    assert(data == 0);

    logger->debug("Color mask: {:06X}", data);
}

void set_alpha_mask(const u32 data) {
    assert(data == 0);

    logger->debug("Alpha mask: {:02X}", data);
}

u32 get_base() {
    return ctx.base;
}

u32 get_vertex_addr() {
    return ctx.vertex_addr;
}

u32 get_index_addr() {
    return ctx.index_addr;
}

static void transform_3d(std::vector<Vertex>& vertices, const f32* matrix) {
    for (Vertex& vertex : vertices) {
        const f32 w[3] = {
            matrix[0] * vertex.x + matrix[3] * vertex.y + matrix[6] * vertex.z,
            matrix[1] * vertex.x + matrix[4] * vertex.y + matrix[7] * vertex.z,
            matrix[2] * vertex.x + matrix[5] * vertex.y + matrix[8] * vertex.z,
        };

        vertex.x = w[0] + matrix[9];
        vertex.y = w[1] + matrix[10];
        vertex.z = w[2] + matrix[11];
    }
}

static void transform_4d(std::vector<Vertex>& vertices, const f32* matrix) {
    for (Vertex& vertex : vertices) {
        const f32 w[4] = {
            matrix[0] * vertex.x + matrix[4] * vertex.y + matrix[8 ] * vertex.z + matrix[12],
            matrix[1] * vertex.x + matrix[5] * vertex.y + matrix[9 ] * vertex.z + matrix[13],
            matrix[2] * vertex.x + matrix[6] * vertex.y + matrix[10] * vertex.z + matrix[14],
            matrix[3] * vertex.x + matrix[7] * vertex.y + matrix[11] * vertex.z + matrix[15],
        };

        vertex.x = w[0];
        vertex.y = w[1];
        vertex.z = w[2];
        vertex.w = w[3];
    }
}

static void viewport_transform(std::vector<Vertex>& vertices) {
    for (Vertex& vertex : vertices) {
        const f32 w = vertex.w;

        vertex.x = ctx.viewport_scale[0] * vertex.x / w + ctx.viewport_offset[0];
        vertex.y = ctx.viewport_scale[1] * vertex.y / w + ctx.viewport_offset[1];
        vertex.z = ctx.viewport_scale[2] * vertex.z / w + ctx.viewport_offset[2];
    }
}

static void screen_transform(std::vector<Vertex>& vertices) {
    for (Vertex& vertex : vertices) {
        vertex.x -= ctx.offset_x;
        vertex.y -= ctx.offset_y;
    }
}

// Blending/lighting helpers

static inline u8 color_clamp(const int color) {
    if (color > 255) {
        return 255;
    } else if (color < 0) {
        return 0;
    }

    return (u8)color;
}

static inline Color color_add(const Color src_color, const Color dst_color) {
    return Color {
        .r = color_clamp(src_color.r + dst_color.r),
        .g = color_clamp(src_color.g + dst_color.g),
        .b = color_clamp(src_color.b + dst_color.b),
        .a = color_clamp(src_color.a + dst_color.a),
    };
}

static inline Color color_subtract(const Color src_color, const Color dst_color) {
    return Color {
        .r = color_clamp(src_color.r - dst_color.r),
        .g = color_clamp(src_color.g - dst_color.g),
        .b = color_clamp(src_color.b - dst_color.b),
        .a = color_clamp(src_color.a - dst_color.a),
    };
}

static inline Color color_multiply(const Color src_color, const Color dst_color) {
    return Color {
        .r = (u8)((src_color.r * dst_color.r) / 255),
        .g = (u8)((src_color.g * dst_color.g) / 255),
        .b = (u8)((src_color.b * dst_color.b) / 255),
        .a = (u8)((src_color.a * dst_color.a) / 255),
    };
}

static inline u8 color_multiply(const u8 src_color, const u8 dst_color) {
    return ((int)src_color * (int)dst_color) / 255;
}

static void calculate_lighting(std::vector<Vertex>& vertices) {
    auto& lighting = ctx.lighting;

    const bool has_colors = ctx.vertex_type.color_type != ColorType::COLOR_TYPE_NONE;

    for (Vertex& vertex : vertices) {
        Color final_color, vertex_color{ .r = (u8)vertex.r, .g = (u8)vertex.g, .b = (u8)vertex.b, .a = (u8)vertex.a };

        if (ctx.lighting_enable) {
            // Set color to model emission color + global ambient
            final_color = color_add(lighting.model_colors[ModelColor::MODEL_COLOR_EMISSION], color_multiply(lighting.model_colors[ModelColor::MODEL_COLOR_AMBIENT], lighting.ambient_color));
        } else if (!has_colors) {
            // Use model ambient color if there is no color data
            final_color = lighting.model_colors[ModelColor::MODEL_COLOR_AMBIENT];

            vertex.has_colors = true;
        }

        vertex.r = final_color.r;
        vertex.g = final_color.g;
        vertex.b = final_color.b;
        vertex.a = final_color.a;
    }
}

static Vertex fetch_vertex(u32 addr) {
    assert(ctx.vertex_type.modcoord_type != ModcoordType::MODCOORD_TYPE_NONE);

    bus::Bus* bus = kanacore::get_sc_bus_ptr();

    Vertex vertex{ .addr = addr };

    const bool through_mode = ctx.vertex_type.through_mode;

    if (!through_mode) {
        // Only fetch weights in Normal mode
        const u32 weight_type = ctx.vertex_type.weight_type;

        switch (weight_type) {
            case WeightType::WEIGHT_TYPE_NONE:
                break;
            default:
                logger->error("Unimplemented weight type {}", VTYPE_WT_NAMES[weight_type]);
                exit(1);
        }
    }

    const u32 texcoord_type = ctx.vertex_type.texcoord_type;

    vertex.has_texcoords = texcoord_type != TexcoordType::TEXCOORD_TYPE_NONE;

    switch (texcoord_type) {
        case TexcoordType::TEXCOORD_TYPE_NONE:
            break;
        case TexcoordType::TEXCOORD_TYPE_F32:
            vertex.s = from_u32(bus->read<u32>(addr + 0));
            vertex.t = from_u32(bus->read<u32>(addr + 4));

            addr += 2 * sizeof(f32);
            break;
        default:
            logger->error("Unimplemented texcoord type {}", VTYPE_TT_NAMES[texcoord_type]);
            exit(1);
    }

    const u32 color_type = ctx.vertex_type.color_type;

    vertex.has_colors = color_type != ColorType::COLOR_TYPE_NONE;

    switch (color_type) {
        case ColorType::COLOR_TYPE_NONE:
            vertex.has_colors = false;
            break;
        case ColorType::COLOR_TYPE_RGBA8888: {
            const u32 color = bus->read<u32>(addr);

            vertex.a = (color >> 24) & 0xFF;
            vertex.b = (color >> 16) & 0xFF;
            vertex.g = (color >>  8) & 0xFF;
            vertex.r = (color >>  0) & 0xFF;

            addr += sizeof(u32);
            break;
        }
        default:
            logger->error("Unimplemented color type {}", VTYPE_CT_NAMES[color_type]);
            exit(1);
    }

    if (!through_mode) {
        // Only fetch normals in Normal mode
        const u32 normal_type = ctx.vertex_type.normal_type;

        vertex.has_normals = normal_type != NormalType::NORMAL_TYPE_NONE;

        switch (normal_type) {
            case NormalType::NORMAL_TYPE_NONE:
                break;
            case NormalType::NORMAL_TYPE_F32:
                vertex.nx = from_u32(bus->read<u32>(addr + 0));
                vertex.ny = from_u32(bus->read<u32>(addr + 4));
                vertex.nz = from_u32(bus->read<u32>(addr + 8));

                addr += 3 * sizeof(f32);
                break;
            default:
                logger->error("Unimplemented normal type {}", VTYPE_NT_NAMES[normal_type]);
                exit(1);
        }
    }

    const u32 modcoord_type = ctx.vertex_type.modcoord_type;

    switch (modcoord_type) {
        case ModcoordType::MODCOORD_TYPE_FP16:
            vertex.x = (f32)(i16)bus->read<u16>(addr + 0);
            vertex.y = (f32)(i16)bus->read<u16>(addr + 2);
            vertex.z = (f32)(i16)bus->read<u16>(addr + 4);
            break;
        case ModcoordType::MODCOORD_TYPE_F32:
            vertex.x = from_u32(bus->read<u32>(addr + 0));
            vertex.y = from_u32(bus->read<u32>(addr + 4));
            vertex.z = from_u32(bus->read<u32>(addr + 8));
            break;
        default:
            logger->error("Unimplemented modcoord type {}", VTYPE_VT_NAMES[modcoord_type]);
            exit(1);
    }

    return vertex;
}

static std::vector<Vertex> fetch_vertices(const u32 count, const bool is_rectangle = false) {
    bus::Bus* bus = kanacore::get_sc_bus_ptr();

    std::vector<Vertex> vertices(count);

    const bool through_mode = ctx.vertex_type.through_mode;
    const bool morph_enable = !through_mode && (ctx.vertex_type.num_morph > 0);

    // Input vertices per output vertex
    const u32 num_vertices = !morph_enable ? 1 : (ctx.vertex_type.num_morph + 1);

    for (u32 i = 0; i < count; i++) {
        Vertex& vertex = vertices[i];

        u32 vertex_addr = ctx.vertex_addr;

        switch (ctx.vertex_type.index_type) {
            case IndexType::INDEX_TYPE_NONE:
                vertex_addr += num_vertices * i * ctx.vertex_size;
                break;
            case IndexType::INDEX_TYPE_U8:
                vertex_addr += num_vertices * ctx.vertex_size * bus->read<u8>(ctx.index_addr + i);
                break;
            case IndexType::INDEX_TYPE_U16:
                vertex_addr += num_vertices * ctx.vertex_size * bus->read<u16>(ctx.index_addr + i * sizeof(u16));
                break;
            default:
                logger->error("Invalid index type");
                exit(1);
        }

        if (morph_enable) {
            vertex.has_texcoords = ctx.vertex_type.texcoord_type != TexcoordType::TEXCOORD_TYPE_NONE;
            vertex.has_colors = ctx.vertex_type.color_type != ColorType::COLOR_TYPE_NONE;

            for (u32 j = 0; j < num_vertices; j++) {
                const f32 weight = ctx.weights[j];

                Vertex blend_vertex = fetch_vertex(vertex_addr);

                if (blend_vertex.has_texcoords) {
                    vertex.s += blend_vertex.s * weight;
                    vertex.t += blend_vertex.t * weight;
                }

                if (blend_vertex.has_colors) {
                    vertex.r += blend_vertex.r * weight;
                    vertex.g += blend_vertex.g * weight;
                    vertex.b += blend_vertex.b * weight;
                    vertex.a += blend_vertex.a * weight;
                }

                if (blend_vertex.has_normals) {
                    vertex.nx += blend_vertex.nx * weight;
                    vertex.ny += blend_vertex.ny * weight;
                    vertex.nz += blend_vertex.nz * weight;
                }

                vertex.x += blend_vertex.x * weight;
                vertex.y += blend_vertex.y * weight;
                vertex.z += blend_vertex.z * weight;

                vertex_addr += ctx.vertex_size;
            }
        } else {
            vertex = fetch_vertex(vertex_addr);
        }

        logger->trace("Vertex @ {:08X}", vertex.addr);

        if (vertex.has_texcoords) {
            logger->trace("S: {}, T: {}", vertex.s, vertex.t);
        }

        if (vertex.has_colors) {
            logger->trace("R: {}, G: {}, B: {}, A: {}", vertex.r, vertex.g, vertex.b, vertex.a);
        }

        if (vertex.has_normals) {
            logger->trace("NX: {}, NY: {}, NZ: {}", vertex.nx, vertex.ny, vertex.nz);
        }

        logger->trace("X: {}, Y: {}, Z: {}", vertex.x, vertex.y, vertex.z);
    }

    // Rectangles always use display coordinates, so they do not experience this
    if (!through_mode && !is_rectangle) {
        // In normal mode, the GE performs a buuuunch of vertex transformations...
        transform_3d(vertices, ge::get_world_matrix());

        calculate_lighting(vertices);

        transform_3d(vertices, ge::get_view_matrix());
        transform_4d(vertices, ge::get_perspective_matrix());
        viewport_transform(vertices);
    }

    if (!is_rectangle) {
        screen_transform(vertices);
    }

    return vertices;
}

static inline f32 edge_function(const Vertex& a, const Vertex& b, const Vertex& c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

static inline f32 interpolate(
    const f32 w0,
    const f32 w1,
    const f32 w2,
    const f32 a,
    const f32 b,
    const f32 c,
    const f32 area
) {
    return (w0 * a + w1 * b + w2 * c) / area;
}

static inline void st_interpolate(
    const f32 w0,
    const f32 w1,
    const f32 w2,
    const Vertex& a,
    const Vertex& b,
    const Vertex& c,
    f32* s,
    f32* t
) {
    const f32 w = w0 * (1.0 / a.w) + w1 * (1.0 / b.w) + w2 * (1.0 / c.w);

    // Perspectively interpolates S and T
    *s = (w0 * (a.s / a.w) + w1 * (b.s / b.w) + w2 * (c.s / c.w)) / w;
    *t = (w0 * (a.t / a.w) + w1 * (b.t / b.w) + w2 * (c.t / c.w)) / w;
}

static u32 swizzle_to_linear(const u32 u, const u32 v, const u32 width, const u32 bpp) {
    const u32 ubpp = u * bpp;

    return ((v / 8) * width * bpp) + (16 * (v % 8)) + (ubpp & ~127) + ((ubpp & 127) / 8);
}

static u32 from_rgba4444(const u16 color) {
    Color new_color;

    new_color.r = (color >>  0) & 0xF;
    new_color.g = (color >>  4) & 0xF;
    new_color.b = (color >>  8) & 0xF;
    new_color.a = (color >> 12) & 0xF;

    return new_color.raw | (new_color.raw << 4);
}

static u32 fetch_clut(const u32 idx) {
    auto& clut = ctx.clut;

    const u32 csa = clut.start_addr;

    // clut_idx[8]   = csa[4]
    // clut_idx[7:4] = csa[3:0] | ((idx >> shift) & mask)[7:4]
    // clut_idx[3:0] = ((idx >> shift) & mask)[3:0]
    const u32 clut_idx = (csa << 4) | ((u8)(idx >> clut.shift) & clut.mask);

    // Unsure what happens when the index is larger than 256 in RGBA8888 mode
    assert((clut.format != ClutFormat::CLUT_FORMAT_RGBA8888) || (clut_idx < 256));

    switch (clut.format) {
        case ClutFormat::CLUT_FORMAT_RGBA4444:
            return from_rgba4444(clut.data[clut_idx >> 1].half[clut_idx & 1]);
        case ClutFormat::CLUT_FORMAT_RGBA8888:
            return clut.data[clut_idx].full;
        default:
            logger->error("Unimplemented color LUT format {}", CLUT_FORMAT_NAMES[clut.format]);
            exit(1);
    }
}

static u32 fetch_texel(const u32 u, const u32 v) {
    bus::Bus* bus = kanacore::get_sc_bus_ptr();

    // For now we use texture 0. The rest is related to MIP mapping
    const u32 tex0_addr = ctx.texture[0].addr;
    const u32 tex0_buf_width = ctx.texture[0].buf_width;

    switch (ctx.texture_format) {
        case TexelFormat::TEXEL_FORMAT_RGBA8888:
            return bus->read<u32>(tex0_addr + swizzle_to_linear(u, v, tex0_buf_width, 32));
        case TexelFormat::TEXEL_FORMAT_IDX4:
            return fetch_clut(((bus->read<u8>(tex0_addr + swizzle_to_linear(u, v, tex0_buf_width, 4))) >> (4 * (u & 1))) & 0xF);
        case TexelFormat::TEXEL_FORMAT_IDX8:
            return fetch_clut(bus->read<u8>(tex0_addr + swizzle_to_linear(u, v, tex0_buf_width, 8)));
        default:
            logger->error("Unimplemented texture format {}", TEXEL_FORMAT_NAMES[ctx.texture_format]);
            exit(1);
    }

    exit(1);
}

static u32 read_framebuffer(const u32 x, const u32 y) {
    bus::Bus* bus = kanacore::get_sc_bus_ptr();

    const auto& framebuffer = ctx.framebuffer;

    // This will do automatic format conversion when we add other formats
    return bus->read<u32>(framebuffer.addr + sizeof(u32) * (y * framebuffer.width + x));
}

static u16 read_depth_buffer(const u32 x, const u32 y) {
    bus::Bus* bus = kanacore::get_sc_bus_ptr();

    const auto& depth_buffer = ctx.depth_buffer;

    return bus->read<u16>(depth_buffer.addr + sizeof(u16) * (y * depth_buffer.width + x));
}

static void write_framebuffer(const u32 x, const u32 y, const u32 color) {
    bus::Bus* bus = kanacore::get_sc_bus_ptr();

    const auto& framebuffer = ctx.framebuffer;

    // This will do automatic format conversion when we add other formats
    bus->write<u32>(framebuffer.addr + sizeof(u32) * (y * framebuffer.width + x), color);
}

static void write_depth_buffer(const u32 x, const u32 y, const u16 depth) {
    bus::Bus* bus = kanacore::get_sc_bus_ptr();

    const auto& depth_buffer = ctx.depth_buffer;

    bus->write<u16>(depth_buffer.addr + sizeof(u16) * (y * depth_buffer.width + x), depth);
}

static void clear_buffers() {
    for (u32 y = 0; y < 272; y++) {
        for (u32 x = 0; x < 480; x++) {
            write_framebuffer(x, y, 0);
            write_depth_buffer(x, y, 0);
        }
    }
}

static bool depth_range_test(const u16 depth) {
    if (!ctx.vertex_type.through_mode) {
        return (depth >= ctx.depth_buffer.minz) && (depth <= ctx.depth_buffer.maxz);
    }

    // The depth range test always passes in Through mode
    return true;
}

static bool alpha_test(u32 alpha) {
    auto& alpha_test = ctx.alpha_test;

    if (ctx.alpha_test_enable) {
        const u32 ref = alpha_test.ref & alpha_test.mask;

        alpha &= alpha_test.mask;

        switch (alpha_test.test) {
            case Test::TEST_NEVER:
                return false;
            case Test::TEST_ALWAYS:
                return true;
            case Test::TEST_EQUAL:
                return alpha == ref;
            case Test::TEST_NOT_EQUAL:
                return alpha != ref;
            case Test::TEST_LESS:
                return alpha < ref;
            case Test::TEST_LESS_EQUAL:
                return alpha <= ref;
            case Test::TEST_GREATER:
                return alpha > ref;
            case Test::TEST_GREATER_EQUAL:
                return alpha >= ref;
        }
    }

    return true;
}

static bool depth_test(const u32 x, const u32 y, const u16 depth) {
    if (ctx.depth_test_enable) {
        const u16 old_depth = read_depth_buffer(x, y);

        bool test_passed;

        switch (ctx.depth_test) {
            case Test::TEST_NEVER:
                // Always fails
                return false;
            case Test::TEST_ALWAYS:
                // Always passes
                test_passed = true;
                break;
            case Test::TEST_EQUAL:
                test_passed = depth == old_depth;
                break;
            case Test::TEST_NOT_EQUAL:
                test_passed = depth != old_depth;
                break;
            case Test::TEST_LESS:
                test_passed = depth < old_depth;
                break;
            case Test::TEST_LESS_EQUAL:
                test_passed = depth <= old_depth;
                break;
            case Test::TEST_GREATER:
                test_passed = depth > old_depth;
                break;
            case Test::TEST_GREATER_EQUAL:
                test_passed = depth >= old_depth;
                break;
        }

        if (!test_passed) {
            return false;
        }

        if (!ctx.depth_buffer.mask) {
            write_depth_buffer(x, y, depth);
        }
    }

    // If the test is disabled, it always passes, but the depth buffer
    // isn't updated
    return true;
}

constexpr const char* BLEND_INPUT_NAMES[] = {

};

static Color blend(const Color color, const u32 x, const u32 y) {
    if (!ctx.alpha_blending_enable) {
        return color;
    }

    auto& blend_params = ctx.blend_params;

    const Color src_color = color;
    const Color dst_color = Color{ .raw = read_framebuffer(x, y) };
    
    Color a_color, b_color;

    switch (blend_params.src_input) {
        case 1:
            // Reverse destination alpha
            a_color.r = 255 - dst_color.a;
            a_color.g = 255 - dst_color.a;
            a_color.b = 255 - dst_color.a;
            a_color.a = 255 - dst_color.a;
            break;
        case 2:
            // Source alpha
            a_color.r = src_color.a;
            a_color.g = src_color.a;
            a_color.b = src_color.a;
            a_color.a = src_color.a;
            break;
        default:
            logger->error("Unimplemented blend source input {}", blend_params.src_input);
            exit(1);
    }

    switch (blend_params.dst_input) {
        case 3:
            // Reverse source alpha
            b_color.r = 255 - src_color.a;
            b_color.g = 255 - src_color.a;
            b_color.b = 255 - src_color.a;
            b_color.a = 255 - src_color.a;
            break;
        case 10:
            // FIXB
            b_color = blend_params.fixb;
            break;
        default:
            logger->error("Unimplemented blend destination input {}", blend_params.dst_input);
            exit(1);
    }

    Color final_color;

    switch (blend_params.func) {
        case BlendFunc::BLEND_FUNC_ADD:
            final_color = color_add(color_multiply(src_color, a_color), color_multiply(dst_color, b_color));
            break;
        case BlendFunc::BLEND_FUNC_REVERSE_SUBTRACT:
            final_color = color_subtract(color_multiply(dst_color, b_color), color_multiply(src_color, a_color));
            break;
        default:
            logger->error("Unimplemented blend function {}", BLEND_FUNC_NAMES[blend_params.func]);
            exit(1);
    }

    return final_color;
}

static Color blend_texture(const Color vertex_color, const Color tex_color) {
    auto& blend_params = ctx.tex_blend_params;

    Color final_color;

    switch (blend_params.func) {
        case 0:
            // Modulate
            final_color.r = color_multiply(vertex_color.r, tex_color.r);
            final_color.g = color_multiply(vertex_color.g, tex_color.g);
            final_color.b = color_multiply(vertex_color.b, tex_color.b);
            final_color.a = color_multiply(vertex_color.a, tex_color.a);
            break;
        default:
            logger->error("Unimplemented texture function {}", blend_params.func);
            exit(1);
    }

    // Something about this is odd, I'll turn it off for now
    /* if (!blend_params.use_tex_alpha) {
        final_color.a = vertex_color.a;
    } */

    return final_color;
}

static u32 tex_sample_bilinear(const f32 u, const f32 v) {
    const int x0 = (int)u;
    const int x1 = std::min(x0 + 1, (int)(ctx.texture[0].width - 1));
    const int y0 = (int)v;
    const int y1 = std::min(y0 + 1, (int)(ctx.texture[0].height - 1));

    const f32 dx = u - x0;
    const f32 dy = v - y0;

    Color colors[6] = {
        { fetch_texel(x0, y0) }, { fetch_texel(x1, y0) },
        { fetch_texel(x0, y1) }, { fetch_texel(x1, y1) },
    };

    // Blend top pixels
    colors[4].r = color_clamp(colors[0].r * (1.0 - dx) + colors[1].r * dx);
    colors[4].g = color_clamp(colors[0].g * (1.0 - dx) + colors[1].g * dx);
    colors[4].b = color_clamp(colors[0].b * (1.0 - dx) + colors[1].b * dx);
    colors[4].a = color_clamp(colors[0].a * (1.0 - dx) + colors[1].a * dx);

    // Blend bottom pixels
    colors[5].r = color_clamp(colors[2].r * (1.0 - dx) + colors[3].r * dx);
    colors[5].g = color_clamp(colors[2].g * (1.0 - dx) + colors[3].g * dx);
    colors[5].b = color_clamp(colors[2].b * (1.0 - dx) + colors[3].b * dx);
    colors[5].a = color_clamp(colors[2].a * (1.0 - dx) + colors[3].a * dx);

    return Color {
        .r = color_clamp(colors[4].r * (1.0 - dy) + colors[5].r * dy),
        .g = color_clamp(colors[4].g * (1.0 - dy) + colors[5].g * dy),
        .b = color_clamp(colors[4].b * (1.0 - dy) + colors[5].b * dy),
        .a = color_clamp(colors[4].a * (1.0 - dy) + colors[5].a * dy),
    }.raw;
}

static void draw_triangle(Vertex a, Vertex b, Vertex c) {
    logger->trace(
        "Triangle ({}, {}, {}), ({}, {}, {}), ({}, {}, {})",
        a.x, a.y, a.z,
        b.x, b.y, b.z,
        c.x, c.y, c.z
    );

    if (edge_function(a, b, c) < 0.0) {
        std::swap(b, c);
    }

    const f32 area = edge_function(a, b, c);

    // Calculate bounding box
    const int x_min = std::floor(std::max(std::min(c.x, std::min(a.x, b.x)), (f32)ctx.scissor.sx1));
    const int x_max = std::floor(std::min(std::max(c.x, std::max(a.x, b.x)), (f32)ctx.scissor.sx2));
    const int y_min = std::floor(std::max(std::min(c.y, std::min(a.y, b.y)), (f32)ctx.scissor.sy1));
    const int y_max = std::floor(std::min(std::max(c.y, std::max(a.y, b.y)), (f32)ctx.scissor.sy2));

    logger->trace("Bounding box: ({}, {}), ({}, {})", x_min, y_min, x_max, y_max);

    if ((x_min >= x_max) || (y_min >= y_max)) {
        return;
    }

    for (int y = y_min; y <= y_max; y++) {
        for (int x = x_min; x <= x_max; x++) {
            Vertex p{ .x = (f32)x, .y = (f32)y };

            // Calculate weights
            const f32 w0 = edge_function(b, c, p);
            const f32 w1 = edge_function(c, a, p);
            const f32 w2 = edge_function(a, b, p);

            if ((w0 >= 0.0) && (w1 >= 0.0) && (w2 >= 0.0)) {
                const u16 z = (u16)interpolate(w0, w1, w2, a.z, b.z, c.z, area);

                if (!depth_range_test(z)) {
                    continue;
                }

                if (!depth_test(x, y, z)) {
                    continue;
                }

                Color vertex_color, final_color;

                assert(a.has_colors);

                if (ctx.gouraud_shading_enable) {
                    vertex_color.r = interpolate(w0, w1, w2, a.r, b.r, c.r, area);
                    vertex_color.g = interpolate(w0, w1, w2, a.g, b.g, c.g, area);
                    vertex_color.b = interpolate(w0, w1, w2, a.b, b.b, c.b, area);
                    vertex_color.a = interpolate(w0, w1, w2, a.a, b.a, c.a, area);
                } else {
                    vertex_color.r = c.r;
                    vertex_color.g = c.g;
                    vertex_color.b = c.b;
                    vertex_color.a = c.a;
                }

                if (ctx.texture_mapping_enable && a.has_texcoords) {
                    f32 s, t;

                    st_interpolate(w0, w1, w2, a, b, c, &s, &t);

                    s = s * ctx.texture_scale[0] + ctx.texture_offset[0];
                    t = t * ctx.texture_scale[1] + ctx.texture_offset[1];

                    // TODO: implement wrapping
                    // Clamp S
                    if (s < 0.0) {
                        s = 0.0;
                    } else if (s > 1.0) {
                        s = 1.0;
                    }

                    // Clamp T
                    if (t < 0.0) {
                        t = 0.0;
                    } else if (t > 1.0) {
                        t = 1.0;
                    }

                    const f32 u = s * (ctx.texture[0].width  - 1);
                    const f32 v = t * (ctx.texture[0].height - 1);

                    const Color tex_color { .raw = tex_sample_bilinear(u, v) };

                    final_color = blend_texture(vertex_color, tex_color);
                } else {
                    final_color = vertex_color;
                }

                if (!alpha_test(final_color.a)) {
                    continue;
                }

                final_color = blend(final_color, x, y);

                write_framebuffer(p.x, p.y, final_color.raw);
                write_depth_buffer(p.x, p.y, z);
            }
        }
    }
}

void draw_primitive(const u32 count, const u32 prim_type) {
    assert(prim_type <= PrimType::PRIM_TYPE_RECTANGLE);

    if (count == 0) {
        return;
    }

    const bool is_rectangle = prim_type == PrimType::PRIM_TYPE_RECTANGLE;

    const std::vector<Vertex> vertices = fetch_vertices(count, is_rectangle);

    switch (prim_type) {
        case PrimType::PRIM_TYPE_TRIANGLE_STRIP: {
            assert(count > 2);

            for (u32 i = 0; i < (count - 2); i++) {
                draw_triangle(vertices[i + 0], vertices[i + 1], vertices[i + 2]);
            }
            break;
        }
        default:
            logger->warn("Unimplemented primitive type {}", PRIMITIVE_NAMES[prim_type]);
            break;
    }

    if (is_rectangle) {
        // Massive hack, we need to check for clear mode and actually draw
        // rectangles at some point :D
        clear_buffers();
    }
}

// This calculates the value of the Bernstein basis polynomial B_idx
static inline f32 get_bernstein_value(const int idx, const f32 x) {
    assert((idx >= 0) && (idx <= 3));

    switch (idx) {
        case 0:
            // (1 - x)^3
            return (1 - x) * (1 - x) * (1 - x);
        case 1:
            // 3x * (1 - x)^2
            return 3 * x * (1 - x) * (1 - x);
        case 2:
            // 3x^2 * (1 - x)
            return 3 * x * x * (1 - x);
        case 3:
            // x^3
            return x * x * x;
    }

    // Whatever
    exit(1);
}

static void draw_bezier_patch(
    const std::vector<Vertex>& control_points,
    const u32 patch_u,
    const u32 patch_v,
    const u32 u_count
) {
    logger->trace("Drawing Bezier patch ({}, {})", patch_u / 3, patch_v / 3);

    const u32 u_div = ctx.patch_control.u_div;
    const u32 v_div = ctx.patch_control.v_div;

    assert((u_div >= 1) && (u_div <= 64));
    assert((v_div >= 1) && (v_div <= 64));

    // We only support triangles for now
    assert(ctx.patch_control.primitive == PATCH_PRIMITIVE_TRIANGLE);

    std::vector<Vertex> vertices((u_div + 1) * (v_div + 1));

    // If one control point has texcoords, all of them do. Applies to the
    // vertices we generate, too
    const bool has_texcoords = control_points[0].has_texcoords;
    const bool has_colors = control_points[0].has_colors;

    // Tessellate the patch
    for (u32 v = 0; v <= v_div; v++) {
        for (u32 u = 0; u <= u_div; u++) {
            Vertex& vertex = vertices[v * (u_div + 1) + u];

            vertex.has_texcoords = has_texcoords;
            vertex.has_colors = has_colors;

            const f32 u_sub = (f32)u / u_div;
            const f32 v_sub = (f32)v / v_div;

            for (int i = 0; i < 4; i++) {
                for (int j = 0; j < 4; j++) {
                    const f32 bernstein_u = get_bernstein_value(i, u_sub);
                    const f32 bernstein_v = get_bernstein_value(j, v_sub);

                    const Vertex& control_point = control_points[(patch_v + j) * u_count + patch_v + i];

                    vertex.x += bernstein_u * bernstein_v * control_point.x;
                    vertex.y += bernstein_u * bernstein_v * control_point.y;
                    vertex.z += bernstein_u * bernstein_v * control_point.z;

                    if (has_texcoords) {
                        vertex.s += bernstein_u * bernstein_v * control_point.s;
                        vertex.t += bernstein_u * bernstein_v * control_point.t;
                    }

                    if (has_colors) {
                        vertex.r += bernstein_u * bernstein_v * control_point.r;
                        vertex.g += bernstein_u * bernstein_v * control_point.g;
                        vertex.b += bernstein_u * bernstein_v * control_point.b;
                        vertex.a += bernstein_u * bernstein_v * control_point.a;
                    }
                }
            }

            logger->trace("Patch vertex (X: {}, Y: {}, Z: {})", vertex.x, vertex.y, vertex.z);
        }
    }

    // Draw the patch mesh
    for (u32 v = 0; v < v_div; v++) {
        for (u32 u = 0; u < u_div; u++) {
            Vertex& a = vertices[(v + 0) * (u_div + 1) + (u + 0)];
            Vertex& b = vertices[(v + 0) * (u_div + 1) + (u + 1)];
            Vertex& c = vertices[(v + 1) * (u_div + 1) + (u + 0)];
            Vertex& d = vertices[(v + 1) * (u_div + 1) + (u + 1)];
            
            draw_triangle(a, b, c);
            draw_triangle(c, b, d);
        }
    }
}

void draw_bezier(const u32 u_count, const u32 v_count) {
    // A patch needs 16 control points, edges overlap so every
    // subsequent patch only needs three extra points in every direction
    assert((u_count >= 4) && (((u_count - 1) % 3) == 0));
    assert((v_count >= 4) && (((v_count - 1) % 3) == 0));

    const std::vector<Vertex> control_points = fetch_vertices(u_count * v_count);

    for (u32 patch_v = 0; patch_v < (v_count - 1); patch_v += 3) {
        for (u32 patch_u = 0; patch_u < (u_count - 1); patch_u += 3) {
            draw_bezier_patch(control_points, patch_u, patch_v, u_count);
        }
    }
}

static std::vector<int> get_knots(const int count, const KnotType knot_type) {
    // Every knot vector needs (number of control points + 4) knots
    const int knot_count = count + 5;

    std::vector<int> knots(knot_count);

    switch (knot_type) {
        case KnotType::KNOT_TYPE_CLOSE_CLOSE:
            for (int i = 0; i < knot_count; i++) {
                knots[i] = i - 3;
            }
            break;
        case KnotType::KNOT_TYPE_OPEN_CLOSE:
            for (int i = 0; i < knot_count; i++) {
                if (i < 4) {
                    knots[i] = 0;
                } else {
                    knots[i] = i - 3;
                }
            }
            break;
        case KnotType::KNOT_TYPE_CLOSE_OPEN:
            for (int i = 0; i < knot_count; i++) {
                if (i > count) {
                    knots[i] = count - 2;
                } else {
                    knots[i] = i - 3;
                }
            }
            break;
        case KnotType::KNOT_TYPE_OPEN_OPEN:
            for (int i = 0; i < knot_count; i++) {
                if (i < 4) {
                    knots[i] = 0;
                } else if (i > count) {
                    knots[i] = count - 2;
                } else {
                    knots[i] = i - 3;
                }
            }
            break;
    }
    
    return knots;
}

static f32 get_basis_spline_value(const int i, const int j, const f32 x, const std::vector<int>& knots) {
    if (j == 0) {
        if ((knots[i] <= x) && (x < knots[i + 1])) {
            return 1;
        }

        if ((x == knots[i + 1]) && (knots[i] < knots[i + 1])) {
            if (knots[i + 1] == knots.back()) {
                return 1;
            }
        }

        return 0;
    }

    f32 w0 = 0;
    f32 w1 = 0;

    if ((knots[i + j] - knots[i]) != 0) {
        w0 = (x - knots[i]) / (knots[i + j] - knots[i]);
    }

    if ((knots[i + j + 1] - knots[i + 1]) != 0) {
        w1 = (knots[i + j + 1] - x) / (knots[i + j + 1] - knots[i + 1]);
    }

    return
        w0 * get_basis_spline_value(i + 0, j - 1, x, knots) +
        w1 * get_basis_spline_value(i + 1, j - 1, x, knots);
}

static void draw_spline_patch(
    const std::vector<Vertex>& control_points,
    const u32 patch_u,
    const u32 patch_v,
    const u32 u_count,
    const u32 v_count,
    const KnotType u_knot_type,
    const KnotType v_knot_type
) {
    logger->trace("Drawing spline patch ({}, {})", patch_u, patch_v);

    const u32 u_div = ctx.patch_control.u_div;
    const u32 v_div = ctx.patch_control.v_div;

    assert((u_div >= 1) && (u_div <= 64));
    assert((v_div >= 1) && (v_div <= 64));

    // We only support triangles for now
    assert(ctx.patch_control.primitive == PATCH_PRIMITIVE_TRIANGLE);

    std::vector<Vertex> vertices((u_div + 1) * (v_div + 1));

    const int n = u_count - 1;
    const int m = v_count - 1;

    const std::vector<int> u_knots = get_knots(n, u_knot_type);
    const std::vector<int> v_knots = get_knots(m, v_knot_type);

    const f32 u_sub = 1.0 / u_div;
    const f32 v_sub = 1.0 / v_div;

    // Tessellate the patch
    for (u32 v = 0; v <= v_div; v++) {
        for (u32 u = 0; u <= u_div; u++) {
            Vertex& vertex = vertices[v * (u_div + 1) + u];

            for (int i = 0; i < 4; i++) {
                for (int j = 0; j < 4; j++) {
                    const f32 spline_u = get_basis_spline_value(i + patch_u, 3, patch_u + u * u_sub, u_knots);
                    const f32 spline_v = get_basis_spline_value(j + patch_v, 3, patch_v + v * v_sub, v_knots);

                    const Vertex& control_point = control_points[(patch_v + j) * u_count + patch_u + i];

                    vertex.x += spline_u * spline_v * control_point.x;
                    vertex.y += spline_u * spline_v * control_point.y;
                    vertex.z += spline_u * spline_v * control_point.z;

                    if (control_point.has_colors) {
                        vertex.r += spline_u * spline_v * control_point.r;
                        vertex.g += spline_u * spline_v * control_point.g;
                        vertex.b += spline_u * spline_v * control_point.b;
                        vertex.a += spline_u * spline_v * control_point.a;

                        vertex.has_colors = true;
                    }
                }
            }

            logger->trace("Patch vertex (X: {}, Y: {}, Z: {})", vertex.x, vertex.y, vertex.z);
        }
    }

    // Draw the patch mesh
    for (u32 v = 0; v < v_div; v++) {
        for (u32 u = 0; u < u_div; u++) {
            Vertex& a = vertices[(v + 0) * (u_div + 1) + (u + 0)];
            Vertex& b = vertices[(v + 0) * (u_div + 1) + (u + 1)];
            Vertex& c = vertices[(v + 1) * (u_div + 1) + (u + 0)];
            Vertex& d = vertices[(v + 1) * (u_div + 1) + (u + 1)];
            
            draw_triangle(a, b, c);
            draw_triangle(c, b, d);
        }
    }
}

void draw_spline(const u32 u_count, const u32 v_count, const u32 u_knot_type, const u32 v_knot_type) {
    // A patch needs 16 control points, edges overlap so every
    // subsequent patch only needs one extra point in every direction
    assert(u_count >= 4);
    assert(v_count >= 4);

    // The XMB waves rely on shade mapping to look right, so we won't actually
    // draw splines for now
    return;

    const std::vector<Vertex> control_points = fetch_vertices(u_count * v_count);

    for (u32 patch_v = 0; patch_v < (v_count - 3); patch_v++) {
        for (u32 patch_u = 0; patch_u < (u_count - 3); patch_u++) {
            draw_spline_patch(control_points, patch_u, patch_v, u_count, v_count, (KnotType)(u_knot_type & 3), (KnotType)(v_knot_type & 3));
        }
    }
}

};
