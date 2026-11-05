#pragma once
#include <stdint.h>

/* Window A as hekate left it (read before we touch anything). */
struct dc_state {
	uint32_t win_options, color_depth, size, stride, start, kind;
};

/* Point window A at fb. Returns 0 if only START_ADDR changed, 1 if the
 * window had to be fully reprogrammed. */
int display_take_over(uintptr_t fb, struct dc_state *st);
