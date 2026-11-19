/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef RP_CONFORMANCE_H
#define RP_CONFORMANCE_H

#include "types.h"

/* The edge cases platform.h promises, checked through platform.h alone
   so the same file runs against any backend: on the host by `make
   check`, and on a target from a smoke program that links it.
   `dir` is a directory the test may create and delete files in,
   written so that dir followed by a file name is a path ("" for the
   current directory, "/tmp/", "A:\\", "RAM:"). Each check that fails
   is passed to `report` (its name); the count of failures is returned. */
typedef void (*rp_conformance_report)(const char *what);

u32 rp_conformance_files(const char *dir, rp_conformance_report report);
u32 rp_conformance_clipboard(rp_conformance_report report);

#endif
