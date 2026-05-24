#pragma once
#include <cstdint>

using StringHash = uint32_t;

// FNV-1a ハッシュアルゴリズム
constexpr uint32_t hashString(const char* str) {
    uint32_t hash = 2166136261u;
    while (*str) {
        hash ^= static_cast<uint32_t>(*str++);
        hash *= 16777619u;
    }
    return hash;
}

// 文字列リテラルで "name"_hash と書けるようにする
constexpr StringHash operator"" _hash(const char* str, size_t) {
    return hashString(str);
}