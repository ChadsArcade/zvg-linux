/*****************************************************************************
* FRAME buffered ZVG driver routines.
*
* Author:       Zonn Moore
* Created:      05/20/03
*
* History:
*    07/02/03
*       Moved spotkiller logic to zvgEnc.c. Added calls to 'zvgSOF()' to
*       handle spotkiller.
*
*    07/01/03
*       Moved Color to B&W conversions to 'zvgEnc.c'.
*
*    06/30/03
*       Do Color to B&W conversions. Limit colors instead of masking them.
*
*    06/29/03
*       Yet another adjustment to spotkill.  RipOff was still occasionally
*       triggering the spotkiller.  Set new boundary to require at least
*       a 3/4 screen deflection before spotkill dots are not drawn.
*
*    06/26/03
*       Moved the spotkiller tests to zvgEnc.c since the comparisons must
*       be made after clipping. Blank POINTs are still generated here since
*       the encoder doesn't know how to send data to the next frame.
*
*    06/20/03
*       Added spotkiller killer code.  Looks for screens the would cause
*       the spotkiller to activate, and if found, send some "blank" dots
*       to keep the spotkiller from activating.
*
* (c) Copyright 2003-2004, Zektor, LLC.  All Rights Reserved.
*****************************************************************************/
#ifdef WIN32
	#include <windows.h>

#elif defined(linux)
#else // DOS
#endif

#include	"zstddef.h"
#include	"zvgport.h"
#include	"zvgenc.h"
#include	"timer.h"
#include "zvgframe.h"

// If MAME is defined, then the 'tmrRestore()' call will *NOT* be
// included in the driver code.

//#define	MAME										// if set, indicate this compile is to be used with MAME

// Allocate a small encode buffer used to buffer commands generated by the
// ZVG encoder.

uchar			EncodeBfr[zENC_CMD_SIZE*10];

// Keep track of status information returned from the ZVG

ZvgSpeeds_a	ZvgSpeeds;
ZvgMon_s		ZvgMon;
ZvgID_s		ZvgID;

// Thread and mutex stuff----------------------
#ifdef ZVG_IO_THREAD_ENABLED
	#if defined(WIN32)
	#elif defined(linux)
	
		#include <stdio.h> // for printf - remove later!!!
		#include <unistd.h> // for usleep()
		#include <pthread.h>
		
		enum buffState {ZBUF_EMPTY, ZBUF_FILL, ZBUF_READY, ZBUF_WRITE};
		
		static pthread_mutex_t	zvgMutex = PTHREAD_MUTEX_INITIALIZER;
		static pthread_t		zvgIOThreadHandle;

		// These THREE variables are controled by the mutex.  NO access outside
		// of the mutex!
		static int				zvgFillState;	// -1=wait, 0=filling 0, 1=filling 1.
		static buffState		zvgBufStatus[2];
		static int				zvgWriteState;	// -1=wait, 0=writing 0, 1=writing 1

		static void *zvgStartThread( void *arg); 

		#ifdef _DEBUG
			#define MUTEXERR(a) if (a) printf("[%s/%d] (UN)LOCK returned %d (0x%08x)\n", \
										__FILE__, __LINE__, (a), (a))
			#define ASSERT(a) if (!(a)) printf("[%s/%d] \"%s\" failed!\n", \
										__FILE__, __LINE__, #a)
		#else /* not _DEBUG */
			#define MUTEXERR(a) ;
			#define ASSERT(a) ;
		#endif /* _DEBUG */

	#else /* DOS - not supported */
	#endif /* OS */
#endif /* ZVG_IO_THREAD_ENABLED */

/*****************************************************************************
* Intialize the ZVG, setup DMA buffers, etc.
*
* This routine is called before anything ZVG related is done.
*****************************************************************************/
uint zvgFrameOpen( void)
{
	uint	err;

	err = zvgInit();									// look for the ZVG

	// read IEEE version information data from ZVG

	if (!err)
		err = zvgReadDeviceID( &ZvgID);

	// read Monitor setup information from ZVG

	if (!err)
		err = zvgReadMonitorInfo( &ZvgMon);

	// read Speed table information for ZVG

	if (!err)
		err = zvgReadSpeedInfo( ZvgSpeeds);

	// reset the ZVG encoder, point to encoder scratch buffer

	if (!err)
	{	zvgEncReset();									// reset ZVG encoder
		zvgEncSetPtr( EncodeBfr);					// point to local encode buffer

		// move monitor flags from environment variable to encoder flags

		if (ZvgIO.envMonitor & MONF_FLIPX)
			ZvgENC.encFlags |= ENCF_FLIPX;

		if (ZvgIO.envMonitor & MONF_FLIPY)
			ZvgENC.encFlags |= ENCF_FLIPY;

		if (ZvgIO.envMonitor & MONF_SPOTKILL)
			ZvgENC.encFlags |= ENCF_SPOTKILL;

		if (ZvgIO.envMonitor & MONF_BW)
			ZvgENC.encFlags |= ENCF_BW;

		if (ZvgIO.envMonitor & MONF_NOOVS)
		{	ZvgENC.encFlags |= ENCF_NOOVS;
			zvgEncSetClipNoOverscan();
		}
	}

	if (err)
		zvgClose();					// if any error occurred, fix everything

	return (err);
}

/*****************************************************************************
* Setup ZVG for buffered frame use.
*****************************************************************************/
void zvgFrameClose( void)
{
	zvgClose();											// restore everything but the timers

	// The following would need to be called from non-MAME code.  MAME handles
	// the 8254 timer outside of ZVG, so it is left to MAME to return the 8254
	// to it's original setup when exiting.

#ifndef MAME
	tmrRestore();
#endif
}


#ifdef ZVG_IO_THREAD_ENABLED
#if defined(WIN32)
#elif defined(linux)

/*****************************************************************************
* Intialize the ZVG, setup DMA buffers, etc.
*
* This routine is called before anything ZVG related is done.
* 
* Author: JBJarvis 5/2007
*****************************************************************************/
uint zvgFrameOpenThreaded( void)
{
	uint	err;
	int		retCode;

	err = zvgFrameOpen();			// Perform standard open stuff.

	// Create thread.
	if (!err) {
		//===========begin=critical=section====================================
		retCode = pthread_mutex_lock( &zvgMutex);
		MUTEXERR(retCode);
		{
			zvgBufStatus[0] = zvgBufStatus[1] = ZBUF_EMPTY;
			zvgWriteState = -1;		// Not writing.
			zvgFillState = zvgCurrentBufferID();		// Filling buffer 0.
		}
		retCode = pthread_mutex_unlock( &zvgMutex);
		MUTEXERR(retCode);
		//===========end=critical=section======================================

		retCode = pthread_create( &zvgIOThreadHandle, NULL,
									zvgStartThread, NULL);
		if (retCode != 0) {		// Failure?
			err = errThreadFail;
			zvgClose();
		}
	}
	else
		zvgClose();					// if any error occurred, close ALL

	return (err);
}

/*****************************************************************************
* Setup ZVG for buffered frame use.
*  
* Author: JBJarvis 5/2007
*****************************************************************************/
void zvgFrameCloseThreaded( void)
{
	zvgFrameClose();
}

#else /* DOS - not supported */
#endif /* OS */
#endif /* ZVG_IO_THREAD_ENABLED */

/*****************************************************************************
* Encode and Send a single vector to the DMA buffer.
*
* For this call, a vector is an absolute position on the screen. The screen's
* dimensions are:
*
*    Upper Left Corner:  X = -512, Y =  383
*    Center of screen:   X =    0, Y =    0
*    Lower Right Corner: X =  511, Y = -384
*
* The screen may be overscanned by 1.25 inches (on a 19" monitor), the
* screen's dimensions with overscan are:
*
*    Upper Left Corner:  X = -600, Y =  471
*    Center of screen:   X =    0, Y =    0
*    Lower Right Corner: X =  599, Y = -472
*
* So the screen is laid out like a peice of graph paper in math class with
* X getting larger as it moves to the right, and Y getting larger as it
* moves upwards.  The orgin: 0,0 is located at the center of the screen.
*
* The color of the vector must have been previously set by a call to one
* of the set color routines.
*
* Called with:
*    xStart = Starting X position of vector.
*    yStart = Starting Y position of vector.
*    xEnd   = Ending X position of vector.
*    yEnd   = Ending Y position of vector.
*
*****************************************************************************/
uint zvgFrameVector( int xStart, int yStart, int xEnd, int yEnd)
{
	uint	err;

	// Encoode vector into 'EncodeBfr[]'

	zvgEnc( xStart, yStart, xEnd, yEnd);

	// move encoded ZVG command into DMA buffer

	err = zvgDmaPutMem( EncodeBfr, zvgEncSize());

	// clear the encode buffer

	zvgEncClearBfr();
	return (err);
}

/*****************************************************************************
* Send the current buffer to the ZVG using DMA.
*****************************************************************************/
uint zvgFrameSend( void)
{
	uint	err;

	// Send End of Frame info. (Center Trace, pad ZVG buffer)

	zvgEncEOF();

	err = zvgDmaPutMem( EncodeBfr, zvgEncSize());

	if (err)
		return (err);

	zvgEncClearBfr();				// Clear the encode buffer

	// Start the DMA transfer of the encoded command in the DMA buffer to
	// the ZVG.
	
	err = zvgDmaSendSwap();		// send the current buffer and swap DMA buffers

	if (err)
		return (err);

	// Start next buffer with spot kill stuff if needed

	zvgEncSOF();

	err = zvgDmaPutMem( EncodeBfr, zvgEncSize());

	if (err)
		return (err);

	zvgEncClearBfr();				// Clear the encode buffer
	return (errOk);
}

#ifdef ZVG_IO_THREAD_ENABLED
#if defined(WIN32)
#elif defined(linux)

/*****************************************************************************
* Send the current buffer to the ZVG using the thread.
*  
* Author: JBJarvis 5/2007
*****************************************************************************/
uint zvgFrameSendThreaded( void)
{
	uint	err;
	int		retCode;

	zvgEncEOF();									// Send End of Frame info.
	err = zvgDmaPutMem( EncodeBfr, zvgEncSize());	// Copy buffer.
	if (err)
		return (err);

	zvgEncClearBfr();								// Clear the encode buffer

	//===========begin=critical=section========================================
	retCode = pthread_mutex_lock( &zvgMutex);
	MUTEXERR(retCode);
	{
		// Want to mark the current buffer as writable.  IOThread will see this
		// and start writting.  other part of the swap is handled in 
		// zvgGetABufferThreaded.
		ASSERT(zvgBufStatus[ zvgFillState] == ZBUF_FILL);
		zvgBufStatus[ zvgFillState] = ZBUF_READY;
	}
	retCode = pthread_mutex_unlock( &zvgMutex);
	MUTEXERR(retCode);
	//===========end=critical=section==========================================

//	printf("[Send] buff %d ready.\n", zvgFillState);
	return (errOk);
}

/*****************************************************************************
* Get a buffer to fill with draw (etc) commands.
*  
* Author: JBJarvis 5/2007
*****************************************************************************/
uint zvgGetABufferThreaded( void)
{
	uint	err;
	int		retCode;
	int		clearDMA = 0;
	int		fillState;

	//===========begin=critical=section========================================
	retCode = pthread_mutex_lock( &zvgMutex);
	MUTEXERR(retCode);
	{
		if (zvgWriteState == zvgFillState) {
			zvgFillState ^= 0x01;					// Switch buffer
			ASSERT(zvgBufStatus[ zvgFillState] == ZBUF_EMPTY);
			zvgBufStatus[ zvgFillState] = ZBUF_FILL;
		}
		else {	// zvgWriteState may be == -1 too.
			zvgBufStatus[ zvgFillState] = ZBUF_FILL;
		}
		fillState = zvgFillState;
	}
	retCode = pthread_mutex_unlock( &zvgMutex);
	MUTEXERR(retCode);
	//===========end=critical=section==========================================

//	printf("[GetABuff] filling buff %d\n", fillState);
	// Ensure correct DMA buffer is in use, i.e. matches the zvgFillState value!
	if (fillState != zvgCurrentBufferID())
		zvgDmaSwap();
	ASSERT(fillState == zvgCurrentBufferID());

	zvgDmaClearBfr();
	zvgEncClearBfr();
	zvgEncSOF();				// Start next buffer with spot kill stuff if needed
	err = zvgDmaPutMem( EncodeBfr, zvgEncSize());

	if (err)
		return (err);

	zvgEncClearBfr();				// Clear the encode buffer

	return errOk;					// Have buffer will write!
}

/*****************************************************************************
* Entry point for the IO process thread.
*  
* Author: JBJarvis 5/2007
*****************************************************************************/
static void *zvgStartThread( void *arg) 
{
	int		i = 1;
	int		retCode;
	int		writeState;
	uint	err;

	printf("\nStarted ZVG IO thread\n");
	while (1) {

		//===========begin=critical=section====================================
		retCode = pthread_mutex_lock( &zvgMutex);
		MUTEXERR(retCode);
		{
			switch (zvgWriteState) {
			case 0:
			case 1:
				zvgBufStatus[ zvgWriteState] = ZBUF_EMPTY;
				zvgWriteState ^= 0x01;			// switch buff
				if (zvgBufStatus[ zvgWriteState] == ZBUF_READY)	// ready?
					zvgBufStatus[ zvgWriteState] = ZBUF_WRITE;
				else
					zvgWriteState = -1;			// Stop writing;
				break;

			case -1:
			default:
				if (zvgBufStatus[ 0] == ZBUF_READY) {
					zvgWriteState = 0;
					zvgBufStatus[ 0] = ZBUF_WRITE;
				}
				else if (zvgBufStatus[ 1] == ZBUF_READY) {
					zvgWriteState = 1;
					zvgBufStatus[ 1] = ZBUF_WRITE;
				}
				else
					zvgWriteState = -1;
				break;
			}
			writeState = zvgWriteState;			// my copy of state.
		}
		retCode = pthread_mutex_unlock( &zvgMutex);
		MUTEXERR(retCode);
		//===========end=critical=section======================================

//		printf("[Thrd] write buff %d\n", writeState);
		if (writeState == -1) {					// Idle?
			usleep(2U);							// SHORT pause, then try again.
		}
		else {									// Writing.
			err = zvgDmaSendBuff( writeState);	// BTW, doesn't swap buffers!
			if (err)
				zvgError( err);					// Report it.
		}
	}
}

#else /* DOS */
#endif /* OS */
#endif /* ZVG_IO_THREAD_ENABLED */
