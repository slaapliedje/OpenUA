/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef RP_GEMCOMPAT_H
#define RP_GEMCOMPAT_H

/* gemcompat.h -- the places two GEMs are not the same shape.
 *
 * This backend builds against two AES bindings: the ST's (mintlib's gem.h
 * over a 68000) and gem4xe's (~/dev/gem4xe, GEM on an Atari 8-bit with a
 * 65816 in it). The AES contract is the same one in both -- the opcodes,
 * the object tree, the resource format -- and almost every line here is
 * written once and compiles for both. What differs is not behaviour but C
 * declaration, in two structures, and both differences are correct for the
 * machine that made them. So the facts live here rather than as #ifdefs
 * scattered through the backend.
 *
 * Detected by gem4xe's own include guard rather than by a build flag,
 * because the thing that actually differs is which gem.h is on the
 * include path.
 *
 * #include <gem.h> before this header. */

#include <gem.h>

#ifdef GEM4XE_APP_GEM_H
#  include <stdint.h>
#else
#  include <osbind.h>
#endif

/* GEMDOS -- Malloc, Fopen and the rest of the ST's osbind names.
 *
 * On the ST they come from mintlib's <osbind.h>, which is a header of
 * inline trap #1 stubs and nothing else.  gem4xe declares the same names
 * in its own <gem.h> (they are one of its three COP faces), so including
 * osbind.h there would redeclare them -- and there is no such header on
 * that toolchain to include in any case.  One include, decided by which
 * GEM is on the path, and the callers below say nothing about it. */

/* MFDB_SET_ADDR -- assign a buffer to an MFDB's fd_addr.
 *
 * The ST's bindings declare the field `void *`, which on a 68000 is the 32
 * bits the VDI's layout wants. gem4xe runs on a 65816 whose small data
 * model makes `void *` SIXTEEN bits, and an MFDB whose first field shrank
 * would shift every field after it and break every application that builds
 * one by hand -- so it declares fd_addr as a uint32_t and says so. */
#ifdef GEM4XE_APP_GEM_H
#  define MFDB_SET_ADDR(m, p)  ((m).fd_addr = (uint32_t)(p))
#else
#  define MFDB_SET_ADDR(m, p)  ((m).fd_addr = (void *)(p))
#endif

/* OBJECT.ob_spec -- one union now, in both GEMs.
 *
 * This was the largest entry in this header.  mintlib declares ob_spec as
 * a UNION, so ST source says which half it means by naming a member:
 * `.free_string` for the char * a G_STRING, G_BUTTON or G_TITLE carries,
 * `.index` for the colour-and-border word a G_BOX, G_IBOX or G_BOXCHAR
 * carries (Compendium p.6.18).  gem4xe declared a bare LONG -- which is
 * what the ST's own aes.h has -- so every site needed two spellings and
 * the gem4xe one had to widen the LONG back to the far pointer it was
 * made from, by hand, in both directions.
 *
 * Since gem4xe 0b3b5b3 its gem.h declares the same union with the same
 * member names, and measured its layout in both data models rather than
 * assuming it: an OBJECT is 24 bytes with ob_spec at offset 12 either
 * way, a small-model pointer landing on the low word where the near
 * address is, a large-model one filling the long with the bank in byte 2.
 *
 * So these five are one definition each now, and this header no longer
 * has an opinion about ob_spec -- it only gives the operations names, so
 * the shell reads as intent rather than as union members.  The macros are
 * kept rather than deleted because 79 call sites spell them, and because
 * naming the operation is worth something even when both branches agree.
 *
 * The far/near question does not arise in the assignment: under
 * --data-model=large a `char *` IS far, and a `__near` string converts to
 * it by taking bank $00, which is where the AES reads these anyway.
 * Checked with the compiler, not by reasoning about it. */
#define OB_SPEC_SET_STRING(o, p)  ((o).ob_spec.free_string = (p))
#define OB_SPEC_SET_INDEX(o, v)   ((o).ob_spec.index = (v))
#define OB_SPEC_INDEX(o)          ((long)(o).ob_spec.index)
#define OB_SPEC_STRING(o)         ((const char *)(o).ob_spec.free_string)
#define OB_SPEC_IS_STRING(o, p)   ((o).ob_spec.free_string == (p))

/* FA_ERROR / FA_INFO -- the icon prefix of a form_alert string.
 *
 * These are gemlib's spelling, not the AES's: the alert string itself
 * carries the icon as "[n]" and both GEMs parse the same grammar, so all
 * that is needed here is the constant mintlib's mt_gem.h supplies
 * (FA_ERROR "[1]", FA_INFO "[4]").
 *
 * gem4xe draws three icons -- note, question, stop -- which is what
 * classic GEM has, and its al_icon() (src/aes/alert.c) sends anything
 * above 2 to stop.  "[4]" is a MagiC/XaAES extension, so FA_INFO maps to
 * the note icon here rather than arriving as a stop sign: an
 * informational alert with a stop sign on it says the wrong thing.
 *
 * Worth knowing on the ST as well: "[4]" is outside the 0-3 that plain
 * TOS documents, so what a Falcon under plain TOS draws for FA_INFO is
 * its own business and has not been checked on hardware. */
#ifdef GEM4XE_APP_GEM_H
#  define FA_ERROR "[1]"
#  define FA_INFO  "[1]"
#endif

/* WP_AES_NEAR -- data the AES is going to be handed must live in bank $00.
 *
 * gem4xe's ABI passes an address to the AES as sixteen bits and supplies
 * the bank itself: src/sys/abi.c's near_of() takes the low word and the
 * AES reads it in bank $00.  That is not a limitation it apologises for
 * -- the AES, its object trees and its strings have always lived in one
 * bank, and making the ABI carry a bank byte would cost every call.
 *
 * It collides with --data-model=large, which is the model this port needs
 * for everything else: there, a global goes to far memory unless it says
 * otherwise, so an object tree declared the ordinary way lands in bank
 * $05 and the AES reads whatever is at its offset in bank $00.  Nothing
 * traps; the menu simply does not draw.  gemapp.scm says so in as many
 * words -- "a program that hands the AES a tree or a string must declare
 * those __near".
 *
 * So: anything whose ADDRESS crosses the seam is tagged with this, and
 * anything that merely holds the word processor's own data is not.  On
 * the ST it is nothing at all -- a 68000 has one address space and the
 * question does not arise. */
#ifdef GEM4XE_APP_GEM_H
#  define WP_AES_NEAR NEAR             /* portab.h: __near on the 65C816, nothing on the 6502 */
#else
#  define WP_AES_NEAR
#endif

/* OB_SPEC_ZERO -- the ob_spec slot of a static OBJECT initializer.
 *
 * `{0}` in both GEMs now, ob_spec being a union in both: a union's
 * initializer is brace-enclosed.  This used to be `0` on gem4xe, where
 * ob_spec was a bare LONG and Calypsi refused the braces C89 6.5.7 allows
 * around a scalar ("internal error: unhandled initializer").  That limit
 * is unchanged; it simply no longer applies, and the braced form was
 * compiled against the new header to be sure. */
#define OB_SPEC_ZERO {0}

/* The AES's small change: flag values that gemlib names and gem4xe, which
 * declares only the calls, leaves as the numbers the AES documents.  All
 * six are the ST's own (mt_gem.h) and none of them is in dispute. */
#ifdef GEM4XE_APP_GEM_H
#  define MENU_REMOVE   0
#  define MENU_INSTALL  1
#  define UNCHECK       0
#  define CHECK         1
#  define HIGHLIGHT     0
#  define UNHIGHLIGHT   1

/* MFORM -- gemlib declares graf_mouse's form as a struct of the mouse
 * form's fields; gem4xe declares it as the bare WORD array the AES
 * actually reads.  Everything in this tree passes a null form (the shape
 * is chosen by `mode`, and only USER_DEF reads the pointer), so the alias
 * carries every use there is.  Code that wants to BUILD a form should use
 * gem4xe's own shape rather than this name. */
typedef WORD MFORM;
#endif

/* wind_create_grect and wind_open_grect were defined here as macros while
 * gem4xe's gem.h carried the rest of that family and not those two.  It
 * declares them now, beside their five siblings, which is the better home
 * -- so the macros are gone rather than shadowing real functions. */

/* WP_EVNT_MULTI -- one wait, two spellings.
 *
 * The AES call is the same underneath, and since gem4xe's kit grew
 * evnt_multi in the ST's own shape -- the Compendium's twenty-three
 * arguments, both mouse rectangles flat -- only one thing still differs:
 * gemlib joins the timer into a LONG, and the AES has always taken it as
 * two words.  So the seam here is this macro and nothing else.  The
 * adapter that used to build two MOBLKs is gone, and gemcompat_gem4xe.c
 * with it.
 *
 * `tm` is expanded twice, which a macro should be shy about.  It is left
 * so because the only caller in this tree passes a constant, and because
 * the alternative is the function just deleted; a caller wanting a
 * computed timeout should put it in a variable first.
 *
 * The cast to unsigned long before shifting is the point of that line: a
 * LONG is signed and right-shifting a negative one is
 * implementation-defined (C89 6.3.7).  Nothing here passes a negative
 * timeout, which is exactly why it must not be left to chance.
 *
 * Fixed arity rather than a variadic macro: this tree is C89, which has
 * no __VA_ARGS__. */
#ifdef GEM4XE_APP_GEM_H
#  define WP_EVNT_MULTI(fl,bc,bm,bs, a1,a2,a3,a4,a5, b1,b2,b3,b4,b5, \
                        msg,tm, mx,my,mb,ks,kr,br)                    \
       evnt_multi((fl),(bc),(bm),(bs),                                \
                  (a1),(a2),(a3),(a4),(a5),                           \
                  (b1),(b2),(b3),(b4),(b5), (msg),                    \
                  (WORD)((unsigned long)(tm) & 0xFFFFuL),             \
                  (WORD)(((unsigned long)(tm) >> 16) & 0xFFFFuL),     \
                  (mx),(my),(mb),(ks),(kr),(br))
#else
#  define WP_EVNT_MULTI(fl,bc,bm,bs, a1,a2,a3,a4,a5, b1,b2,b3,b4,b5, \
                        msg,tm, mx,my,mb,ks,kr,br)                    \
       evnt_multi((fl),(bc),(bm),(bs),                                \
                  (a1),(a2),(a3),(a4),(a5),                           \
                  (b1),(b2),(b3),(b4),(b5), (msg),(tm),               \
                  (mx),(my),(mb),(ks),(kr),(br))
#endif

#endif /* RP_GEMCOMPAT_H */
