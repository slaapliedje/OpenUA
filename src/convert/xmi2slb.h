/* xmi2slb — DOS XMIDI soundtrack -> Mac-format MUSIC.SLB bank.
 *
 * C port of tools/xmi2slb.py (the byte-exact-tested REFERENCE — change the
 * Python first, then mirror here; tests/test_xmi2slb_c.py asserts the two
 * produce identical bytes over the module corpus and synthetic files).
 *
 * Why it exists on-device: a fan module ships its music as .XMI inside the
 * design, and the engine resolves MUSIC.SLB design-first (ADR-0011 applied to
 * music). Until this port, the conversion was host-side Python only, so a
 * module installed by uainst on the Amiga or Atari played the BASE game's
 * soundtrack — working audio, wrong music.
 *
 * Dependency-free C99 integer code, 68000-safe (no unaligned loads, no
 * negative shifts), no allocation — the caller supplies a scratch arena, as
 * for artconv. The one 64-bit operation (the tempo -> period division) is
 * done in unsigned long long; it runs once per song.
 */
#ifndef XMI2SLB_H
#define XMI2SLB_H

enum {
	XMI2SLB_ERR_BAD = -1,		/* no EVNT chunk / truncated event stream */
	XMI2SLB_ERR_SPACE = -3		/* dst or scratch too small */
};

/* Driver arrangements, in PREFERENCE order (lower wins). Tandy is already
 * three monophonic voices — the shape the Mac 4-tone sequencer wants. */
enum {
	XMI2SLB_DRV_TY = 0,		/* TYDQ?.XMI  Tandy 3-voice */
	XMI2SLB_DRV_PC = 1,		/* PCDQ?.XMI  PC speaker */
	XMI2SLB_DRV_RO = 2,		/* RODQ?.XMI  Roland MT-32 */
	XMI2SLB_DRV_AD = 3,		/* ADDQ?.XMI  AdLib */
	XMI2SLB_DRV_DQK = 4,		/* dqkQ?.xmi / Dqk?.xmi (fan modules) */
	XMI2SLB_NDRV = 5
};

/* Classify one XMI BASENAME (case-insensitive). Returns 1 and fills *drv
 * (XMI2SLB_DRV_*) and *q (the song number, 1..9) when the name is one of
 * the three shapes the corpus uses, 0 otherwise:
 *   ??DQ<n>.XMI   retail naming, 9 chars      e.g. ADDQ1.XMI
 *   DQKQ<n>.XMI   9 chars                     e.g. dqkQ1.xmi
 *   DQK<n>.XMI    8 chars, no Q               e.g. Dqk1.xmi   (curse: 8 songs) */
int xmi2slb_classify(const char *basename, int *drv, int *q);

/* Convert one XMI file to a complete .slb SONG record into dst.
 * Returns the record length or a negative XMI2SLB_ERR_*. */
long xmi2slb_song(const unsigned char *xmi, long len,
		  unsigned char *dst, long dstcap,
		  unsigned char *scratch, long scratchcap);

/* Build the whole 8-song bank. xmi[0..2] / len[0..2] are songs Q1..Q3; a
 * NULL entry is a missing song and becomes an EMPTY song (a lone 0xFF
 * terminator), exactly as the Mac-only slots 2..6 already are, so jt985's
 * range check passes and a design asking for it plays silence.
 * Returns the bank length or a negative XMI2SLB_ERR_*.
 * dstcap: 64 KB covers every module in the corpus (largest bank 16 KB);
 * scratch: 256 KB is comfortable. */
long xmi2slb_bank(const unsigned char *const xmi[3], const long len[3],
		  unsigned char *dst, long dstcap,
		  unsigned char *scratch, long scratchcap);

#endif /* XMI2SLB_H */
