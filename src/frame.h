/*
 * s16-pi - the emulated frame: its picture and sound, and where its lines go
 *
 * Each line the core finishes goes straight to the screen (with the
 * scanlines), from core 1 or from core 0 when it draws the lines itself;
 * with COMPOSITE it goes to core 2, which filters it and puts it on screen.
 */
#ifndef FRAME_H
#define FRAME_H

#include "screen.h"
#include <stdint.h>

extern u32 g_VideoBuf[GAME_SRC_PITCH * GAME_SRC_LINES];

// after each frame: the core's sound for it (FrameTakeAudio)
extern int16_t g_AudioBuf[8192];
extern unsigned g_nAudioFrames;		// stereo frames

void FrameSetup (CScreen *pVideo);	// once, before the first frame
void FrameTakeAudio (void);
void FrameSync (void);			// wait until cores 1 and 2 have finished the frame

// FALSE: the lines stay in g_VideoBuf (benchmark, MC test); FrameDraw shows them
void FrameLinesToScreen (boolean bOn);
boolean FrameTvInit (void);		// COMPOSITE's tables (about 4 MB, built once)
void FrameSetComposite (boolean bOn);
boolean FrameIsComposite (void);
void FrameSetRaced (boolean bOn);	// this frame is beam raced

void FrameDraw (void);			// g_VideoBuf to the screen, if not there yet
void FrameDrawPicture (u32 *pBuffer);	// a UI picture, through the same filter

unsigned FrameMcFailFrame (void);	// frame at which core 1 stopped answering

#endif
