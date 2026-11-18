#include "scan.h"

#include <cstring>

namespace scan {

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

Pattern parse(const char* text) {
    Pattern p;
    if (!text) return p;
    const char* s = text;
    for (;;) {
        while (*s == ' ') s++;
        if (!*s) break;
        const char* e = s;
        while (*e && *e != ' ') e++;
        const size_t len = (size_t)(e - s);
        if ((len == 1 && s[0] == '?') || (len == 2 && s[0] == '?' && s[1] == '?')) {
            p.bytes.push_back(0);
            p.wild.push_back(1);
        } else if (len == 2 && hex_digit(s[0]) >= 0 && hex_digit(s[1]) >= 0) {
            p.bytes.push_back((uint8_t)(hex_digit(s[0]) * 16 + hex_digit(s[1])));
            p.wild.push_back(0);
        } else {
            return Pattern{};
        }
        s = e;
    }
    // A pattern of only wildcards would match everywhere.
    bool anyFixed = false;
    for (uint8_t w : p.wild) anyFixed |= !w;
    p.ok = anyFixed;
    return p;
}

Pattern wildcard_head(const Pattern& p, size_t n) {
    Pattern r = p;
    for (size_t i = 0; i < n && i < r.wild.size(); i++) {
        r.wild[i] = 1;
        r.bytes[i] = 0;
    }
    bool anyFixed = false;
    for (uint8_t w : r.wild) anyFixed |= !w;
    r.ok = p.ok && anyFixed;
    return r;
}

size_t find_all(const uint8_t* data, size_t size, const Pattern& p, const uint8_t** hits, size_t maxHits) {
    const size_t len = p.bytes.size();
    if (!p.ok || !data || size < len) return 0;

    // Search for the first fixed byte with memchr, then compare the rest.
    size_t anchor = 0;
    while (p.wild[anchor]) anchor++;
    const uint8_t anchorByte = p.bytes[anchor];

    size_t count = 0;
    const size_t lastStart = size - len;
    size_t start = 0;
    while (start <= lastStart) {
        const uint8_t* from = data + start + anchor;
        const uint8_t* found = (const uint8_t*)memchr(from, anchorByte, lastStart - start + 1);
        if (!found) break;
        start = (size_t)(found - data) - anchor;
        bool match = true;
        for (size_t i = anchor + 1; i < len; i++) {
            if (!p.wild[i] && data[start + i] != p.bytes[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            if (count < maxHits && hits) hits[count] = data + start;
            count++;
        }
        start++;
    }
    return count;
}

bool starts_with_jump(const uint8_t* c, size_t avail) {
    if (!c) return false;
    if (avail >= 5 && c[0] == 0xE9) return true;
    if (avail >= 6 && c[0] == 0xFF && c[1] == 0x25) return true;
    if (avail >= 12 && c[0] == 0x48 && c[1] == 0xB8 && c[10] == 0xFF && c[11] == 0xE0) return true;
    if (avail >= 13 && c[0] == 0x49 && c[1] == 0xBB && c[10] == 0x41 && c[11] == 0xFF && c[12] == 0xE3) return true;
    return false;
}

}  // namespace scan
