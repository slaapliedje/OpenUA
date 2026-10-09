/*
 * No MIDI port on this platform (Amiga, the Unix/X11 build): the MIDI music
 * API (plat_midi.h) as no-ops, so the shim and the event loop link the same
 * way everywhere. The four-tone synth keeps the music.
 */
#include "plat_midi.h"

int  plat_midi_init(void)                                  { return -1; }
int  plat_midi_enabled(void)                               { return 0; }
int  plat_midi_song(const unsigned char *xmi, long len)    { (void)xmi; (void)len; return -1; }
void plat_midi_stop(void)                                  { }
void plat_midi_mute(int muted)                             { (void)muted; }
int  plat_midi_playing(void)                               { return 0; }
void plat_midi_vbl(unsigned long hz200)                    { (void)hz200; }
void plat_midi_poll(void)                                  { }
void plat_midi_shutdown(void)                              { }
