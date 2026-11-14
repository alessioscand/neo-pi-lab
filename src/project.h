/*
 * s16-pi - what the shared files in common/ need to know about the project
 */
#ifndef PROJECT_H
#define PROJECT_H

#define PROJECT_NAME	"s16-pi"
#define PROJECT_VERSION	"0.3.2"

/* The emulated picture */
#define PROJECT_WIDTH	320
#define PROJECT_HEIGHT	224

/* TV filter (common/tvcore, common/composite): one line in (320 pixels
   0x00RRGGBB), the filtered line out (COMP_OUT_W) */
#define PROJECT_TV_IN_BYTES	(320 * 4)
#define PROJECT_TV_OUT_W	749

/* Players: one pad each (common/input) */
#define PROJECT_PLAYERS	4

/* Buttons besides the directions, START and SELECT: A B C D (the games use
   up to three) */
#define PROJECT_BUTTONS	4

/* Each emulated frame starts here: the first line after the picture */
#define PROJECT_PHASE_LINE	224

/* Error codes = number of green LED blinks when the screen is unusable */
#define PROJECT_ERR_FB    2   /* framebuffer / HDMI */
#define PROJECT_ERR_SD    3   /* SD card or FAT filesystem */
#define PROJECT_ERR_GAME  5   /* no System 16 or 18 game (MAME .zip) in the s16pi folder */
#define PROJECT_ERR_CORE  6   /* emulator core error */
#define PROJECT_ERR_MEM   7   /* out of memory */

#ifdef __cplusplus
extern "C" {
#endif

void project_log (const char *pFormat, ...);
void project_fatal (int nCode, const char *pFormat, ...);

#ifdef __cplusplus
}
#endif

#endif
