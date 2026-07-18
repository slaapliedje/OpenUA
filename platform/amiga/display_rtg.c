/*
 * Amiga RTG display backend (ADR-0012) — Picasso96/CyberGraphX cards via
 * the OS, for machines whose native chipset can't carry the game: an ECS
 * Amiga (A500/A2000/A600) with a graphics card (PiStorm RTG, ZZ9000,
 * Picasso II...) runs the full 256-colour game here, no bitplanes involved.
 *
 * Deliberately API-minimal: everything is stock graphics.library/
 * intuition.library — BestModeIDTags finds an 8-bit 320x200-capable mode,
 * OpenScreenTags opens a quiet 8-bit screen on it, WriteChunkyPixels
 * pushes the engine's chunky rows (the RTG layer intercepts it as a fast
 * chunky blit on card screens), and LoadRGB32 drives the palette — so no
 * Picasso96API/cybergraphics headers or link-time deps exist. On a P96
 * screen palette loads are hardware CLUT writes: palette animation (the
 * fireplace) stays free.
 *
 * dsp_detect (display_aga.c) picks THIS backend only when the machine has
 * no AA chipset — on AGA the copper backend is strictly better (free
 * palette-in-copper, sprite pointer). Here the pointer is the shim's
 * software composite (plat_cursor_active() reports inactive because the
 * AGA sprite path never armed), pushed through present_rect.
 *
 * The input stack is display-independent: the VERTB server ticks, JOY0DAT
 * mouse and the input.device keyboard handler work unchanged, and Paula
 * audio doesn't care what Denise is (or isn't) doing.
 */

#include "display.h"
#include "dbglog.h"

#ifdef FRUA_AMIGA

#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/screens.h>
#include <graphics/modeid.h>
#include <graphics/gfx.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#define RTG_W  320
#define RTG_H  200

extern struct GfxBase *GfxBase;         /* display_aga.c owns the symbol */
struct IntuitionBase *IntuitionBase;

static struct Screen *s_screen;
static unsigned char *s_chunky;
static dsp_surface_t  s_surface;

static int rtg_init(short want_w, short want_h)
{
	ULONG modeid;

	(void)want_w; (void)want_h;

	/* GfxBase is display_aga.c's global; on this path aga_init never ran,
	 * so take our own open of both libraries. */
	if (GfxBase == NULL) {
		GfxBase = (struct GfxBase *)
		    OpenLibrary((CONST_STRPTR)"graphics.library", 39);
		if (GfxBase == NULL) {
			dbg_log("rtg: graphics.library v39 open failed");
			return 1;
		}
	}
	IntuitionBase = (struct IntuitionBase *)
	    OpenLibrary((CONST_STRPTR)"intuition.library", 39);
	if (IntuitionBase == NULL) {
		dbg_log("rtg: intuition.library v39 open failed");
		return 1;
	}

	modeid = BestModeID(BIDTAG_NominalWidth,  RTG_W,
	                    BIDTAG_NominalHeight, RTG_H,
	                    BIDTAG_Depth,         8,
	                    TAG_DONE);
	if (modeid == (ULONG)INVALID_ID) {
		/* No 8-bit mode at 320x200 — try the ubiquitous 640x480 card
		 * mode; the screen just carries a border around the image. */
		modeid = BestModeID(BIDTAG_NominalWidth,  640,
		                    BIDTAG_NominalHeight, 480,
		                    BIDTAG_Depth,         8,
		                    TAG_DONE);
	}
	if (modeid == (ULONG)INVALID_ID) {
		dbg_log("rtg: no 8-bit display mode (no RTG card?)");
		CloseLibrary((struct Library *)IntuitionBase);
		IntuitionBase = NULL;
		return 1;
	}
	dbg_log_num("rtg: mode id = ", (long)modeid);

	s_chunky = AllocMem((ULONG)RTG_W * RTG_H, MEMF_ANY | MEMF_CLEAR);
	if (s_chunky == NULL) {
		CloseLibrary((struct Library *)IntuitionBase);
		IntuitionBase = NULL;
		return 1;
	}

	s_screen = OpenScreenTags(NULL,
	                          SA_DisplayID, modeid,
	                          SA_Width,     RTG_W,
	                          SA_Height,    RTG_H,
	                          SA_Depth,     8,
	                          SA_Quiet,     TRUE,
	                          SA_ShowTitle, FALSE,
	                          SA_Type,      CUSTOMSCREEN,
	                          TAG_DONE);
	if (s_screen == NULL) {
		/* The 320x200 open can fail on card drivers that only do the
		 * mode's nominal size — retry letting the screen be as big as
		 * the mode wants; the game image sits top-left. */
		s_screen = OpenScreenTags(NULL,
		                          SA_DisplayID, modeid,
		                          SA_Depth,     8,
		                          SA_Quiet,     TRUE,
		                          SA_ShowTitle, FALSE,
		                          SA_Type,      CUSTOMSCREEN,
		                          TAG_DONE);
	}
	if (s_screen == NULL) {
		dbg_log("rtg: OpenScreenTags failed");
		FreeMem(s_chunky, (ULONG)RTG_W * RTG_H);
		s_chunky = NULL;
		CloseLibrary((struct Library *)IntuitionBase);
		IntuitionBase = NULL;
		return 1;
	}
	dbg_log_num("rtg: screen w = ", s_screen->Width);
	dbg_log_num("rtg: screen h = ", s_screen->Height);

	s_surface.width  = RTG_W;
	s_surface.height = RTG_H;
	s_surface.pitch  = RTG_W;
	s_surface.pixels = s_chunky;
	return 0;
}

static void rtg_shutdown(void)
{
	if (s_screen != NULL) {
		CloseScreen(s_screen);
		s_screen = NULL;
	}
	if (s_chunky != NULL) {
		FreeMem(s_chunky, (ULONG)RTG_W * RTG_H);
		s_chunky = NULL;
	}
	if (IntuitionBase != NULL) {
		CloseLibrary((struct Library *)IntuitionBase);
		IntuitionBase = NULL;
	}
}

static dsp_surface_t *rtg_surface(void)
{
	return &s_surface;
}

static void rtg_present_rect(short x, short y, short w, short h)
{
	if (s_screen == NULL)
		return;
	if (x < 0) { w = (short)(w + x); x = 0; }
	if (y < 0) { h = (short)(h + y); y = 0; }
	if (x + w > RTG_W) w = (short)(RTG_W - x);
	if (y + h > RTG_H) h = (short)(RTG_H - y);
	if (w <= 0 || h <= 0)
		return;

	/* graphics.library v40+: chunky rows straight into the RastPort.
	 * On an RTG screen the card driver takes this as a chunky blit. */
	WriteChunkyPixels(&s_screen->RastPort,
	                  x, y, (short)(x + w - 1), (short)(y + h - 1),
	                  s_chunky + (long)y * RTG_W + x, RTG_W);
}

static void rtg_present(void)
{
	rtg_present_rect(0, 0, RTG_W, RTG_H);
}

static void rtg_set_palette(const dsp_color_t *colors, short first, short count)
{
	/* LoadRGB32 wants: 1 longword (count<<16 | first), then 3 longwords
	 * of LEFT-JUSTIFIED 32-bit gun values per entry, then a 0 terminator. */
	static ULONG spec[1 + 256 * 3 + 1];
	ULONG *p = spec;
	short  i;

	if (s_screen == NULL || count <= 0)
		return;
	if (first < 0 || first >= 256)
		return;
	if (first + count > 256)
		count = (short)(256 - first);

	*p++ = ((ULONG)count << 16) | (UWORD)first;
	for (i = 0; i < count; i++) {
		*p++ = (ULONG)colors[i].r * 0x01010101UL;
		*p++ = (ULONG)colors[i].g * 0x01010101UL;
		*p++ = (ULONG)colors[i].b * 0x01010101UL;
	}
	*p = 0;
	LoadRGB32(&s_screen->ViewPort, spec);
}

static const dsp_backend_t rtg_backend = {
	"amiga-rtg (OS chunky screen)",
	rtg_init,
	rtg_shutdown,
	rtg_surface,
	rtg_present,
	rtg_present_rect,
	rtg_set_palette,
	1,                      /* single-buffered (WriteChunkyPixels straight to the
	                         * OS screen, no flip): one present seeds the frame.
	                         * Was 0, which the qd clamp already made 1 — made
	                         * explicit so it is not mistaken for an oversight. */
};

const dsp_backend_t *dsp_backend_rtg(void)
{
	return &rtg_backend;
}

#endif /* FRUA_AMIGA */
