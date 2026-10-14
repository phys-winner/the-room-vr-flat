#pragma once
#include <stddef.h>
#include <stdint.h>

// Installs an inline hook. Returns the trampoline that calls the original code when want_orig != 0,
// (void*)1 on success without trampoline, or NULL on failure (err explains; nothing was written).
void* rf_hook_install(void* target, void* repl, const uint8_t* expect16, int want_orig, char* err, size_t errlen);
