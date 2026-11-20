#pragma once
/*
 * Gibberlink: remote control by sound. The board's microphones (ES7210) listen
 * for ggwave transmissions, the data-over-sound protocol AI agents switch to in
 * Gibberlink (audible Normal, Fast and Fastest; up to 140 bytes). Each message
 * is one devctl command (devctl.h: tap, long, swipe, press, move, release, wait,
 * ping, heap, screenoff), optionally after a Gibberlink sender id ("AB$tap 360 400").
 * The command runs as if it came over serial, and the answer ("OK tap",
 * "ERR ...") goes back as sound, ggwave audible Fast, prefixed "P4$".
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

#ifdef __cplusplus
}
#endif
