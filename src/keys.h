// Key names for the ini file. No Windows dependencies; values are Windows virtual-key codes.
#pragma once
#include <cstddef>

namespace keys {

// A name such as "H", "PageUp", "LShift" or "F7", or a number (decimal or 0x hex).
// Returns the virtual-key code, 0 for "None" or an empty string, -1 if not understood.
int parse(const char* name);

// Writes a name parse() accepts back.
void name_of(int vk, char* out, size_t cap);

}  // namespace keys
