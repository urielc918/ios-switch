/*
 * Move display controller A's window A to our own framebuffer. hekate
 * leaves the panel running (sld_type=1) with window A in pitch mode on its
 * own 720x1280 B8G8R8A8 buffer, so normally only START_ADDR needs to change.
 * If the window doesn't look like that, program it fully (values in NOTES.md).
 */
#include "t210.h"
#include "display.h"

#define DC(r)			(DC_A_BASE + ((r) << 2))
#define DC_CMD_DISPLAY_COMMAND	0x32
#define DC_CMD_STATE_CONTROL	0x41
#define  GENERAL_ACT_REQ	BIT(0)
#define  WIN_A_ACT_REQ		BIT(1)
#define  GENERAL_UPDATE		BIT(8)
#define  WIN_A_UPDATE		BIT(9)
#define DC_CMD_WINDOW_HEADER	0x42
#define  WINDOW_A_SELECT	BIT(4)
#define DC_DISP_WIN_OPTIONS	0x402
#define  DSI_ENABLE		BIT(29)
#define DC_WIN_WIN_OPTIONS	0x700
#define  WIN_ENABLE		BIT(30)
#define DC_WIN_BUFFER_CONTROL	0x702
#define DC_WIN_COLOR_DEPTH	0x703
#define  WIN_COLOR_DEPTH_B8G8R8A8 0xC
#define DC_WIN_POSITION		0x704
#define DC_WIN_SIZE		0x705
#define DC_WIN_PRESCALED_SIZE	0x706
#define DC_WIN_H_INITIAL_DDA	0x707
#define DC_WIN_V_INITIAL_DDA	0x708
#define DC_WIN_DDA_INC		0x709
#define DC_WIN_LINE_STRIDE	0x70A
#define DC_WINBUF_START_ADDR	0x800
#define DC_WINBUF_ADDR_H_OFFSET	0x806
#define DC_WINBUF_ADDR_V_OFFSET	0x808
#define DC_WINBUF_SURFACE_KIND	0x80B

#define W	720
#define H	1280

static void commit(void)
{
	wr32(DC(DC_CMD_STATE_CONTROL), GENERAL_UPDATE | WIN_A_UPDATE);
	wr32(DC(DC_CMD_STATE_CONTROL), GENERAL_ACT_REQ | WIN_A_ACT_REQ);
}

int display_take_over(uintptr_t fb, struct dc_state *st)
{
	wr32(DC(DC_CMD_WINDOW_HEADER), WINDOW_A_SELECT);
	st->win_options = rd32(DC(DC_WIN_WIN_OPTIONS));
	st->color_depth = rd32(DC(DC_WIN_COLOR_DEPTH));
	st->size = rd32(DC(DC_WIN_SIZE));
	st->stride = rd32(DC(DC_WIN_LINE_STRIDE));
	st->start = rd32(DC(DC_WINBUF_START_ADDR));
	st->kind = rd32(DC(DC_WINBUF_SURFACE_KIND));

	int as_expected = (st->win_options & WIN_ENABLE) &&
			  st->color_depth == WIN_COLOR_DEPTH_B8G8R8A8 &&
			  st->size == ((H << 16) | W) &&
			  (st->stride & 0xFFFF) == W * 4 &&
			  st->kind == 0;
	if (as_expected) {
		wr32(DC(DC_WINBUF_START_ADDR), (uint32_t)fb);
		commit();
		return 0;
	}

	/* Full window A setup, pitch-linear. */
	wr32(DC(DC_WIN_WIN_OPTIONS), 0);
	wr32(DC(DC_WIN_COLOR_DEPTH), WIN_COLOR_DEPTH_B8G8R8A8);
	wr32(DC(DC_WIN_POSITION), 0);
	wr32(DC(DC_WIN_H_INITIAL_DDA), 0);
	wr32(DC(DC_WIN_V_INITIAL_DDA), 0);
	wr32(DC(DC_WIN_PRESCALED_SIZE), (H << 16) | (W * 4));
	wr32(DC(DC_WIN_DDA_INC), 0x10001000);
	wr32(DC(DC_WIN_SIZE), (H << 16) | W);
	wr32(DC(DC_WIN_LINE_STRIDE), ((W * 2) << 16) | (W * 4));
	wr32(DC(DC_WIN_BUFFER_CONTROL), 0);
	wr32(DC(DC_WINBUF_SURFACE_KIND), 0);
	wr32(DC(DC_WINBUF_START_ADDR), (uint32_t)fb);
	wr32(DC(DC_WINBUF_ADDR_H_OFFSET), 0);
	wr32(DC(DC_WINBUF_ADDR_V_OFFSET), 0);
	wr32(DC(DC_WIN_WIN_OPTIONS), WIN_ENABLE);
	wr32(DC(DC_DISP_WIN_OPTIONS), DSI_ENABLE);
	wr32(DC(DC_CMD_DISPLAY_COMMAND), BIT(5));	/* C_DISPLAY */
	commit();
	return 1;
}
