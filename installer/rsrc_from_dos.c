/*
 * rsrc_from_dos.c — build frua.rsc from the user's own DOS CKIT.EXE, natively.
 *
 * The C twin of tools/rsrc_from_dos.py (ADR-0017). That tool runs on a PC; this
 * one runs on the machine you are installing onto, so a set of DOS floppies and
 * an ST or an Amiga are enough on their own — no PC in the loop. An ST reads
 * DOS 720K/1.44M floppies natively and CrossDOS is standard on Workbench 2.1+,
 * so "dust off the old disks and install straight onto the retro machine" is a
 * real path, not a hypothetical one.
 *
 * WHAT IT PRODUCES. A minimal FRSC archive holding exactly one resource, STRS 0
 * — the string pool the reconstructed A5 world's 1016 pointer relocations point
 * into. It deliberately contains no DATA/ZERO/DREL; their absence is what routes
 * the engine onto the reconstructed-A5 path at boot. 2108 of the 2145 entries
 * are read out of the user's executable at committed positions; the other 37 are
 * Mac-platform plumbing with no DOS counterpart and are port-authored (they live
 * in tools/rsrc_from_dos.py and reach this file through the generated header, so
 * the two implementations cannot drift).
 *
 * ★ IT REFUSES A DIFFERENT EXECUTABLE RATHER THAN EMITTING GARBAGE. The map is
 * derived against ONE exact build (the GOG/Steam v1.2 CKIT.EXE — both ship the
 * identical file). Any other build shifts every offset, and the output would be
 * a plausible-looking archive full of wrong strings, which the engine would only
 * reveal as bizarre text much later. Size and SHA-256 are both checked first.
 *
 * ★ MEMORY: it never loads the executable. SHA-256 streams it in 4 KB chunks and
 * the 2108 extractions are seek+read, so peak RAM is the 29 KB pool plus buffers
 * — this runs on a 512 KB ST, which matters because the machines most likely to
 * want it are the small ones.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "strs_map_dos12.h"
#include "rsrc_from_dos.h"

/* ---------------------------------------------------------------- SHA-256 */
/* FIPS 180-4. Included rather than pulled in because the installer links no
 * crypto library on either target, and the digest is the only thing standing
 * between a repacked CKIT.EXE and a silently corrupt frua.rsc. */

typedef struct {
	unsigned long  h[8];
	unsigned long  nbits_hi, nbits_lo;
	unsigned char  buf[64];
	unsigned int   buflen;
} sha256_ctx;

static const unsigned long K[64] = {
0x428a2f98UL,0x71374491UL,0xb5c0fbcfUL,0xe9b5dba5UL,0x3956c25bUL,0x59f111f1UL,
0x923f82a4UL,0xab1c5ed5UL,0xd807aa98UL,0x12835b01UL,0x243185beUL,0x550c7dc3UL,
0x72be5d74UL,0x80deb1feUL,0x9bdc06a7UL,0xc19bf174UL,0xe49b69c1UL,0xefbe4786UL,
0x0fc19dc6UL,0x240ca1ccUL,0x2de92c6fUL,0x4a7484aaUL,0x5cb0a9dcUL,0x76f988daUL,
0x983e5152UL,0xa831c66dUL,0xb00327c8UL,0xbf597fc7UL,0xc6e00bf3UL,0xd5a79147UL,
0x06ca6351UL,0x14292967UL,0x27b70a85UL,0x2e1b2138UL,0x4d2c6dfcUL,0x53380d13UL,
0x650a7354UL,0x766a0abbUL,0x81c2c92eUL,0x92722c85UL,0xa2bfe8a1UL,0xa81a664bUL,
0xc24b8b70UL,0xc76c51a3UL,0xd192e819UL,0xd6990624UL,0xf40e3585UL,0x106aa070UL,
0x19a4c116UL,0x1e376c08UL,0x2748774cUL,0x34b0bcb5UL,0x391c0cb3UL,0x4ed8aa4aUL,
0x5b9cca4fUL,0x682e6ff3UL,0x748f82eeUL,0x78a5636fUL,0x84c87814UL,0x8cc70208UL,
0x90befffaUL,0xa4506cebUL,0xbef9a3f7UL,0xc67178f2UL };

#define M32(x)     ((x) & 0xffffffffUL)
#define ROR(x, n)  M32((M32(x) >> (n)) | M32((x) << (32 - (n))))

static void sha256_block(sha256_ctx *c, const unsigned char *p)
{
	unsigned long w[64], a, b, cc, d, e, f, g, h, t1, t2;
	int i;

	for (i = 0; i < 16; i++)
		w[i] = ((unsigned long)p[i * 4] << 24) |
		       ((unsigned long)p[i * 4 + 1] << 16) |
		       ((unsigned long)p[i * 4 + 2] << 8) |
		        (unsigned long)p[i * 4 + 3];
	for (i = 16; i < 64; i++) {
		unsigned long s0 = ROR(w[i-15],7) ^ ROR(w[i-15],18) ^ (M32(w[i-15]) >> 3);
		unsigned long s1 = ROR(w[i-2],17) ^ ROR(w[i-2],19) ^ (M32(w[i-2]) >> 10);
		w[i] = M32(w[i-16] + s0 + w[i-7] + s1);
	}
	a=c->h[0]; b=c->h[1]; cc=c->h[2]; d=c->h[3];
	e=c->h[4]; f=c->h[5]; g=c->h[6];  h=c->h[7];
	for (i = 0; i < 64; i++) {
		unsigned long S1 = ROR(e,6) ^ ROR(e,11) ^ ROR(e,25);
		unsigned long ch = (e & f) ^ ((~e) & g);
		unsigned long S0 = ROR(a,2) ^ ROR(a,13) ^ ROR(a,22);
		unsigned long mj = (a & b) ^ (a & cc) ^ (b & cc);
		t1 = M32(h + S1 + ch + K[i] + w[i]);
		t2 = M32(S0 + mj);
		h=g; g=f; f=e; e=M32(d+t1); d=cc; cc=b; b=a; a=M32(t1+t2);
	}
	c->h[0]=M32(c->h[0]+a); c->h[1]=M32(c->h[1]+b);
	c->h[2]=M32(c->h[2]+cc); c->h[3]=M32(c->h[3]+d);
	c->h[4]=M32(c->h[4]+e); c->h[5]=M32(c->h[5]+f);
	c->h[6]=M32(c->h[6]+g); c->h[7]=M32(c->h[7]+h);
}

static void sha256_init(sha256_ctx *c)
{
	c->h[0]=0x6a09e667UL; c->h[1]=0xbb67ae85UL; c->h[2]=0x3c6ef372UL;
	c->h[3]=0xa54ff53aUL; c->h[4]=0x510e527fUL; c->h[5]=0x9b05688cUL;
	c->h[6]=0x1f83d9abUL; c->h[7]=0x5be0cd19UL;
	c->nbits_hi = c->nbits_lo = 0; c->buflen = 0;
}

static void sha256_update(sha256_ctx *c, const unsigned char *p, unsigned long n)
{
	unsigned long add = M32(n << 3);

	/* 64-bit length kept as two 32-bit halves: a 68000 gnu99 long is 32 bits
	 * and CKIT.EXE is only 574 KB, but the carry costs one line. */
	c->nbits_lo = M32(c->nbits_lo + add);
	if (c->nbits_lo < add)
		c->nbits_hi = M32(c->nbits_hi + 1);
	c->nbits_hi = M32(c->nbits_hi + (n >> 29));

	while (n) {
		unsigned long take = 64 - c->buflen;
		if (take > n) take = n;
		memcpy(c->buf + c->buflen, p, (size_t)take);
		c->buflen += (unsigned int)take;
		p += take; n -= take;
		if (c->buflen == 64) { sha256_block(c, c->buf); c->buflen = 0; }
	}
}

static void sha256_final(sha256_ctx *c, unsigned char out[32])
{
	unsigned long hi = c->nbits_hi, lo = c->nbits_lo;
	unsigned char pad = 0x80;
	unsigned char len[8];
	int i;

	sha256_update(c, &pad, 1);
	pad = 0;
	while (c->buflen != 56)
		sha256_update(c, &pad, 1);
	len[0]=(unsigned char)(hi>>24); len[1]=(unsigned char)(hi>>16);
	len[2]=(unsigned char)(hi>>8);  len[3]=(unsigned char)hi;
	len[4]=(unsigned char)(lo>>24); len[5]=(unsigned char)(lo>>16);
	len[6]=(unsigned char)(lo>>8);  len[7]=(unsigned char)lo;
	/* length goes in raw: sha256_update would re-count it */
	memcpy(c->buf + 56, len, 8);
	sha256_block(c, c->buf);
	for (i = 0; i < 8; i++) {
		out[i*4]   = (unsigned char)(c->h[i] >> 24);
		out[i*4+1] = (unsigned char)(c->h[i] >> 16);
		out[i*4+2] = (unsigned char)(c->h[i] >> 8);
		out[i*4+3] = (unsigned char)c->h[i];
	}
}

int uainst_sha256_selftest(void)
{
	/* FIPS known answer for "abc". A digest that is subtly wrong would make
	 * this refuse every legitimate CKIT.EXE, which looks like a bad dump
	 * rather than a bad build — so prove the primitive before trusting it. */
	static const unsigned char want[32] = {
		0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,
		0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,
		0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad };
	sha256_ctx c;
	unsigned char got[32];

	sha256_init(&c);
	sha256_update(&c, (const unsigned char *)"abc", 3);
	sha256_final(&c, got);
	return memcmp(got, want, 32) == 0;
}

/* ------------------------------------------------------------- the builder */

static void put_be16(unsigned char *p, unsigned int v)
{
	p[0] = (unsigned char)(v >> 8); p[1] = (unsigned char)v;
}

static void put_be32(unsigned char *p, unsigned long v)
{
	p[0]=(unsigned char)(v>>24); p[1]=(unsigned char)(v>>16);
	p[2]=(unsigned char)(v>>8);  p[3]=(unsigned char)v;
}

/*
 * FRSC (ADR-0007), matching tools/rsrcpack.py byte for byte:
 *   header  "FRSC" u16 version=1  u16 count  u32 entry_off=16  u32 reserved=0
 *   entry   4-byte type  i16 id  u16 attrs  u32 data_off  u32 length
 *   body    the resource data
 * One resource here, so entry_off 16 and data_off 32.
 */
#define FRSC_HDR   16
#define FRSC_ENT   16

int uainst_rsrc_from_dos(const char *exe_path, const char *out_path,
                         char *msg, unsigned long msgcap)
{
	FILE *f;
	unsigned char *pool = NULL, *blob = NULL;
	unsigned char digest[32], chunk[4096];
	sha256_ctx c;
	unsigned long total = 0, got;
	long i;
	int rc = UAINST_RSRC_ERR;

	if (!uainst_sha256_selftest()) {
		snprintf(msg, msgcap, "internal SHA-256 self-test failed");
		return UAINST_RSRC_ERR;
	}
	f = fopen(exe_path, "rb");
	if (!f) {
		snprintf(msg, msgcap, "cannot open %s", exe_path);
		return UAINST_RSRC_ERR;
	}

	/* Pass 1: size + digest, streamed. */
	sha256_init(&c);
	while ((got = (unsigned long)fread(chunk, 1, sizeof chunk, f)) > 0) {
		sha256_update(&c, chunk, got);
		total += got;
	}
	sha256_final(&c, digest);

	if (total != (unsigned long)SM_CKIT_SIZE) {
		snprintf(msg, msgcap,
		         "%s is %lu bytes, expected %ld - a different DOS build",
		         exe_path, total, (long)SM_CKIT_SIZE);
		fclose(f);
		return UAINST_RSRC_WRONGBUILD;
	}
	if (memcmp(digest, sm_ckit_sha256, 32) != 0) {
		snprintf(msg, msgcap,
		         "%s checksum mismatch - a different DOS build; refusing "
		         "rather than writing garbage", exe_path);
		fclose(f);
		return UAINST_RSRC_WRONGBUILD;
	}

	/* Pass 2: the pool. Zero-filled first — the map does not cover every
	 * byte, and the gaps are NULs in the original pool. */
	pool = (unsigned char *)calloc(1, (size_t)SM_POOL_SIZE);
	if (!pool) {
		snprintf(msg, msgcap, "out of memory (%ld byte pool)",
		         (long)SM_POOL_SIZE);
		fclose(f);
		return UAINST_RSRC_ERR;
	}
	for (i = 0; i < SM_NENTRIES; i++) {
		unsigned long off = sm_pool_off[i];
		unsigned long len = sm_len[i];

		if (off + len > (unsigned long)SM_POOL_SIZE)
			continue;                       /* generator asserts this */
		if (fseek(f, (long)sm_dos_off[i], SEEK_SET) != 0 ||
		    fread(pool + off, 1, (size_t)len, f) != (size_t)len) {
			snprintf(msg, msgcap, "read failed at entry %ld", i);
			goto done;
		}
	}
	for (i = 0; i < SM_NAUTHORED; i++)
		memcpy(pool + sm_authored[i].off, sm_authored[i].text,
		       sm_authored[i].len);

	/* Pass 3: wrap in FRSC and write. */
	blob = (unsigned char *)malloc((size_t)(FRSC_HDR + FRSC_ENT + SM_POOL_SIZE));
	if (!blob) {
		snprintf(msg, msgcap, "out of memory (archive)");
		goto done;
	}
	memcpy(blob, "FRSC", 4);
	put_be16(blob + 4, 1);                  /* version */
	put_be16(blob + 6, 1);                  /* one resource */
	put_be32(blob + 8, FRSC_HDR);           /* entry table offset */
	put_be32(blob + 12, 0);                 /* reserved */
	memcpy(blob + 16, "STRS", 4);
	put_be16(blob + 20, 0);                 /* id 0 */
	put_be16(blob + 22, 0);                 /* attrs */
	put_be32(blob + 24, FRSC_HDR + FRSC_ENT);
	put_be32(blob + 28, (unsigned long)SM_POOL_SIZE);
	memcpy(blob + 32, pool, (size_t)SM_POOL_SIZE);

	{
		FILE *o = fopen(out_path, "wb");
		size_t n = (size_t)(FRSC_HDR + FRSC_ENT + SM_POOL_SIZE);
		if (!o) {
			snprintf(msg, msgcap, "cannot create %s", out_path);
			goto done;
		}
		if (fwrite(blob, 1, n, o) != n) {
			fclose(o);
			snprintf(msg, msgcap, "write failed (disk full?)");
			goto done;
		}
		fclose(o);
		snprintf(msg, msgcap,
		         "wrote %s: %lu bytes (STRS pool %ld, %d extracted + %d "
		         "authored)", out_path, (unsigned long)n,
		         (long)SM_POOL_SIZE, SM_NENTRIES, SM_NAUTHORED);
		rc = UAINST_RSRC_OK;
	}
done:
	fclose(f);
	free(pool);
	free(blob);
	return rc;
}
