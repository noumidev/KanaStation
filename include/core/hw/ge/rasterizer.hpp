/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* core/hw/ge/rasterizer.hpp - Software rasterizer */

#pragma once

#include <common/types.hpp>

namespace kanacore::hw::ge::rasterizer {

void initialize();
void soft_reset();
void hard_reset();
void shutdown();

// Got a bit too lazy with some of these, ideally decoding the params
// happens in GE
void set_base(const common::u32 data);
void set_vertex_addr(const common::u32 data);
void set_index_addr(const common::u32 data);
void set_vertex_type(const common::u32 data);
void set_region_upper(const common::u32 data);
void set_region_lower(const common::u32 data);
void set_lighting_enable(const bool enable);
void set_light_enable(const common::u32 idx, const bool enable);
void set_clipping_enable(const bool enable);
void set_backface_culling_enable(const bool enable);
void set_texture_mapping_enable(const bool enable);
void set_fogging_enable(const bool enable);
void set_dithering_enable(const bool enable);
void set_alpha_blending_enable(const bool enable);
void set_alpha_test_enable(const bool enable);
void set_depth_test_enable(const bool enable);
void set_stencil_test_enable(const bool enable);
void set_antialiasing_enable(const bool enable);
void set_patch_culling_enable(const bool enable);
void set_color_test_enable(const bool enable);
void set_logic_operation_enable(const bool enable);
void set_morph_weight(const common::u32 idx, const common::f32 data);
void set_patch_division(const common::u32 u_div, const common::u32 v_div);
void set_patch_primitive(const common::u32 primitive);
void set_patch_face(const bool is_ccw);
void set_viewport_scale(const common::u32 idx, const common::f32 data);
void set_viewport_offset(const common::u32 idx, const common::f32 data);
void set_texture_scale(const common::u32 idx, const common::f32 data);
void set_texture_offset(const common::u32 idx, const common::f32 data);
void set_offset_x(const common::f32 data);
void set_offset_y(const common::f32 data);
void set_gouraud_shading_enable(const bool enable);
void set_model_color(const common::u32 idx, const common::u32 data);
void set_model_alpha(const common::u32 data);
void set_ambient_color(const common::u32 data);
void set_ambient_alpha(const common::u32 data);
void set_framebuffer_base(const common::u32 addr_lo);
void set_framebuffer_width(const common::u32 width);
void set_depth_buffer_base(const common::u32 addr_lo);
void set_depth_buffer_width(const common::u32 width);
void set_texture_base(const common::u32 idx, const common::u32 addr_lo);
void set_texture_buffer_width(const common::u32 idx, const common::u32 addr_hi, const common::u32 width);
void set_clut_base_lo(const common::u32 addr_lo);
void set_clut_base_hi(const common::u32 addr_hi);
void set_texture_size(const common::u32 idx, const common::u32 width, const common::u32 height);
void set_texture_format(const common::u32 format);
void load_clut(const common::u32 num_palettes);
void set_clut(const common::u32 data);
void set_texture_blend_params(const common::u32 data);
void set_scissor_upper(const common::u32 data);
void set_scissor_lower(const common::u32 data);
void set_minimum_depth(const common::u16 minz);
void set_maximum_depth(const common::u16 minz);
void set_alpha_test(const common::u32 data);
void set_depth_test(const common::u32 data);
void set_blend_params(const common::u32 data);
void set_fixed_color_a(const common::u32 data);
void set_fixed_color_b(const common::u32 data);
void set_dither_matrix(const common::u32 idx, const common::u32 data);
void set_color_mask(const common::u32 data);
void set_alpha_mask(const common::u32 data);

common::u32 get_base();
common::u32 get_vertex_addr();
common::u32 get_index_addr();

void draw_primitive(const common::u32 count, const common::u32 prim_type);
void draw_bezier(const common::u32 u_count, const common::u32 v_count);
void draw_spline(
    const common::u32 u_count,
    const common::u32 v_count,
    const common::u32 u_knot_type,
    const common::u32 v_knot_type
);

};
