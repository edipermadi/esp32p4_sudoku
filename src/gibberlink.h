#pragma once
/*
 * Gibberlink: remote control by sound. The board's microphones (ES7210) listen
 * for ggwave transmissions, the data-over-sound protocol AI agents switch to in
 * Gibberlink (audible Normal, Fast and Fastest; up to 140 bytes). Each message
 * is one devctl command (devctl.h: tap, long, swipe, press, move, release, wait,
 * fill, state, ping, heap, screenoff), optionally after a Gibberlink sender id
 * ("AB$tap 360 400").
 * The command runs as if it came over serial, and the answer ("OK tap",
 * "ERR ...") goes back as sound, ggwave audible Fast, prefixed "P4$".
 *
 * A message may end in "*" and 4 hex digits, the CRC-16/CCITT-FALSE (poly 0x1021,
 * init 0xFFFF) of the text before it (after any sender id): "fill 1:9 4:2*7C3E".
 * ggwave's error correction now and then lets a garbled message through as
 * another one; with the check, a garbled one isn't run ("ERR checksum"), and
 * the answer carries the same kind of check over its text after "P4$".
 *
 * "pz" messages are parts of a puzzle sent by sound (another board's Share >
 * Send by sound): no answer is played for them, so the next part isn't missed.
 *
 * Off by default (Settings > Sound > Gibberlink): while on, a worker on core 0
 * reads the microphones and decodes, and a small icon shows at the bottom right
 * (it lights up while a command runs). What the board plays itself (its reply,
 * sound effects) is not decoded.
 */
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Turns listening on or off (starts the worker on first use). */
void gibberlink_set_enabled(bool on);
bool gibberlink_enabled(void);

/* Call from loop() (the LVGL task): runs a received command, if any. */
void gibberlink_poll(void);

/* Sends text (up to 140 bytes, a Gibberlink id first: "SU$pz ...") as sound,
 * ggwave audible Fast, after what's queued already, with a short gap between
 * messages; works whether listening is on or not (the worker starts for it and
 * ends after). false: no room or no memory. gibberlink_sending(): how many are
 * still to play (0: done). */
bool gibberlink_send(const char *text);
int gibberlink_sending(void);

#ifdef __cplusplus
}
#endif
