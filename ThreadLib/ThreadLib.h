/* === File: ThreadLib/ThreadLib.h === */
/* See the file Distribution for distribution terms.
	(c) Copyright 1994 Ari Halberstadt */

	#pragma once

	#include <limits.h>
	#include <stddef.h>
	#include <MacTypes.h> // Include MacTypes for OSErr definition used below
	
	#define THREAD_VERSION			(1)			/* version of thread library */
	#define THREAD_NONE				(0)			/* serial number of an invalid thread */
	#define THREAD_TICKS_SEC		(60L)			/* number of ticks in a second */
	#define THREAD_TICKS_MAX		(LONG_MAX)	/* maximum number of ticks */
	
	/* Thread status values. A thread's status is set with ThreadStatusSet, and
		retrieved with ThreadStatus. Values from THREAD_STATUS_NORMAL through
		THREAD_STATUS_RESERVED are reserved for use by the thread library. All
		other values can be used by the application for its own purposes. */
	typedef long ThreadStatusType;
	enum {
		THREAD_STATUS_NORMAL,						/* status of a new thread */
		THREAD_STATUS_QUIT,							/* the application is quitting */
		THREAD_STATUS_RESERVED = 1023				/* last reserved status */
	};
	
	/* error numbers are defined in standard MacErrors.h (included via other headers)
	   or potentially <Threads.h> if used directly.
	#ifndef __THREADS__
		enum {
			threadTooManyReqsErr	= -617,			// can't create any more threads
			threadNotFoundErr		= -618,			// thread not found
			threadProtocolErr		= -619			// not used by thread library
		};
	#endif // __THREADS__
	*/
	
	typedef long ThreadType;						/* thread reference */
	typedef long ThreadTicksType;					/* clock ticks */
	typedef void (*ThreadProcType)(void *data); /* thread call-back function */
	
	/* The type ThreadSNType is a synonym for the type ThreadType.
		Applications should refer to threads using variables of type
		ThreadType. The type ThreadSNType is included for compatability
		with versions 1.0d2.2 through 1.0d3. New applications should no
		longer use the type ThreadSNType. The same applies to
		THREAD_SN_NONE. */
	#define THREAD_SN_NONE	THREAD_NONE
	typedef ThreadType ThreadSNType;
	
	/* information needed in "SegmentLib.c" for automatic segment unloading */
	typedef struct {
		Ptr stack_top;									/* highest address in thread's stack */
		Ptr stack_bottom;								/* lowest address in thread's stack */
		Ptr register_a6;								/* value of thread's register a6 */
	} ThreadStackFrameType;
	
	OSErr ThreadError(void);
	
	short ThreadCount(void);
	ThreadType ThreadMain(void);
	ThreadType ThreadActive(void);
	ThreadType ThreadFirst(void);
	ThreadType ThreadNext(ThreadType thread);
	
	ThreadStatusType ThreadStatus(ThreadType thread);
	void ThreadStatusSet(ThreadType thread, ThreadStatusType status);
	
	void *ThreadData(ThreadType thread);
	void ThreadDataSet(ThreadType thread, void *data);
	
	size_t ThreadStackMinimum(void);
	size_t ThreadStackDefault(void);
	size_t ThreadStackSpace(ThreadType thread);
	
	void ThreadStackFrame(ThreadType thread, ThreadStackFrameType *frame);
	
	void ThreadSleepSet(ThreadType thread, ThreadTicksType sleep);
	ThreadType ThreadSchedule(void);
	void ThreadActivate(ThreadType thread);
	void ThreadYield(ThreadTicksType sleep);
	ThreadTicksType ThreadYieldInterval(void);
	
	ThreadType ThreadBeginMain(ThreadProcType suspend,
		ThreadProcType resume, void *data);
	ThreadType ThreadBegin(ThreadProcType entry,
		ThreadProcType suspend, ThreadProcType resume,
		void *data, size_t stack_size);
	void ThreadEnd(ThreadType thread);