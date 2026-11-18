// Byte pattern scanner. No Windows dependencies.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace scan {

struct Pattern {
    std::vector<uint8_t> bytes;
    std::vector<uint8_t> wild;  // 1 = matches any byte
    bool ok = false;
};

// "48 8B ?? C4": hex bytes separated by spaces, "?" or "??" for a wildcard.
Pattern parse(const char* text);

// The same pattern with its first n bytes as wildcards (the bytes a hook overwrites).
Pattern wildcard_head(const Pattern& p, size_t n);

// Stores up to maxHits matches and returns how many there are in total.
size_t find_all(const uint8_t* data, size_t size, const Pattern& p, const uint8_t** hits, size_t maxHits);

// Whether code starts with a jump another mod's hook would have written: E9 rel32,
// FF 25 [rip], mov rax, imm64; jmp rax, or mov r11, imm64; jmp r11.
bool starts_with_jump(const uint8_t* code, size_t avail);

}  // namespace scan
