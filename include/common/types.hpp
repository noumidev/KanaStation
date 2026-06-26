/*
 * KanaStation is an experimental PlayStation Portable emulator.
 * Copyright (C) 2026  noumidev
 */

/* common/types.hpp - Important type definitions */

#pragma once

#include <cinttypes>

namespace common {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using f32 = float;
using f64 = double;

template<typename T>
inline T byteswap(const T data);

template<>
inline u16 byteswap(const u16 data) {
    return (data >> 8) | (data << 8);
}

template<>
inline u32 byteswap(const u32 data) {
    return (data >> 24) | ((data >> 8) & 0xFF00) | ((data & 0xFF00) << 8) | (data << 24);
}

template<>
inline u64 byteswap(const u64 data) {
    u64 n = data;

    n = ((n & 0x00000000FFFFFFFFULL) << 32) | ((n & 0xFFFFFFFF00000000ULL) >> 32);
    n = ((n & 0x0000FFFF0000FFFFULL) << 16) | ((n & 0xFFFF0000FFFF0000ULL) >> 16);
    n = ((n & 0x00FF00FF00FF00FFULL) <<  8) | ((n & 0xFF00FF00FF00FF00ULL) >>  8);

    return n;
}

}
