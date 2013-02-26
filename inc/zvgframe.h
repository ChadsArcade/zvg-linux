#ifndef _ZVGFRAME_H_
#define _ZVGFRAME_H_
/*****************************************************************************
* Header file for FRAME buffered ZVG driver routines.
*
* Author:       Zonn Moore
* Created:      05/20/03
*
* History:
*    07/30/03
*       Added "TIMER.H" to file.
*
*    06/27/03
*       Added "ZVGENC.H" and "ZVGPORT.H" to file, making "ZVGFRAME.H" the
*       only file needing to be included for those using ZVGFRAME routines.
*
* (c) Copyright 2003-2004, Zektor, LLC.  All Rights Reserved.
*****************************************************************************/
#ifndef _ZVGENC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include	"zvgenc.h"
#endif

#ifndef _ZVGPORT_H_
#include	"zvgport.h"
#endif

#ifndef _TIMER_H_
#include	"timer.h"
#endif

/* These limits are not checked in the ZVG library. See zvgFrameVector
 * in zvgframe.c. */
#define ZVG_X_MIN -512
#define ZVG_X_MAX  511
#define ZVG_Y_MAX  383 
#define ZVG_Y_MIN -384

#define ZVG_X_OVERSCAN_MIN -600
#define ZVG_X_OVERSCAN_MAX  599
#define ZVG_Y_OVERSCAN_MAX  471 
#define ZVG_Y_OVERSCAN_MIN -472

/* Setup some aliases to make the API consistent. */

#define	zvgFrameSetColor( newcolor) \
			zvgEncSetColor( newcolor)

#define	zvgFrameSetRGB24( red, green, blue) \
			zvgEncSetRGB24( red, green, blue)

#define	zvgFrameSetRGB16( red, green, blue) \
			zvgEncSetRGB16( red, green, blue)

#define	zvgFrameSetRGB15( red, green, blue) \
			zvgEncSetRGB15( red, green, blue)

#define	zvgFrameSetClipWin( xMin, yMin, xMax, yMax) \
			zvgEncSetClipWin( xMin, yMin, xMax, yMax)

#define	zvgFrameSetClipOverscan() \
			zvgEncSetClipOverscan()

#define	zvgFrameSetClipNoOverscan() \
			zvgEncSetClipNoOverscan()

extern ZvgSpeeds_a	ZvgSpeeds;
extern ZvgMon_s		ZvgMon;
extern ZvgID_s			ZvgID;

extern uint zvgFrameOpen( void);
extern void zvgFrameClose( void);

#if defined(WIN32) || defined(linux)
	extern uint zvgFrameVector( int xStart, int yStart, int xEnd, int yEnd);
#else /* DOS */
	extern uint zvgFrameVector( uint xStart, uint yStart, uint xEnd, uint yEnd);
#endif /* OS */

extern uint zvgFrameSend( void);

/* Interface for using a thread.  This capability is new and it in no way
 * modifies previous functionality. (Or, that was the goal!) */
#ifdef ZVG_IO_THREAD_ENABLED

	#if defined(WIN32)
	#elif defined(linux)

		extern uint zvgFrameOpenThreaded( void);
		extern void zvgFrameCloseThreaded( void);
		extern uint zvgFrameSendThreaded( void);
		extern uint zvgGetABufferThreaded( void);

	#else /* DOS */
	#endif /* OS */
#endif /* ZVG_IO_THREAD_ENABLED */

#ifdef __cplusplus
}
#endif

#endif /* _ZVGFRAME_H_ */
