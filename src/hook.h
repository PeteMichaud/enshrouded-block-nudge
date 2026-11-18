#pragma once

// Finds the game's cursor transform function by signature and post-hooks it. Returns false,
// with the reason in the log and nothing installed, if that is not safely possible.
bool hook_install();
