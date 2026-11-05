#pragma once
#include <stdint.h>

struct payload;

/* The payload build.sh appended behind the shim, or 0. */
const struct payload *loader_find(void);
/* Header and memory sanity; prints a summary. Returns an error string or 0. */
const char *loader_check(const struct payload *p);
/* Copy and jump; only returns (with an error string) if xnu does. */
const char *loader_boot(const struct payload *p, uintptr_t fb);
