/*
 * Port-authored A5-world scalars — ADR-0017 / task #71.
 *
 * The A5 world splits into relocations (generated position tables, see
 * a4_map.h), scalar runs recoverable from the user's own DOS CKIT.EXE
 * (g_a5_dos_scalars, likewise positions-only), and this: the residue that is
 * in neither. These are functional constants — bit masks, fills, index
 * ladders, and individual flag/count/default bytes — so they are simply
 * STATED here as the port's own code rather than copied out of the Mac DATA
 * image. That is what makes a build carrying them redistributable.
 *
 * Unlike a4_map.c this file is COMMITTED and needs no resource fork: it is
 * authored, not generated.
 *
 * Deliberately NOT here (see #71): A5-8673, a 97-byte symmetric curve, and
 * A5-804, a 24-byte equal-temperament pitch table. Both look formulaic but no
 * derivation reproduces them byte-exactly — the pitch table mixes rounding
 * modes, the curve fits a quadratic only to +/-3 — so re-expressing them
 * would change behaviour rather than preserve it. The curve is in CKIT.EXE
 * verbatim and ships via the DOS scalar map (split_by_dos partial-run
 * matching); the pitch table is in no DOS file at all and still comes from
 * the DATA replay on Mac-sourced installs.
 */
#include <string.h>

#include "data_pool_replay.h"   /* g_a5_byte */
#include "a5_scalars.h"

void a5_seed_authored_scalars(void)
{
	short i;

	/* Right-shift mask ladder (A5-4650): four (1<<(n+1))-1 bytes, then
	 * eight 0xffff>>n words. Verified byte-exact against the Mac image. */
	for (i = 0; i < 4; i++)
		g_a5_byte(-4650 + i) = (unsigned char)((1u << (i + 1)) - 1u);
	for (i = 0; i < 8; i++) {
		unsigned short m = (unsigned short)(0xffffu >> i);
		g_a5_byte(-4646 + i * 2)     = (unsigned char)(m >> 8);
		g_a5_byte(-4646 + i * 2 + 1) = (unsigned char)(m & 0xff);
	}

	/* Left-shift mask ladder (A5-4615): a lead 0x01, then eight
	 * (0xffff<<n) words. Verified byte-exact. */
	g_a5_byte(-4615) = 0x01;
	for (i = 0; i < 8; i++) {
		unsigned short m = (unsigned short)((0xffffu << i) & 0xffffu);
		g_a5_byte(-4614 + i * 2)     = (unsigned char)(m >> 8);
		g_a5_byte(-4614 + i * 2 + 1) = (unsigned char)(m & 0xff);
	}

	/* Dither pattern (A5-3040): nine 0x55 then 0x55/0xff alternating. */
	for (i = 0; i < 9; i++)
		g_a5_byte(-3040 + i) = 0x55;
	for (i = 0; i < 7; i++)
		g_a5_byte(-3031 + i) = (i & 1) ? 0x55 : 0xff;

	/* A5-7224: the 0xff,0xfd pair repeated. */
	for (i = 0; i < 4; i++)
		g_a5_byte(-7224 + i) = (i & 1) ? 0xfd : 0xff;


	/* Constant fills. */
	memset(&g_a5_byte(-27861), 0x01, 3);
	memset(&g_a5_byte(-27857), 0xff, 3);
	memset(&g_a5_byte(-27853), 0xff, 2);
	memset(&g_a5_byte(-27850), 0x01, 3);
	memset(&g_a5_byte(-17497), 0x3f, 7);
	memset(&g_a5_byte(-13040), 0xff, 2);
	memset(&g_a5_byte(-12296), 0xff, 2);
	memset(&g_a5_byte(-11690), 0x01, 3);
	memset(&g_a5_byte(-11683), 0x01, 3);
	memset(&g_a5_byte(-5943), 0x01, 2);
	memset(&g_a5_byte(-5937), 0xff, 2);
	memset(&g_a5_byte(-5930), 0x01, 2);
	memset(&g_a5_byte(-5920), 0x01, 2);
	memset(&g_a5_byte(-4884), 0xff, 2);

	/* Index ladders (first value + constant step). */
	for (i = 0; i < 2; i++)
		g_a5_byte(-30451 + i) = (unsigned char)(6 + i * 3);
	for (i = 0; i < 2; i++)
		g_a5_byte(-27977 + i) = (unsigned char)(78 + i * -9);
	for (i = 0; i < 2; i++)
		g_a5_byte(-27470 + i) = (unsigned char)(2 + i * -1);
	for (i = 0; i < 2; i++)
		g_a5_byte(-4880 + i) = (unsigned char)(31 + i * 129);

	/* Individual flag / count / default bytes. */
	{
		static const struct { short slot; unsigned char val; } b[] = {
			{ -28046, 0x07 }, { -27980, 0x4e }, { -27956, 0x07 }, { -27846, 0xff },
			{ -23227, 0x0a }, { -23225, 0x0a }, { -23219, 0x1e }, { -23217, 0x0c },
			{ -23215, 0x64 }, { -18877, 0x05 }, { -18484, 0x01 }, { -18396, 0x31 },
			{ -17517, 0x0a }, { -14681, 0x7f }, { -14440, 0x01 },
			{ -14434, 0x01 }, { -14432, 0x01 }, { -14429, 0x01 }, { -13017, 0x02 },
			{ -12912, 0x01 }, { -12649, 0x01 }, { -12289, 0xff }, { -12239, 0x05 },
			{ -12237, 0x04 }, { -12235, 0x06 }, { -12233, 0x04 }, { -12231, 0x02 },
			{ -12229, 0x07 }, { -12227, 0x02 }, { -12223, 0x09 }, { -12221, 0x05 },
			{ -12219, 0x04 }, { -12217, 0x03 }, { -12215, 0x03 }, { -12213, 0x03 },
			{ -12211, 0x01 }, { -12209, 0x01 }, { -12207, 0x01 }, { -12201, 0x04 },
			{ -11692, 0xff }, { -11622, 0x01 }, { -11493, 0x04 }, { -8529, 0x01 },
			{ -6926, 0xff }, { -5946, 0xff }, { -5285, 0x03 }, { -5283, 0x01 },
			{ -5234, 0x41 }, { -4859, 0x0f }, { -4850, 0x01 }, { -4682, 0x02 },
			{ -4670, 0x53 }, { -900, 0x01 }, { -893, 0x01 }, { -131, 0x01 },
		};
		for (i = 0; i < (short)(sizeof b / sizeof b[0]); i++)
			g_a5_byte(b[i].slot) = b[i].val;
	}

	/* Terrain-feature rules matrix (A5-27848, 76 classes x 4 bytes):
	 * {move triple, display tile} per combat/area terrain class. The
	 * combat-field builders stamp class codes into the live map and the
	 * cell drawer picks art via [class*4+3] (l78fa: tile2 = -27848[feat*4
	 * +3]); l1162 charges movement from the same rows. Without these rows
	 * the replay-off build drew tile 0 for every class — the #75 "spurious
	 * wall chunks throughout the combat zone". This is functional game-rule
	 * data (passability kind + move cost + tile index), authored here as
	 * the port's own statement of the rules (the #68 rules-matrix
	 * precedent). Layout is two tile banks: classes 1..31 draw DungCom
	 * tile class-1, classes 32..69 draw WildCom tile class-32; the
	 * multi-cell object/event classes 26..31 and the water class 70 remap
	 * explicitly. The movement kinds:
	 *   S start   01 00 ff      W wall     ff 01 02   O open  01 01 00
	 *   D dense   02 02 00      H heavy    02 01 00   B block ff 01 00
	 *   G ground  01 00 00      X barrier  ff 00 00   Z unused 0
	 * Verified byte-exact against the Mac image (tests/test_a5_scalars.py). */
	{
		static const char kind[77] =
		    "S"                          /*  0                    */
		    "WWWWOWWWO"                  /*  1.. 9                */
		    "WOWOWOWOW"                  /* 10..18                */
		    "WWWWOOWD"                   /* 19..26                */
		    "OOOOO"                      /* 27..31                */
		    "WWWWW"                      /* 32..36                */
		    "OOOOO"                      /* 37..41                */
		    "WWHOO"                      /* 42..46                */
		    "OOOHH"                      /* 47..51                */
		    "OOOBBB"                     /* 52..57                */
		    "OOOGGXBHOB"                 /* 58..67                */
		    "OOO"                        /* 68..70                */
		    "ZZZZZ";                     /* 71..75                */
		static const unsigned char trip[9][4] = {
			/* S */ {  1,   0, 255 }, /* W */ { 255, 1, 2 },
			/* O */ {  1,   1,   0 }, /* D */ {   2, 2, 0 },
			/* H */ {  2,   1,   0 }, /* B */ { 255, 1, 0 },
			/* G */ {  1,   0,   0 }, /* X */ { 255, 0, 0 },
			/* Z */ {  0,   0,   0 },
		};
		static const char kinds[10] = "SWODHBGXZ";
		/* Object/event classes 26..31 draw other tiles; 70 = class 54's. */
		static const unsigned char tile26[6] = { 38, 14, 13, 12, 20, 21 };

		for (i = 0; i < 76; i++) {
			const char    *kp = kind + i;
			short          k  = 0;
			unsigned char  tile;

			while (kinds[k] != *kp)
				k++;
			if (i == 0 || *kp == 'Z') tile = 0;
			else if (i >= 26 && i <= 31) tile = tile26[i - 26];
			else if (i == 70)        tile = 22;
			else if (i >= 32)        tile = (unsigned char)(i - 32);
			else                     tile = (unsigned char)(i - 1);
			g_a5_byte(-27848 + i * 4 + 0) = trip[k][0];
			g_a5_byte(-27848 + i * 4 + 1) = trip[k][1];
			g_a5_byte(-27848 + i * 4 + 2) = trip[k][2];
			g_a5_byte(-27848 + i * 4 + 3) = tile;
		}
	}

	/* Compass direction labels (g_a5_27980): 8 facings x 3 bytes — the
	 * N / NE / E / SE / S / SW / W / NW rose strings. The compass-face
	 * selector (l67ca) reads [facing*3] to pick which FRAME face (pieces
	 * 22-25) to draw; a wrong/zero letter falls through its switch and the
	 * needle vanishes. This 24-byte table is functional lookup data, but the
	 * --refs-from scalar filter dropped it — g_a5_27980 is named once and
	 * indexed off that base, so only the first 4 bytes fell inside the
	 * filter window and the East 'E' (offset 6) was lost. That is exactly
	 * why the replay-off compass rendered as a bare dome (#73). Verified
	 * byte-exact against the Mac image. */
	{
		static const unsigned char dirs[24] = {
			'N',  0,  0,   'N', 'E',  0,   'E',  0,  0,   'S', 'E',  0,
			'S',  0,  0,   'S', 'W',  0,   'W',  0,  0,   'N', 'W',  0,
		};
		memcpy(&g_a5_byte(-27980), dirs, sizeof dirs);
	}

	/* Formula-derivable residue: constant fills and arithmetic
	 * ramps that are neither relocations nor DOS-locatable. Fills
	 * and ramps are structural (a repeated default, an index
	 * sequence) — functional, not creative content — so they are
	 * stated as their formula here. Opaque residue (game/engine
	 * tables whose bytes are not a formula) is deliberately NOT
	 * copied: that would recreate the non-redistributable payload;
	 * it comes from the user's own binary via the DOS map (#68). */
	memset(&g_a5_byte(-30517), 0x06, 3);
	memset(&g_a5_byte(-30485), 0x09, 2);
	memset(&g_a5_byte(-30478), 0x09, 2);
	memset(&g_a5_byte(-29516), 0x02, 2);
	memset(&g_a5_byte(-29237), 0x02, 2);
	memset(&g_a5_byte(-29228), 0x02, 2);
	memset(&g_a5_byte(-29224), 0x02, 2);
	memset(&g_a5_byte(-29215), 0x02, 2);
	memset(&g_a5_byte(-29210), 0x02, 3);
	memset(&g_a5_byte(-29206), 0x02, 2);
	memset(&g_a5_byte(-29201), 0x02, 3);
	memset(&g_a5_byte(-29197), 0x02, 2);
	memset(&g_a5_byte(-29192), 0x02, 3);
	memset(&g_a5_byte(-29188), 0x02, 2);
	memset(&g_a5_byte(-29183), 0x02, 3);
	memset(&g_a5_byte(-29179), 0x02, 2);
	memset(&g_a5_byte(-29174), 0x02, 3);
	memset(&g_a5_byte(-29170), 0x02, 2);
	memset(&g_a5_byte(-29165), 0x02, 3);
	memset(&g_a5_byte(-29161), 0x02, 2);
	memset(&g_a5_byte(-29156), 0x02, 3);
	memset(&g_a5_byte(-29152), 0x02, 2);
	memset(&g_a5_byte(-29147), 0x02, 3);
	memset(&g_a5_byte(-29143), 0x02, 2);
	memset(&g_a5_byte(-29138), 0x02, 3);
	memset(&g_a5_byte(-29134), 0x02, 2);
	memset(&g_a5_byte(-29129), 0x02, 3);
	memset(&g_a5_byte(-29125), 0x02, 2);
	memset(&g_a5_byte(-29120), 0x02, 3);
	memset(&g_a5_byte(-29116), 0x02, 2);
	memset(&g_a5_byte(-29111), 0x02, 3);
	memset(&g_a5_byte(-29107), 0x02, 2);
	memset(&g_a5_byte(-29102), 0x02, 3);
	memset(&g_a5_byte(-29098), 0x02, 2);
	memset(&g_a5_byte(-28014), 0x28, 2);
	memset(&g_a5_byte(-17489), 0x3f, 2);
	memset(&g_a5_byte(-17485), 0x3f, 3);
	memset(&g_a5_byte(-16442), 0x02, 2);
	memset(&g_a5_byte(-16426), 0x02, 2);
	memset(&g_a5_byte(-16410), 0x02, 2);
	memset(&g_a5_byte(-16394), 0x02, 2);
	memset(&g_a5_byte(-15871), 0x01, 4);
	memset(&g_a5_byte(-15567), 0x01, 2);
	memset(&g_a5_byte(-11686), 0xff, 2);
	memset(&g_a5_byte(-11679), 0xff, 3);
	memset(&g_a5_byte(-7892), 0x11, 2);
	memset(&g_a5_byte(-7884), 0x20, 2);
	memset(&g_a5_byte(-5933), 0x01, 2);
	memset(&g_a5_byte(-5926), 0xff, 2);
	memset(&g_a5_byte(-5916), 0xff, 2);
	for (i = 0; i < 3; i++)
		g_a5_byte(-30980 + i) = (unsigned char)(0x6c + i * 1);
	for (i = 0; i < 3; i++)
		g_a5_byte(-29498 + i) = (unsigned char)(0x03 + i * 255);
	for (i = 0; i < 3; i++)
		g_a5_byte(-15610 + i) = (unsigned char)(0x02 + i * 2);
	for (i = 0; i < 3; i++)
		g_a5_byte(-15562 + i) = (unsigned char)(0x02 + i * 2);

	/* Class/alignment legality matrix (g_a5_-30450): 17 classes x 12 bytes,
	 * each row `count, legal-alignment values..., zero pad`. Alignment index
	 * = (law_axis-1)*3 + (good_axis-1): 0=LG 1=LN 2=LE 3=NG 4=NN 5=NE 6=CG
	 * 7=CN 8=CE. l2f8e validates the chargen radios against this; with it
	 * zeroed the good/evil axis has no legal default and never lights (#68
	 * target 1, found by the replay-off chargen diff).
	 *
	 * This is the AD&D 2nd-edition class-alignment RULES MATRIX stated as
	 * data — the paladin's lawful-good-only, the ranger's any-good, the
	 * druid's neutrals — i.e. functional game rules, authored here per the
	 * ADR-0017 option-(c) call extended to rules matrices. Verified
	 * byte-exact against the Mac image (tests/test_a5_scalars.py).
	 *   row 2 = Fighter (any), row 3 = Paladin (LG), row 4 = Ranger
	 *   (good), row 1 = Druid (the five neutral-touching); several
	 *   multiclass rows repeat "any" or "any non-good". */
	{
		static const unsigned char aln[17][12] = {
		 { 9, 0,1,2,3,4,5,6,7,8, 0,0 },        /* 0: any            */
		 { 5, 1,3,4,5,7, 0,0,0,0,0,0 },        /* 1: Druid-neutrals */
		 { 9, 0,1,2,3,4,5,6,7,8, 0,0 },        /* 2: Fighter — any  */
		 { 1, 0, 0,0,0,0,0,0,0,0,0,0 },        /* 3: Paladin — LG   */
		 { 3, 0,3,6, 0,0,0,0,0,0,0,0 },        /* 4: Ranger — good  */
		 { 9, 0,1,2,3,4,5,6,7,8, 0,0 },        /* 5: any            */
		 { 7, 1,2,3,4,5,7,8, 0,0,0,0 },        /* 6: non-good       */
		 { 9, 0,1,2,3,4,5,6,7,8, 0,0 },        /* 7: any            */
		 { 9, 0,1,2,3,4,5,6,7,8, 0,0 },        /* 8: any            */
		 { 9, 0,1,2,3,4,5,6,7,8, 0,0 },        /* 9: any            */
		 { 3, 0,3,6, 0,0,0,0,0,0,0,0 },        /* 10: good          */
		 { 9, 0,1,2,3,4,5,6,7,8, 0,0 },        /* 11: any           */
		 { 9, 0,1,2,3,4,5,6,7,8, 0,0 },        /* 12: any           */
		 { 9, 0,1,2,3,4,5,6,7,8, 0,0 },        /* 13: any           */
		 { 7, 1,2,3,4,5,7,8, 0,0,0,0 },        /* 14: non-good      */
		 { 7, 1,2,3,4,5,7,8, 0,0,0,0 },        /* 15: non-good      */
		 { 7, 1,2,3,4,5,7,8, 0,0,0,0 },        /* 16: non-good      */
		};
		memcpy(&g_a5_byte(-30450), aln, sizeof aln);
	}

	/* Race/class legality matrix (g_a5_-30864): 6 races x 14 bytes, each
	 * `count, legal internal class indices..., zero pad`, indexed rec[88]*14
	 * (races: 0 Elf, 1 Half-Elf, 2 Dwarf, 3 Gnome, 4 Halfling, 5 Human).
	 * The chargen class pick validates against it (readers at 27626/30654);
	 * zeroed, no class default can validate and the class radio never
	 * lights. The same AD&D 2e RULES MATRIX category as the alignment
	 * table above: demihuman class restrictions (dwarf/gnome/halfling =
	 * fighter/thief/fighter-thief; elf and half-elf their multiclass
	 * lists; human the six single classes). Verified byte-exact. */
	{
		static const unsigned char rc[6][14] = {
		 {  7, 2,5,6,13,14,15,16, 0,0,0,0,0,0 },          /* Elf      */
		 { 13, 0,2,5,6,4,8,10,9,11,13,14,15,16 },         /* Half-Elf */
		 {  3, 2,6,14, 0,0,0,0,0,0,0,0,0,0 },             /* Dwarf    */
		 {  3, 2,6,14, 0,0,0,0,0,0,0,0,0,0 },             /* Gnome    */
		 {  3, 2,6,14, 0,0,0,0,0,0,0,0,0,0 },             /* Halfling */
		 {  6, 0,2,5,6,3,4, 0,0,0,0,0,0,0 },              /* Human    */
		};
		memcpy(&g_a5_byte(-30864), rc, sizeof rc);
	}

	/* Nine ascending value thresholds (g_a5_-17516), big-endian words:
	 * 100, 250, 500, 1000, 1500, 2000, 3000, 4000, 5000. Money-shaped
	 * round-number steps; the consumer is not lifted yet, so the semantic
	 * label is open — stated here as the designed ladder it plainly is.
	 * (Subsumes the former single byte at -17515: 0x64 is the low byte of
	 * the first word.) */
	{
		static const short thr[9] = { 100, 250, 500, 1000, 1500,
		                              2000, 3000, 4000, 5000 };
		for (i = 0; i < 9; i++)
			g_a5_word(-17516 + i * 2) = thr[i];
	}

	/* 15-entry index permutation (g_a5_-3072): 1 13 10 9 4 5 6 7 8 3 2 11
	 * 12 15 14 — a bijection over 1..15 with 0 fixed, i.e. a 4-bit index
	 * remap. It lives in the QuickDraw-shim neighbourhood right beside the
	 * -3040 dither ramp and the -3015 glyph block, which makes a colour-
	 * index remap the likely reading; the consumer is not lifted yet. */
	{
		static const unsigned char perm[15] = { 1, 13, 10, 9, 4, 5, 6, 7,
		                                        8, 3, 2, 11, 12, 15, 14 };
		memcpy(&g_a5_byte(-3072), perm, sizeof perm);
	}

	/* The Mac port's own diagnostic strings — error/trace text of the
	 * ported application itself, restated as this port's text. "Moebius"
	 * is a bare name sitting between the two messages (an internal
	 * library/module tag; its consumer is not lifted). -72 is a Pascal
	 * string (length byte 57); the other two are C strings whose NUL
	 * terminators are already zero. */
	g_a5_byte(-72) = 57;
	memcpy(&g_a5_byte(-71),
	       "Color art requires 256 colors -- using Black & White art.", 57);
	memcpy(&g_a5_byte(-2610), "Visible screen %d", 17);
	memcpy(&g_a5_byte(-122), "Moebius", 7);
}
