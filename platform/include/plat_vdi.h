/*
 * Platform VDI printer-workstation HAL.
 *
 * The minimal GEM VDI binding the GDOS printing track needs (see
 * docs/gdos-printing-wall.md): open/close a GDOS device workstation
 * (device 21 = the ASSIGN.SYS printer, 31 = the META.SYS metafile),
 * page control, graphic text, and the font/size attributes. Backed by
 * `trap #2` with the classic contrl/intin/ptsin parameter block; needs
 * a resident GDOS (SpeedoGDOS 5.7 is staged by tools/stage_gdos.sh)
 * for any non-screen device.
 *
 * The Printing Manager face in compat/ builds the Mac spellings
 * (PrOpen, PrOpenPage, ...) on top of this surface; engine code never
 * sees VDI (ADR-0003 layer rule).
 */

#ifndef PLATFORM_VDI_H
#define PLATFORM_VDI_H

/*
 * Is a GDOS resident? (the trap #2 d0=-2 probe). Without one, only the
 * ROM screen VDI exists and plat_vdi_open on a printer device fails.
 */
int plat_vdi_gdos_present(void);

/*
 * v_opnwk on `device` (21 = printer, 31 = metafile; the id indexes
 * ASSIGN.SYS). Returns the workstation handle, 0 on failure. When
 * out_w/out_h are non-NULL they receive the device raster size in
 * pixels (work_out[0/1] + 1) for pagination.
 */
short plat_vdi_open(short device, short *out_w, short *out_h);

/* v_clswk — close the workstation (a printer flushes its last page). */
void plat_vdi_close(short handle);

/* v_clrwk — start a fresh page. */
void plat_vdi_clear(short handle);

/* v_updwk — render/emit the current page (printer form feed). */
void plat_vdi_update(short handle);

/* v_gtext — draw `s` with the current font/size at pixel (x, y). */
void plat_vdi_text(short handle, short x, short y, const char *s);

/* vst_font — select a GDOS font id; returns the id actually set. */
short plat_vdi_font(short handle, short id);

/*
 * vst_point — set the text size in printer points. Returns the size
 * actually set; when out_cell_h is non-NULL it receives the resulting
 * cell height in pixels (the line-advance for a text printer).
 */
short plat_vdi_point(short handle, short pt, short *out_cell_h);

/*
 * vst_load_fonts — attach the GDOS fonts (Speedo outlines included) to
 * the workstation. WITHOUT this call only the device's built-in faces
 * exist — FX80.SYS has none, so text on it silently rasterizes to a
 * blank page. Returns the number of fonts GDOS added; the total face
 * count for vqt_name is work_out[10]-from-open plus this.
 */
short plat_vdi_load_fonts(short handle);

/*
 * vqt_name — the id + name of face `index` (1-based). `name32` (when
 * non-NULL) must hold 33 bytes; receives the NUL-terminated face name.
 * Returns the font id to pass to plat_vdi_font.
 */
short plat_vdi_font_name(short handle, short index, char *name32);

/*
 * vm_filename — metafile workstations only: redirect the output from
 * the default GEMFILE.GEM to `path`. Call immediately after
 * plat_vdi_open(31, ...).
 */
void plat_vdi_meta_filename(short handle, const char *path);

#endif /* PLATFORM_VDI_H */
