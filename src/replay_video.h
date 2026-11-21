/**
 * @file replay_video.h
 * @ingroup replay_video
 * @brief A replay exported as an MP4 on the card: the file, the frames, the encoder.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stats_db.h"
#include "bsp/bsp_board.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup replay_video Replay video
 * @ingroup gameplays_core
 * @brief The bookkeeping of a replay exported as an MP4, without LVGL.
 *
 * The Replay screen is played into a video: the UI builds it on a second, invisible LVGL display
 * that renders into this file's frame buffer (replay_video_fb(), never shown) and steps it through
 * the game frame by frame (replay_video_k_at()); every frame goes through the hardware H.264
 * encoder into an MP4 on the card (video_enc.c, mp4_writer.c). The video holds the start for
 * RX_INTRO_MS, plays the moves at RX_SPEED x the replay timeline (pauses already shortened to
 * RP_MAX_GAP_MS), then holds the solved board for RX_OUTRO_MS.
 * @{
 */

#define RX_FPS        15                  /**< the video's frames per second */
#define RX_SPEED      2                   /**< x the replay timeline */
#define RX_INTRO_MS   1000                /**< the start, held */
#define RX_OUTRO_MS   3000                /**< the solved board, held */
#define RX_DIR        BSP_SD_MOUNT_POINT "/replays"   /**< where videos go */

/** @name Asking for one
 *  @{ */
/**
 * @brief The replay asked for from sudoku.db is to be exported, not shown (or shown).
 * @param v  true: exported
 */
void replay_set_want_export(bool v);
/**
 * @brief Whether the replay asked for is to be exported.
 * @return true: exported
 */
bool is_replay_want_export(void);
/**
 * @brief Whether an export is running.
 * @return true: running
 */
bool is_replay_busy(void);
/**
 * @brief Whether an MP4 export is on its way (asked for) or running.
 * @return true: on its way or running
 */
bool is_replay_exporting(void);
/** @} */

/** @name Making one
 *  @{ */
/**
 * @brief A video's file name from the player, puzzle and when it was solved: letters, digits, - and
 *        _ only ("Ana_Classic-Easy-12_20261121-1743").
 * @param r       the solve
 * @param puzzle  the puzzle's name
 * @param out     gets the name (without .mp4)
 * @param n       its size
 */
void replay_video_name(const stats_replay_t *r, const char *puzzle, char *out, size_t n);
/**
 * @brief Starts an export of the timeline prepared (replay_prepare()): replays/\<file\>.mp4 opened,
 *        the frame buffer made, the frames counted.
 * @param file  the file name (replay_video_name())
 * @param w     the frame's width
 * @param h     its height
 * @return false if the frame buffer or the encoder (or the file) couldn't be opened
 */
bool replay_video_open(const char *file, int w, int h);
/**
 * @brief The frame buffer the invisible display renders into (RGB888: see video_enc.c, RGB565
 *        gave the video a green cast).
 * @return it
 */
uint8_t *replay_video_fb(void);
/**
 * @brief Which step a video frame shows.
 * @param f        the frame
 * @param playing  gets whether it "plays" there
 * @return the step: moves shown
 */
int replay_video_k_at(int f, bool *playing);
/**
 * @brief Encodes the next frame from the frame buffer.
 * @param changed  the picture changed since the last frame
 * @return false if the encoder failed
 */
bool replay_video_frame(bool changed);
/**
 * @brief The frames encoded.
 * @return how many
 */
int replay_video_done(void);
/**
 * @brief The frames the video has.
 * @return how many
 */
int replay_video_frames(void);
/**
 * @brief The video's size so far.
 * @return bytes
 */
uint32_t replay_video_bytes(void);
/** @brief Cancel was tapped: the next step ends it. */
void replay_video_cancel(void);
/**
 * @brief Whether Cancel was tapped.
 * @return true: cancelled
 */
bool is_replay_video_cancelled(void);
/**
 * @brief Ends the encoding: the file closed (kept) or aborted (deleted).
 * @param ok  keep it
 * @return false if it was to be kept but couldn't be closed
 */
bool replay_video_end(bool ok);
/**
 * @brief Frees the frame buffer (after the display that renders into it is gone) and ends the
 *        export.
 */
void replay_video_free(void);
/**
 * @brief What to tell the user afterwards: where it was saved and its size, or why not.
 * @param ok     it was saved
 * @param bytes  its size
 * @param msg    gets the message
 * @param n      its size
 */
void replay_video_result(bool ok, uint32_t bytes, char *msg, size_t n);
/** @} */

/** @} */

#ifdef __cplusplus
}
#endif
