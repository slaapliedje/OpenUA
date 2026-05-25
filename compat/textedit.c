/*
 * Mac TextEdit shim — skeleton. See textedit.h.
 *
 * Single-line behavior for the v1 lift: the destRect / viewRect both
 * hold the visible field, text lives in the hText handle, selStart /
 * selEnd track the caret (with selStart == selEnd → caret only). TEKey
 * inserts printable ASCII at selEnd, backspace removes the previous
 * char, arrow keys move the caret. TEUpdate paints the current text
 * with a vertical caret bar on the active field through the 8x8
 * fallback font (CharWidth = 6 px).
 *
 * Multi-line behavior, the word-break / click-loop hooks, justification
 * other than teJustLeft, the cut / copy / paste scrap, the styled
 * variant, and the real caret-blink timer all follow when an engine
 * path demands them.
 */

#include <stddef.h>             /* NULL          */
#include <string.h>             /* memcpy, memmove */

#include "events.h"             /* TickCount     */
#include "macmemory.h"
#include "quickdraw.h"
#include "textedit.h"

#define TE_CHAR_W        6      /* 8x8 fallback font advance width    */
#define TE_LINE_HEIGHT  12      /* 1-line height for the skeleton     */
#define TE_FONT_ASCENT   9      /* approximate baseline drop          */
#define TE_CARET_BLINK  30      /* ticks between caret state flips    */

static const RGBColor kTEBlack = { 0x0000, 0x0000, 0x0000 };
static const RGBColor kTEWhite = { 0xFFFF, 0xFFFF, 0xFFFF };

static char *te_text(TEHandle hTE)
{
	if (hTE == NULL || *hTE == NULL || (*hTE)->hText == NULL
	 || *(*hTE)->hText == NULL)
		return NULL;
	return (char *)*(*hTE)->hText;
}

static void te_ensure_text(TEHandle hTE)
{
	if (hTE == NULL || *hTE == NULL)
		return;
	if ((*hTE)->hText == NULL) {
		(*hTE)->hText = NewHandleClear(1);
		if ((*hTE)->hText != NULL)
			(*hTE)->teLength = 0;
	}
}

TEHandle TENew(const Rect *destRect, const Rect *viewRect)
{
	TEHandle h;
	TERec   *r;

	if (destRect == NULL || viewRect == NULL)
		return NULL;
	h = (TEHandle)NewHandleClear((Size)sizeof(TERec));
	if (h == NULL)
		return NULL;
	r = *h;
	r->destRect    = *destRect;
	r->viewRect    = *viewRect;
	r->selRect     = *destRect;
	r->lineHeight  = TE_LINE_HEIGHT;
	r->fontAscent  = TE_FONT_ASCENT;
	r->just        = teJustLeft;
	r->crOnly      = -1;            /* -1 = no word wrap (single line) */
	r->hText       = NewHandleClear(1);
	r->teLength    = 0;
	r->nLines      = 1;
	r->lineStarts[0] = 0;
	GetPort(&r->inPort);
	return h;
}

void TEDispose(TEHandle hTE)
{
	if (hTE == NULL || *hTE == NULL)
		return;
	if ((*hTE)->hText != NULL)
		DisposeHandle((*hTE)->hText);
	DisposeHandle((Handle)hTE);
}

void TESetText(const void *text, long length, TEHandle hTE)
{
	TERec *r;
	char  *dst;

	if (hTE == NULL || *hTE == NULL || length < 0)
		return;
	if (length > 0x7FFF)
		length = 0x7FFF;
	r = *hTE;
	if (r->hText == NULL)
		r->hText = NewHandleClear(length + 1);
	else
		SetHandleSize(r->hText, length + 1);
	if (r->hText == NULL || *r->hText == NULL)
		return;
	dst = (char *)*r->hText;
	if (length > 0 && text != NULL)
		memcpy(dst, text, (size_t)length);
	dst[length] = 0;
	r->teLength = (short)length;
	r->selStart = (short)length;
	r->selEnd   = (short)length;
}

Handle TEGetText(TEHandle hTE)
{
	te_ensure_text(hTE);
	return (hTE != NULL && *hTE != NULL) ? (*hTE)->hText : NULL;
}

void TESetSelect(long selStart, long selEnd, TEHandle hTE)
{
	TERec *r;

	if (hTE == NULL || *hTE == NULL)
		return;
	r = *hTE;
	if (selStart < 0) selStart = 0;
	if (selEnd   < 0) selEnd   = 0;
	if (selStart > r->teLength) selStart = r->teLength;
	if (selEnd   > r->teLength) selEnd   = r->teLength;
	if (selStart > selEnd) {
		long t = selStart;
		selStart = selEnd;
		selEnd   = t;
	}
	r->selStart = (short)selStart;
	r->selEnd   = (short)selEnd;
}

/* Insert printable ASCII / handle backspace, arrows. */
void TEKey(short key, TEHandle hTE)
{
	TERec  *r;
	char   *text;
	short   ch;
	short   pos;

	if (hTE == NULL || *hTE == NULL)
		return;
	r = *hTE;
	te_ensure_text(hTE);
	text = te_text(hTE);
	if (text == NULL)
		return;

	ch  = (short)(key & 0xFF);
	pos = r->selEnd;

	/* Replace any selection with the typed char (or simple delete). */
	if (r->selStart != r->selEnd) {
		short del = (short)(r->selEnd - r->selStart);
		short tail = (short)(r->teLength - r->selEnd);

		if (tail > 0)
			memmove(text + r->selStart, text + r->selEnd,
			        (size_t)tail);
		r->teLength -= del;
		r->selEnd    = r->selStart;
		pos          = r->selStart;
		text[r->teLength] = 0;
	}

	switch (ch) {
	case 0x08:                              /* backspace */
		if (pos > 0) {
			memmove(text + pos - 1, text + pos,
			        (size_t)(r->teLength - pos));
			r->teLength--;
			r->selStart = (short)(pos - 1);
			r->selEnd   = r->selStart;
			text[r->teLength] = 0;
		}
		return;
	case 0x1C:                              /* left arrow */
		if (pos > 0) {
			r->selStart = (short)(pos - 1);
			r->selEnd   = r->selStart;
		}
		return;
	case 0x1D:                              /* right arrow */
		if (pos < r->teLength) {
			r->selStart = (short)(pos + 1);
			r->selEnd   = r->selStart;
		}
		return;
	case 0x1E:                              /* up arrow — single-line: home */
		r->selStart = 0;
		r->selEnd   = 0;
		return;
	case 0x1F:                              /* down arrow — single-line: end */
		r->selStart = r->teLength;
		r->selEnd   = r->teLength;
		return;
	default:
		break;
	}

	if (ch < 0x20 || ch >= 0x7F)
		return;                          /* unprintable */

	/* Grow text storage if needed (one byte at a time). */
	if (r->hText != NULL)
		SetHandleSize(r->hText, r->teLength + 2);
	text = te_text(hTE);
	if (text == NULL)
		return;

	if (pos < r->teLength)
		memmove(text + pos + 1, text + pos,
		        (size_t)(r->teLength - pos));
	text[pos] = (char)ch;
	r->teLength++;
	r->selStart = (short)(pos + 1);
	r->selEnd   = r->selStart;
	text[r->teLength] = 0;
}

void TEClick(Point pt, Boolean extend, TEHandle hTE)
{
	TERec *r;
	short  rel_x, char_idx;

	if (hTE == NULL || *hTE == NULL)
		return;
	r = *hTE;
	r->clickTime = TickCount();
	r->selPoint  = pt;

	rel_x = (short)(pt.h - r->destRect.left);
	if (rel_x < 0)
		rel_x = 0;
	char_idx = (short)(rel_x / TE_CHAR_W);
	if (char_idx > r->teLength)
		char_idx = r->teLength;

	if (extend) {
		if (char_idx < r->selStart)
			r->selStart = char_idx;
		else
			r->selEnd = char_idx;
	} else {
		r->selStart = char_idx;
		r->selEnd   = char_idx;
	}
	r->clickLoc = char_idx;
}

void TEActivate(TEHandle hTE)
{
	if (hTE == NULL || *hTE == NULL)
		return;
	(*hTE)->active     = 1;
	(*hTE)->caretState = 1;
	(*hTE)->caretTime  = TickCount();
}

void TEDeactivate(TEHandle hTE)
{
	if (hTE == NULL || *hTE == NULL)
		return;
	(*hTE)->active     = 0;
	(*hTE)->caretState = 0;
}

void TEUpdate(const Rect *updateRect, TEHandle hTE)
{
	TERec  *r;
	char   *text;
	GrafPtr saved;
	short   baseline_y, caret_x;
	unsigned char pstr[256];
	short   i;

	(void)updateRect;
	if (hTE == NULL || *hTE == NULL)
		return;
	r = *hTE;
	te_ensure_text(hTE);
	text = te_text(hTE);

	GetPort(&saved);
	SetPort(qd_screen_port());

	/* Background. */
	RGBForeColor(&kTEWhite);
	PaintRect(&r->viewRect);
	RGBForeColor(&kTEBlack);

	/* Text. Render the live string as a Pascal string for DrawString. */
	if (text != NULL && r->teLength > 0) {
		short n = r->teLength;

		if (n > 255)
			n = 255;
		pstr[0] = (unsigned char)n;
		for (i = 0; i < n; i++)
			pstr[i + 1] = (unsigned char)text[i];
		baseline_y = (short)(r->destRect.top + r->fontAscent);
		MoveTo((short)(r->destRect.left + 2), baseline_y);
		DrawString((ConstStr255Param)pstr);
	}

	/* Caret at selEnd when active. */
	if (r->active && r->caretState) {
		caret_x = (short)(r->destRect.left + 2
		                  + r->selEnd * TE_CHAR_W);
		MoveTo(caret_x, (short)(r->destRect.top + 1));
		LineTo(caret_x, (short)(r->destRect.bottom - 2));
	}
	SetPort(saved);
}

void TEIdle(TEHandle hTE)
{
	TERec *r;
	long   now;

	if (hTE == NULL || *hTE == NULL)
		return;
	r = *hTE;
	if (!r->active)
		return;
	now = TickCount();
	if (now - r->caretTime >= TE_CARET_BLINK) {
		r->caretState = (short)(r->caretState ? 0 : 1);
		r->caretTime  = now;
		TEUpdate(NULL, hTE);
	}
}
