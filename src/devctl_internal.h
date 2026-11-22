/**
 * @file devctl_internal.h
 * @ingroup devctl
 * @brief The devctl files: what they share. Only they include it.
 *
 * | File               | Commands |
 * |--------------------|----------|
 * | devctl.c           | reading command lines, the answer (devctl_reply()), the dispatch; ping, baud, wait, screen, pz, heap |
 * | devctl_touch.c     | tap, long, swipe, press, move, release |
 * | devctl_screen.c    | shot, bench, screenoff |
 * | devctl_files.c     | ls, get, put, mv, rm, mkdir, import |
 * | devctl_game.c      | state, fill |
 * | devctl_clock.c     | clock |
 * | devctl_genbench.c  | genbench |
 * | devctl_sql.c       | sql |
 * | devctl_colls.c     | colls, colldel |
 * | devctl_stream.c    | the data to and from the host: numbered base64 lines with a CRC, compression, base64 in |
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "devctl.h"
#include "serial_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup devctl
 *  @{ */

/** A command line (fill: up to 81 pairs), or an upload's data line. */
#define DEVCTL_LINE_MAX 512
#define CMD_MAX         4096                        /**< a line from the serial port: sql's base64 is the long one */
#define CHUNK           96                          /**< bytes per data line (128 base64 characters) */
#define SETTLE_MS       150                         /**< after a release, or digits entered: let the UI react */

/** @name The command line and its answer (devctl.c)
 *  @{ */
/**
 * @brief The answer to a command: to the port, or (devctl_run_line()) into its buffer.
 * @param fmt  printf's format, then its arguments
 */
void devctl_reply(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/**
 * @brief Lets LVGL run (timers, input, drawing) for a while.
 * @param ms  how long, in ms
 */
void devctl_pump(uint32_t ms);
/**
 * @brief The command came as sound (devctl_run_line()): a short, one-line answer, no serial-only commands.
 * @return true while running such a command
 */
bool devctl_is_sound(void);
/**
 * @brief The buffer lines from the port are read into (CMD_MAX bytes); put reads its data lines into it.
 * @return the buffer
 */
char *devctl_line_buf(void);
/** @} */

/** @name Data to and from the host (devctl_stream.c)
 *  @{ */
/**
 * @brief CRC-32, as zlib.crc32.
 * @param crc  the CRC so far (0 to start)
 * @param p  the bytes
 * @param n  how many
 * @return the CRC with them
 */
uint32_t devctl_crc32(uint32_t crc, const uint8_t *p, int n);
/**
 * @brief Starts the data a command sends: its lines' letter, and whether it's compressed.
 * @param prefix  the lines' letter ('S' a screenshot, 'F' a file, 'Q' SQL's result)
 * @param z  compressed (zlib format) first
 * @return false if there's no memory for the compressor
 */
bool devctl_z_begin(char prefix, bool z);
/**
 * @brief Data to send (counted for the size and CRC as it is, then compressed if asked).
 * @param p  the bytes
 * @param n  how many
 */
void devctl_z_write(const void *p, size_t n);
/**
 * @brief Ends the data: the compressor's last bytes, the last line.
 */
void devctl_z_end(void);
/**
 * @brief The size of the data sent last (its own, before compression), for its END line.
 * @return bytes
 */
uint32_t devctl_z_bytes(void);
/**
 * @brief The CRC-32 of the data sent last (its own, before compression), for its END line.
 * @return the CRC
 */
uint32_t devctl_z_crc(void);
/**
 * @brief base64 -> bytes.
 * @param s  the base64 text
 * @param[out] out  the bytes
 * @param max  out's size
 * @return how many bytes, or -1 if it isn't base64 (or too long)
 */
int devctl_b64_decode(const char *s, uint8_t *out, int max);
/**
 * @brief One line from the host, waiting a while for it.
 * @param[out] buf  the line, without its end
 * @param max  buf's size
 * @param ms  how long to wait
 * @return false on a timeout
 */
bool devctl_read_line(char *buf, int max, uint32_t ms);
/** @} */

/** @name The commands
 *  @{ */
/**
 * @brief tap, long, swipe, press, move, release (devctl_touch.c).
 * @param cmd  the command's name
 * @param a    its numbers
 * @param n    how many there are
 * @return false if it isn't one of them (or lacks its numbers)
 */
bool devctl_touch_cmd(const char *cmd, const int *a, int n);
/**
 * @brief shot [z]: the screen, run-length coded (then, with z, compressed).
 * @param z  compressed
 */
void devctl_shot(bool z);
/**
 * @brief bench [N]: N full-screen redraws of the screen shown (default 30), then their times (devctl_screen.c).
 * @param n  how many numbers came with it
 * @param a  the numbers (a[0]: N)
 */
void devctl_bench(int n, const int *a);
/** @brief screenoff: the screen off as its timeout does (devctl_screen.c). */
void devctl_screenoff(void);
/**
 * @brief ls / get / put / mv / rm / mkdir / import (serial only).
 * @param line  the command line
 * @return true if it was one of them (and has been answered)
 */
bool devctl_files_cmd(char *line);
/**
 * @brief state: the game on screen, as text.
 *
 * (fill and state are functions of their own, not inline in run(): their buffers would sit on the stack
 * under every command, a tap among them, and a tap that opens a hard puzzle runs the solver's deep
 * recursion on top.)
 */
void devctl_state(void);
/**
 * @brief fill \<cell\> \<digit\> ...: digits into the game on screen (any separators, e.g. (12,5),(13,7)).
 * @param line  the command line
 */
void devctl_fill(const char *line);
/**
 * @brief genbench \<variant|all\> [level|all] [n]: n puzzles (default 5) of each, the same seeds every
 *        time (so builds compare), with the CPU at full speed even while the screen is dark.
 * @param line  the command line
 */
void devctl_genbench(const char *line);
/**
 * @brief sql [z \<size\>] \<base64\>: SQL on sudoku.db (stats_db_sql()).
 *
 * The SQL is UTF-8; with z, compressed (zlib format, size bytes once inflated). The result: lines
 * "SQL C\t\<column\>..." and "SQL R\t\<value\>..."; with z, those lines compressed instead, as "SQL BEGIN
 * z", lines "Q \<n\> \<base64\>", "SQL END \<bytes\> \<crc32\>". Then "OK sql \<rows\> \<changes\>[ truncated]" or
 * "ERR sql: \<why\>". The SQL comes coded: it may hold ';', newlines and any text.
 * @param line  the command line
 */
void devctl_sql(const char *line);
/**
 * @brief clock: whether Wi-Fi is connected and the clock is set (by NTP), as "OK clock wifi=\<state\>
 *        time=set YYYY-MM-DD HH:MM:SS" (local time) or "OK clock wifi=\<state\> time=unset".
 *
 * state: off, starting, idle, connecting, connected, failed or no-radio. The network's name, the IP
 * and the time zone are left out (they say where the board is).
 */
void devctl_clock(void);
/**
 * @brief colls: the saved and imported collections (Puzzles > Saved), a "COLL \<imported|saved\> \<puzzles\>
 *        \<name\>" line each (the name with %XX for spaces), then "OK colls \<n\>".
 */
void devctl_colls(void);
/**
 * @brief colldel \<saved|imported\> \<name\>: deletes that collection as the Puzzles screen's Delete does
 *        (sudoku_ui_coll_delete(): its puzzles stay on the board).
 * @param line  the command line
 */
void devctl_colldel(const char *line);
/** @} */

/** @} */

#ifdef __cplusplus
}
#endif
