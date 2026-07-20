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
 * would change behaviour rather than preserve it. They still come from the
 * DATA replay until that is settled.
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
			{ -17517, 0x0a }, { -17515, 0x64 }, { -14681, 0x7f }, { -14440, 0x01 },
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
}
