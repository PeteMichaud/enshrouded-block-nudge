#include "keys.h"

#include <cstdio>
#include <cstdlib>

namespace keys {

struct Named {
    const char* name;
    int vk;
};

static const Named kNamed[] = {
    {"None", 0},
    {"Left", 0x25}, {"Up", 0x26}, {"Right", 0x27}, {"Down", 0x28},
    {"PageUp", 0x21}, {"PageDown", 0x22}, {"End", 0x23}, {"Home", 0x24},
    {"Insert", 0x2D}, {"Delete", 0x2E},
    {"LShift", 0xA0}, {"RShift", 0xA1}, {"LCtrl", 0xA2}, {"RCtrl", 0xA3},
    {"LAlt", 0xA4}, {"RAlt", 0xA5}, {"Shift", 0x10}, {"Ctrl", 0x11}, {"Alt", 0x12},
    {"Space", 0x20}, {"Tab", 0x09}, {"Backspace", 0x08}, {"Enter", 0x0D},
    {"Mouse3", 0x04}, {"Mouse4", 0x05}, {"Mouse5", 0x06},
};

static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }

static bool same(const char* a, const char* b) {
    while (*a && *b) {
        if (lower(*a) != lower(*b)) return false;
        a++;
        b++;
    }
    return !*a && !*b;
}

// A whole-string number in [lo, hi], or -1.
static int number(const char* s, int base, int lo, int hi) {
    if (!*s) return -1;
    char* end = nullptr;
    const long v = strtol(s, &end, base);
    if (*end || v < lo || v > hi) return -1;
    return (int)v;
}

int parse(const char* text) {
    if (!text) return 0;
    char s[32];
    size_t n = 0;
    while (*text == ' ' || *text == '\t') text++;
    while (*text && n < sizeof(s) - 1) s[n++] = *text++;
    if (*text) return -1;
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
    s[n] = 0;
    if (!n) return 0;

    for (const Named& k : kNamed)
        if (same(s, k.name)) return k.vk;

    if (n == 1) {
        const char c = s[0];
        if (c >= 'a' && c <= 'z') return c - 'a' + 'A';
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return c;
        return -1;
    }
    if (s[0] == '0' && lower(s[1]) == 'x') return number(s + 2, 16, 1, 255);
    if (lower(s[0]) == 'f') {
        const int f = number(s + 1, 10, 1, 24);
        if (f > 0) return 0x70 + f - 1;
    }
    if (n == 7 && s[6] >= '0' && s[6] <= '9') {
        const char digit = s[6];
        s[6] = 0;
        if (same(s, "Numpad")) return 0x60 + (digit - '0');
        s[6] = digit;
    }
    return number(s, 10, 1, 255);
}

void name_of(int vk, char* out, size_t cap) {
    if (!cap) return;
    for (const Named& k : kNamed) {
        if (k.vk == vk) {
            snprintf(out, cap, "%s", k.name);
            return;
        }
    }
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) snprintf(out, cap, "%c", vk);
    else if (vk >= 0x70 && vk <= 0x87) snprintf(out, cap, "F%d", vk - 0x70 + 1);
    else if (vk >= 0x60 && vk <= 0x69) snprintf(out, cap, "Numpad%d", vk - 0x60);
    else snprintf(out, cap, "0x%02X", vk);
}

}  // namespace keys
