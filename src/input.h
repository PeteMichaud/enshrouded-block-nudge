#pragma once
#include "config.h"

// Starts the thread that polls the keys and owns the lock state.
void input_start(const Config& config);
