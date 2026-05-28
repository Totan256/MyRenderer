#pragma once
#include <cstdint>

struct StringHash {
    uint32_t value;

    constexpr operator uint32_t() const noexcept {
        return value;
    }
    constexpr bool operator==(const StringHash& other) const noexcept {
        return value == other.value;
    }
};

// FNV-1a ハッシュアルゴリズム
constexpr StringHash hashString(const char* str) {
    uint32_t hash = 2166136261u;
    while (*str) {
        hash ^= static_cast<uint32_t>(*str++);
        hash *= 16777619u;
    }
    return {hash};
}

// 文字列リテラルで "name"_hash と書けるようにする
constexpr StringHash operator"" _hash(const char* str, size_t) {
    return hashString(str);
}