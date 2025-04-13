/* === File: ThreadLib/ThreadLib.c === */
/* See the file Distribution for distribution terms.
	(c) Copyright 1994 Ari Halberstadt */

/*	ThreadLib implements nonpreemptive multiple thread execution within
	a single application.

	94/03/16 aih - changed ThreadType to ThreadStructure, and ThreadSNType
						to ThreadType.
	94/03/11 aih - made gThread and gStackSniffer static variables
	94/03/09 aih - in v1.0d3 i wrote the stack sniffer sentinel value right over
						the heap zone information for the application heap. this of
						course resulted in a damaged heap. what a stupid mistake.
						now the main thread doesn't use a stack sniffer sentinel
						value, but all other threads still use the stack sniffer
						sentinel value.
	94/03/?? aih - release 1.0d3
	94/02/28 aih - fixed error in setting thread error code in ThreadFromSN
					 - added sentinel value to stack sniffer VBL task
	94/02/25 aih - will compile with either "LoMem.h" or "SysEqu.h"
	94/02/23 aih - fixed an error in the private routine ThreadStackFrame
	94/02/19 aih - added ThreadSleepSet, ThreadData, ThreadDataSet
					 - added doubly-linked list circular queue of threads
					 - all external functions refer to threads using thread serial
					 	numbers instead of thread pointers
					 - when a thread is activated it is moved to the end of the
					 	queue of threads, so that the round-robbin scheduling is
					 	fairer (since the main thread has the highest priority).
	94/02/18 aih - release 1.0d2.1
					 - to reduce the possibility of conflict with user-defined types,
					 	uses ThreadTicksType instead of TicksType, THREAD_TICKS_MAX
					 	instead of TICKS_MAX, and THREAD_TICKS_SEC instead of
					 	TICKS_SEC
					 - includes actual headers instead of using THINK C's
					 	non-standard MacHeaders. this was done primarily so I
					 	could use "SysEqu.h" instead of "LoMem.h", but it will
					 	also make porting to another compiler easier.
					 - added THREAD_DEBUG so thread debug code can be selectively
					 	disabled without defining NDEBUG and surrounded debug
					 	functions with conditional compile directives to reduce
					 	dead-code size in non-debug version
					 - uses the OS routines Enqueue and Dequeue instead of my
					 	own linked-list code. this made the code cleaner and will
					 	make the object code even more compact.
					 - made defines with prefix "LM" for all low-memory globals
	94/02/17 aih - put back THREAD_SET_GLOBALS code in attempt to fix
						update problems
	94/02/17 aih - added string to assertion debug statement, and added ifdef
					 	around assertfailed function to keep it from being compiled
					 	into non-debug version
	94/02/17 aih - release 1.0d2
					 - for efficiency, defined TickCount as Ticks low-memory global
					 - for greater ease in adding ThreadLib to other people's
						applications, removed use of exceptions. this also increases
						the efficiency of context switches in applications that
						don't use exceptions
	94/02/15 aih - removed THREAD_SET_GLOBALS code since it didn't seem to be
						needed
	94/02/14 aih - added thread serial numbers
					 - added precondition for ThreadEnd
					 - fixed problem with defer and combine stack adjusts
	94/02/13 aih - added stack sniffer VBL task
					 - added functions for determining the minimum and the default
					 	stack sizes for threads and removed THREAD_STACK_SIZE
	94/02/12 aih - made threads use pointers instead of handles. using pointers
						increases the efficiency of the thread library. this won't
						significantly increase memory fragmentation since stacks
						for threads are already allocated as pointers. (while no
						stack is allocated for the main thread, the main thread
						is created very early in the application and usually
						remains in existence until the application exits.)
					 - limited release 1.0d1.1
	94/02/11 aih - added thread status field
	94/02/10 aih - release 1.0d1; got threads to work without crashing! yay! :-)
	94/02/04 aih - created */

/*----------------------------------------------------------------------------*/
/* include statements */
/*----------------------------------------------------------------------------*/

#include "LowMem.h"      // Include local LowMem first!
#include "ThreadLib.h"

#include <setjmp.h>
#include <Events.h>
#include <Memory.h>
#include <OSUtils.h>
#include <Errors.h>
#include <Retrace.h>

#include <MacTypes.h>
#include <Quickdraw.h>
#include <Fonts.h>
#include <Dialogs.h>
#include <stdlib.h>    // For exit() (if used indirectly)


/*----------------------------------------------------------------------------*/
/* compiler-specific code */
/*----------------------------------------------------------------------------*/

// #if ! defined(THINK_C) || THINK_C != 5
//	/* The register ordering within the jmp_buf type is compiler dependent.
//		It should be straightforward to modify this for MPW, and less so
//		for a native PowerPC version (though perhaps still possible). For
//		MPW and later versions of THINK C you should check the "setjmp.h" header
//		file for the definition of jmp_buf and also check the instructions used to
//		save and restore the registers (in THINK C they're inline definitions).
//		We only really care about accessing and modifying registers a6 and a7
//		in the jump buffer (though of course other registers need to be saved
//		and restored).
//
//		For compilers other than THINK C, you should be careful when turning
//		on the optimizer. For instance, I found that the "defer and combine
//		stack adjusts" option was incompatible with the thread library. I
//		recommend first getting the thread library to work with all
//		optimizations disabled. Once you know that it runs under the new
//		compiler, you can enable the optimizations. If it then crashes,
//		you'll know that one or more of the optimizations performed by
//		the compiler are incompatible with the thread library and should
//		therefore be disabled.
//
//		If you adapt the thread library for any other compiler, such as
//		THINK C 6.0, MPW, or MetroWorks, please let me know what changes
//		were necessary so that I may incorporate them into the next
//		release. */
//	#error "only tested with THINK C 5.0.4"
// #endif

// #if defined(THINK_C) && __option(profile)
//	/* The THINK C profiler assumes a single execution stack and won't
//		work correctly if threads are used. Other profilers may or may
//		not work correctly; use them at your own risk. */
//	#error "won't work with THINK C profiler"
// #endif

// #if defined(THINK_C)
//	/* The thread library will not work correctly (the heap will become
//		corrupted) when the THINK C "defer and combine stack adjusts"
//		optimization option is enabled. We therefore disable the option
//		for this file. All other THINK C optimizations work fine with the
//		thread library. */
//	#pragma options(! defer_adjust)
// #endif

/* Define register offsets for jmp_buf based on Retro68/GCC's setjmp.h */
/* NOTE: This assumes a standard 68k setjmp layout. Verify if issues arise. */
/*       Retro68's setjmp likely saves d2-d7, a2-a7, pc. */
/*       We need a6 and a7. Assuming a standard layout: */
/*       d2-d7 (6 longs), a2-a5 (4 longs), a6 (1 long), a7 (1 long), pc (1 long) */
/*       So a6 is index 10, a7 is index 11 (0-based). */
enum {
	// d2, d3, d4, d5, d6, d7,  // Indices 0-5
	// a2, a3, a4, a5,          // Indices 6-9
	a6 = 10,                   // Index 10
	a7 = 11                    // Index 11
};


/*----------------------------------------------------------------------------*/
/* debug declarations */
/*----------------------------------------------------------------------------*/

/* Define THREAD_DEBUG as 1 to enable debug code, or 0 to disable debug code.
	You can also define NDEBUG to disable debug code. Debug code is enabled
	by default if both THREAD_DEBUG and NDEBUG are undefined. */
#ifndef THREAD_DEBUG
	#ifndef NDEBUG
		#define THREAD_DEBUG			(1)
	#else
		#define THREAD_DEBUG			(0)
	#endif
#endif /* THREAD_DEBUG */

/*----------------------------------------------------------------------------*/
/* low-memory globals */
/*----------------------------------------------------------------------------*/

/* Accessing the Ticks low-memory global directly, rather than going through
	the trap dispatcher, resulted in a signficant increase in speed. These were
	the results of running my ThreadTimed application (each test ran for 60
	seconds):

		Ticks:		Thread Library: count = 148448
		TickCount:	Thread Library: count = 99360
		Ticks:		Thread Manager: count = 52992
		TickCount:	Thread Manager: count = 52912
		Ticks:		No threads: count = 4054864
		TickCount:	No threads: count = 4057008

	Using the Ticks low-memory global was about 50% faster (148448 versus 99360)
	than using the TickCount trap. */

/* Use the accessor functions defined in the local LowMem.h consistently. */
/* The conditional compilation based on __LOMEM__ is removed. */

/* Example usage (no changes needed here as the code below already uses the functions):
   LMGetTicks()
   LMGetEventQueue()
   LMGetHeapEnd()
   LMGetHiHeapMark()
   LMGetApplLimit()
   LMGetCurStackBase()
   LMGetMinStack()
   LMGetDefltStack()
   LMSetStkLowPt(p)
   LMSetHeapEnd(p)
   LMSetApplLimit(p)
   LMSetHiHeapMark(p)
*/


/*----------------------------------------------------------------------------*/
/* Winter Shell is an application framework I've written. This section
	includes code that implements some of the functions defined in Winter
	Shell but which I've redone here so that the thread library can be
	used in other applications. */
/*----------------------------------------------------------------------------*/

#if WINTER_SHELL // Keep this conditional for potential future use

	#include "ExceptionLib.h"
	#include "MemoryLib.h"

	typedef ExceptionType ThreadExceptionType;

	static void FailThreadError(void)
	{
		FailOSErr(ThreadError());
	}

#else /* WINTER_SHELL */

	/* exception handling */

	typedef void *ThreadExceptionType;
	#define FailThreadError()			((void) 0)
	#define ExceptionSave(exc)			((void) 0)
	#define ExceptionRestore(exc)		((void) 0)

	/* memory allocation */

	#define MemAvailable(n)				(true) // Simplified for now

	/* assertions */

	#define require(x)					threadassert(x)
	#define check(x)						threadassert(x)
	#define ensure(x)						threadassert(x)

	#if THREAD_DEBUG

		#define threadassert(x)	((void) ((x) || assertfailed()))

		static int assertfailed(void)
		{
			DebugStr((StringPtr) "\pAn assertion failed in Thread Library.");
			return(0);
		}

	#else /* THREAD_DEBUG */

		#define threadassert(x) ((void) 0)

	#endif /* THREAD_DEBUG */

#endif /* WINTER_SHELL */

/*----------------------------------------------------------------------------*/
/* global variable and type definitions */
/*----------------------------------------------------------------------------*/

/* values returned by calls to setjmp */
typedef enum { THREAD_SAVE, THREAD_RUN };

/* structure describing a thread */
typedef struct ThreadStructure {
	struct ThreadStructure *next;	/* next thread in queue */
	struct ThreadStructure *prev;	/* previous thread in queue */
	Ptr stack;							/* thread's stack */
	jmp_buf jmpenv;					/* cpu's state for context switch */
	ThreadType sn;						/* thread's serial number */
	ThreadTicksType wake;			/* when to wake thread */
	ThreadProcType entry;			/* thread's entry point */
	ThreadProcType suspend;			/* called when thread is suspended */
	ThreadProcType resume;			/* called when thread is resumed */
	ThreadStatusType status;		/* status of thread */
	void *data;							/* data to pass to thread's call-backs */
	Ptr heapEnd;						/* value of HeapEnd low-memory global */
	Ptr applLimit;						/* value of ApplLimit low-memory global */
	Ptr hiHeapMark;					/* value of HiHeapMark low-memory global */
	ThreadExceptionType exception;/* saved state of exception handler */
} ThreadStructure, *ThreadPtr;

/* queue of threads */
typedef struct {
	ThreadPtr head;					/* head of queue */
	ThreadPtr tail;					/* tail of queue */
	short nelem;						/* number of elements in queue */
} ThreadQueueType, *ThreadQueuePtr;

/* structure describing state of thread library */
typedef struct {
	OSErr error;						/* error code from last function called */
	ThreadQueueType queue;			/* queue of threads */
	ThreadType lastsn;				/* serial number of last thread created */
	ThreadPtr main;					/* main thread */
	ThreadPtr active;					/* currently active thread */
	ThreadPtr dispose;				/* thread to dispose of */
} ThreadStateType;

/* state of thread library */
static ThreadStateType gThread;

/*----------------------------------------------------------------------------*/
/*	Thread Validation */
/*----------------------------------------------------------------------------*/

#if THREAD_DEBUG

/*	ThreadValid returns true if the 'thread' parameter is a valid thread.
	This function is primarily for use during debugging, and is called
	by the preconditions to most of the functions in Thread Library. You
	will usually not need to call this function. So that this function can
	easily be called from within other thread functions, the error code is not
	set by this function. */
static Boolean ThreadValid(ThreadPtr thread)
{
	if (! thread || GetPtrSize((Ptr) thread) != sizeof(ThreadStructure)) return(false);
	if (thread->sn <= 0 || gThread.lastsn < thread->sn) return(false);
	if (gThread.main) {
		if (thread == gThread.main) {
			if (thread->stack) return(false);
			if (thread->entry) return(false);
		}
		else {
			if (! thread->stack) return(false);
			if (! thread->entry) return(false);
		}
	}
	return(true);
}

#endif /* THREAD_DEBUG */

/*----------------------------------------------------------------------------*/
/*	�Stack Sniffer */
/*----------------------------------------------------------------------------*/

/*	�When you define THREAD_STACK_SNIFFER as 1, Thread Library installs a
	VBL task that checks for stack overflow every tick. This is similar to the
	stack sniffer VBL task installed by the system. It is a good idea to
	enable the stack sniffer during debugging. The stack sniffer is enabled
	by default if THREAD_STACK_SNIFFER is not already defined and THREAD_DEBUG
	is not zero. */

/* enable stack sniffer if we're not debugging */
#if ! defined(THREAD_STACK_SNIFFER) && THREAD_DEBUG
	#define THREAD_STACK_SNIFFER (1)
#endif

/* The stack could easily overrun its bounds and then shrink back to
	within its bounds between VBL interrupts, so a "magic" long-word
	is written at the bottom of the stack. If the long-word is modified,
	then the stack is assumed to have overrun its bounds. */
#define STACK_SNIFFER_SENTINEL		(0x534E4946) // 'SNIF'

#if THREAD_STACK_SNIFFER

/* By using extra fields following the VBL task record we can have access
	to additional information and global variables needed by the VBL task. */
typedef struct {
	VBLTask vbl;							/* VBL task record */
	Ptr stack_bottom;						/* bottom of active thread's stack */
	ThreadPtr thread;						/* thread being checked by stack sniffer */
	ThreadPtr *main;						/* pointer to gThread.main */
	ThreadPtr *active;					/* pointer to gThread.active */
	Boolean installed;					/* true if stack sniffer is installed */
} StackSnifferVBLType;

static StackSnifferVBLType gStackSniffer;	/* stack sniffer VBL task record */

/* StackSnifferVBL is a VBL task that checks that the stack pointer hasn't
	gone past the end of the current thread's stack. */
static pascal void StackSnifferVBL(void) // Removed register usage for portability
{
	StackSnifferVBLType *task = &gStackSniffer; // Access global directly
	Ptr rsp;

	// Get current stack pointer (SP/A7)
	// This assembly is specific to 68k GCC syntax
	asm volatile ("move.l %%a7, %0" : "=a" (rsp));

	if (task->thread == *task->active) {
		if (rsp < task->stack_bottom ||
			 (task->thread != *task->main &&
			  *(long *) task->stack_bottom != STACK_SNIFFER_SENTINEL))
		{
			SysError(28); // Use standard error code
		}
	}
	task->vbl.vblCount = 1; // Prime for next VBL
	// Need to return; VBL tasks don't use RTS automatically in C
}


/* StackSnifferInstall installs our own VBL task to check for stack underflow
	or overflow in a thread. This is needed since the system's stack sniffer
	knows only about the boundaries of the application's stack, but not about
	the boundaries of the different stacks used by the threads. */
static void StackSnifferInstall(void)
{
	require(ThreadValid(gThread.active));
	if (! gStackSniffer.installed) {
		gStackSniffer.vbl.qType = vType;
		gStackSniffer.vbl.vblAddr = (VBLUPP) StackSnifferVBL; // Cast needed
		gStackSniffer.vbl.vblCount = 1;
		gStackSniffer.vbl.vblPhase = 0; // Initialize phase
		gStackSniffer.stack_bottom = 0;
		gStackSniffer.thread = NULL;
		gStackSniffer.main = &gThread.main;
		gStackSniffer.active = &gThread.active;
		OSErr err = VInstall((QElemPtr) &gStackSniffer);
		if (err == noErr) {
			gStackSniffer.installed = true;
		} else {
			// Handle VInstall error if necessary
		}
	}
}

/* StackSnifferRemove removes our stack sniffer VBL task. */
static void StackSnifferRemove(void)
{
	if (gStackSniffer.installed) {
		(void) VRemove((QElemPtr) &gStackSniffer);
		gStackSniffer.installed = false;
	}
}

/* StackSnifferResume sets the parameters for the active thread's stack
	for our stack sniffer VBL task. The stack sniffer is disabled when the
	gThread.active variable changes during context switches. This function
	grabs the bottom of the new active thread's stack and reenables the
	stack sniffer. */
static void StackSnifferResume(void)
{
	if (!gStackSniffer.installed) return; // Don't do anything if not installed

	/* get bottom of active thread's stack */
	if (gThread.active == gThread.main)
		gStackSniffer.stack_bottom = LMGetApplLimit();
	else
		gStackSniffer.stack_bottom = gThread.active->stack;

	/* the following statement reenables the stack sniffer */
	gStackSniffer.thread = gThread.active;
}

#else /* THREAD_STACK_SNIFFER */

	#define StackSnifferInstall()	((void) 0)
	#define StackSnifferRemove()	((void) 0)
	#define StackSnifferResume()	((void) 0)

#endif /* THREAD_STACK_SNIFFER */

/*----------------------------------------------------------------------------*/
/*	Everything up to this point was just definitions and utility functions.
	This is where the real meat of the code begins. The code from here on
	is much cleaner and contains very few ugly preprocessor directives. */
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* Private Queue Operations */
/*----------------------------------------------------------------------------*/

/*	Threads are kept in a circular queue of threads. A doubly-linked list is
	used to make removal of an arbitrary thread (not just the head of the
	queue) efficient. */

#if THREAD_DEBUG

/* ThreadQueueValid returns true if the queue is valid. */
static Boolean ThreadQueueValid(ThreadQueuePtr queue)
{
	if (! queue) return(false);
	if (queue->nelem < 0) return(false);
	if (queue->nelem == 0 && (queue->head || queue->tail)) return(false);
	if (queue->nelem > 0 && (! queue->head || ! queue->tail)) return(false);
	if (queue->nelem == 1 && queue->head != queue->tail) return(false);
	if (queue->nelem > 1 && queue->head == queue->tail) return(false); // Should not happen in circular list > 1
	if (queue->head && ! ThreadValid(queue->head)) return(false);
	if (queue->tail && ! ThreadValid(queue->tail)) return(false);
	// Add more checks for circular list integrity if needed
	return(true);
}

#endif /* THREAD_DEBUG */

/* ThreadEnqueue adds the thread to the end of the queue. */
static void ThreadEnqueue(ThreadQueuePtr queue, ThreadPtr thread)
{
	require(ThreadQueueValid(queue));
	require(ThreadValid(thread));
	require(thread->next == NULL); // Use NULL check instead of !ThreadValid
	require(thread->prev == NULL); // Use NULL check instead of !ThreadValid

	if (! queue->head) {
		check(! queue->tail);
		queue->head = queue->tail = thread;
		thread->prev = thread->next = thread; // Point to self in single-element list
	}
	else {
		check(queue->tail != NULL);
		thread->prev = queue->tail;
		thread->next = queue->head;
		queue->tail->next = thread;
		queue->head->prev = thread;
		queue->tail = thread;
	}
	queue->nelem++;
	ensure(queue->tail == thread);
	ensure(thread->next != NULL); // Ensure links are set
	ensure(thread->prev != NULL); // Ensure links are set
	ensure(ThreadQueueValid(queue));
}

/* ThreadDequeue removes the thread from the queue. */
static void ThreadDequeue(ThreadQueuePtr queue, ThreadPtr thread)
{
	require(ThreadQueueValid(queue));
	require(ThreadValid(thread));
	require(thread->next != NULL); // Check links are valid before use
	require(thread->prev != NULL);
	require(queue->nelem > 0);

	if (queue->nelem == 1) {
		check(thread == queue->head && thread == queue->tail);
		queue->head = queue->tail = NULL;
	} else {
		thread->prev->next = thread->next;
		thread->next->prev = thread->prev;
		if (thread == queue->head) {
			queue->head = thread->next;
		}
		if (thread == queue->tail) {
			queue->tail = thread->prev;
		}
	}

	thread->next = thread->prev = NULL; // Invalidate links of dequeued item
	queue->nelem--;
	ensure(thread->next == NULL);
	ensure(thread->prev == NULL);
	ensure(ThreadQueueValid(queue));
}


/*----------------------------------------------------------------------------*/
/*	�Error Handling */
/*----------------------------------------------------------------------------*/

/*	�ThreadError returns the last error that occurred, or noErr if the last
	routine completed successfully. */
OSErr ThreadError(void)
{
	return(gThread.error);
}

/*----------------------------------------------------------------------------*/
/*	�Thread Serial Numbers */
/*----------------------------------------------------------------------------*/

/*	�Every thread is assigned a unique serial number. Serial numbers are used
	to refer to threads, rather than using a pointer, since there is always
	the possiblity that a thread may have terminated before a thread pointer
	is used, which would make the thread pointer invalid. The specific
	assignment of serial numbers to threads is not defined by the interface,
	though every valid thread is guaranteed a non-zero serial number. You
	should not assume that any thread will have a specific serial number. */

/*	ThreadSN returns the thread's serial number. The error code is not changed. */
static ThreadType ThreadSN(ThreadPtr thread)
{
	// require(! thread || ThreadValid(thread)); // Removed assertion for performance/simplicity
	return(thread ? thread->sn : THREAD_NONE);
}

/*	Given the serial number of a thread, ThreadFromSN returns the corresponding
	thread pointer, or NULL if there is no thread with the specified serial
	number. If the thread is found the error code is cleared, otherwise it's
	set to threadNotFoundErr. */
static ThreadPtr ThreadFromSN(register ThreadType tsn) // Keep register keyword if desired
{
	register ThreadPtr thread;
	register short nthread;

	// require(0 <= tsn && tsn <= gThread.lastsn); // Removed assertion

	if (tsn == THREAD_NONE || gThread.queue.nelem == 0) {
		 gThread.error = threadNotFoundErr;
		 FailThreadError();
		 return NULL;
	}

	thread = gThread.queue.head;
	nthread = gThread.queue.nelem;
	while (nthread-- > 0) { // Iterate only up to nelem times
		if (thread->sn == tsn) {
			gThread.error = noErr;
			// ensure(ThreadValid(thread) && thread->sn == tsn); // Removed assertion
			return thread;
		}
		thread = thread->next;
		if (thread == gThread.queue.head && nthread > 0) {
			// Should not happen in a correctly maintained circular list unless tsn not found
			break;
		}
	}

	// If loop finishes without finding, tsn is invalid
	gThread.error = threadNotFoundErr;
	FailThreadError();
	// ensure(! thread || (ThreadValid(thread) && thread->sn == tsn)); // Removed assertion
	return(NULL);
}


/*----------------------------------------------------------------------------*/
/*	�Accessing the Queue of Threads */
/*----------------------------------------------------------------------------*/

/*	�ThreadCount returns the number of threads in the queue. */
short ThreadCount(void)
{
	gThread.error = noErr;
	return(gThread.queue.nelem);
}

/*	�ThreadMain returns the main thread, or THREAD_NONE if there are no
	threads. */
ThreadType ThreadMain(void)
{
	gThread.error = noErr;
	return(ThreadSN(gThread.main));
}

/*	�ThreadActive returns the currently active thread, or THREAD_NONE if
	there are no threads. */
ThreadType ThreadActive(void)
{
	gThread.error = noErr;
	return(ThreadSN(gThread.active));
}

/*	�ThreadFirst returns the first thread in the queue of threads, or
	THREAD_NONE if there are no threads. */
ThreadType ThreadFirst(void)
{
	gThread.error = noErr;
	return(ThreadSN(gThread.queue.head));
}

/*	�ThreadNext returns the next thread in the circular queue of threads. */
ThreadType ThreadNext(ThreadType tsn)
{
	ThreadPtr thread;

	thread = ThreadFromSN(tsn);
	// If ThreadFromSN failed, it set the error code already.
	return(thread ? ThreadSN(thread->next) : THREAD_NONE);
}


/*----------------------------------------------------------------------------*/
/*	�Thread Status */
/*----------------------------------------------------------------------------*/

/*	�ThreadStatus returns the status of the thread as set with ThreadStatusSet.
	A new thread is initially assigned THREAD_STATUS_NORMAL. You can call
	ThreadStatus periodically from within each thread (passing the result of
	ThreadActive as the thread parameter) and should take whatever action is
	specified by the return value. For instance, if ThreadStatus returns
	THREAD_STATUS_QUIT, it means that the application is quitting and you
	should exit the thread. Depending on the operation the thread is
	performing, you may want to display an alert asking the user if
	the thread should be exited. */
ThreadStatusType ThreadStatus(ThreadType tsn)
{
	ThreadPtr thread;

	thread = ThreadFromSN(tsn);
	// If ThreadFromSN failed, it set the error code already.
	return(thread ? thread->status : THREAD_STATUS_NORMAL); // Return default if thread not found
}

/*	�ThreadStatusSet sets the status code for the thread. It is the
	responsibility of each thread to call ThreadStatus to determine what
	action should be taken. For instance, when the user quits the application,
	the application should call ThreadStatusSet with a THREAD_STATUS_QUIT
	parameter for each thread in the queue of threads. Then, the application
	should call ThreadYield, waiting for all other threads to exit before the
	application itself exits. If you prefer not use the thread's status to
	indicate to a thread that it should quit, then you could use some global
	variable, say gQuitting, which the thread could check periodically.

	Status values from THREAD_STATUS_NORMAL through THREAD_STATUS_RESERVED are
	reserved for use by Thread Library. All other values can be used by the
	application for its own purposes. */
void ThreadStatusSet(ThreadType tsn, ThreadStatusType status)
{
	ThreadPtr thread;

	thread = ThreadFromSN(tsn);
	if (thread)
		thread->status = status;
	// ensure(! thread || ThreadStatus(tsn) == status); // Removed assertion
}


/*----------------------------------------------------------------------------*/
/*	�Application Defined Data */
/*----------------------------------------------------------------------------*/

/* �ThreadData returns the data field of the thread. The application can
	use the thread's data field for its own purposes. */
void *ThreadData(ThreadType tsn)
{
	ThreadPtr thread;

	thread = ThreadFromSN(tsn);
	// If ThreadFromSN failed, it set the error code already.
	return(thread ? thread->data : NULL);
}

/* �ThreadDataSet sets the data field of the thread. The application can
	use the thread's data field for its own purposes. */
void ThreadDataSet(ThreadType tsn, void *data)
{
	ThreadPtr thread;

	thread = ThreadFromSN(tsn);
	if (thread)
		thread->data = data;
	// ensure(! thread || ThreadData(tsn) == data); // Removed assertion
}


/*----------------------------------------------------------------------------*/
/*	�Information About the Stack */
/*----------------------------------------------------------------------------*/

/*	�ThreadStackMinimum returns the recommended minimum stack size for
	a thread. Thread Library doesn't enforce a lower limit on the
	stack size, but it is a good idea to allow at least this many bytes
	for a thread's stack. */
size_t ThreadStackMinimum(void)
{
	gThread.error = noErr;
	return((size_t)LMGetMinStack()); // Cast to size_t
}

/*	�ThreadStackDefault returns the default stack size for a thread. This
	is the amount of stack space reserved for a thread if a zero stack size
	is passed to ThreadBegin. */
size_t ThreadStackDefault(void)
{
	gThread.error = noErr;
	return((size_t)LMGetDefltStack()); // Cast to size_t
}

/*	�ThreadStackSpace returns the amount of stack space remaining in the
	specified thread. There are at least the returned number of bytes
	between the thread's stack pointer and the bottom of the thread's
	stack, though slightly more space may be available to the application
	due to overhead from Thread Library.

		NOTE: The trap StackSpace will return incorrect results if called from
		any thread other than the main thread. Likewise, using ApplLimit, HeapEnd,
		or CurStackBase to determine the bounds of a thread's stack will produce
		incorrect results when used outside of the main thread. Instead of calling
		StackSpace, use ThreadStackSpace to determine the amount of free stack
		space in a thread. */
size_t ThreadStackSpace(ThreadType tsn)
{
	size_t result;
	Ptr stack_bottom;
	jmp_buf registers;
	ThreadPtr thread;
	Ptr current_sp;

	result = 0;
	thread = ThreadFromSN(tsn);
	if (thread) {
		if (thread == gThread.main)
			stack_bottom = LMGetApplLimit();
		else
			stack_bottom = thread->stack;

		if (thread == gThread.active) {
			// Get current stack pointer (A7) using inline assembly for GCC
			asm volatile ("move.l %%a7, %0" : "=a" (current_sp));
			// check(current_sp >= stack_bottom); // Removed assertion
			if (current_sp >= stack_bottom) { // Basic sanity check
				result = (size_t)(current_sp - stack_bottom);
			} else {
				result = 0; // Should not happen
			}
		}
		else {
			current_sp = (Ptr) thread->jmpenv[a7];
			// check(current_sp >= stack_bottom); // Removed assertion
			if (current_sp >= stack_bottom) { // Basic sanity check
				result = (size_t)(current_sp - stack_bottom);
			} else {
				result = 0; // Should not happen
			}
		}
	}
	// ensure(result >= 0); // Result is unsigned, always >= 0
	return(result);
}


/*----------------------------------------------------------------------------*/
/*	�Support for Segmentation */
/*----------------------------------------------------------------------------*/

/*	�ThreadStackFrame returns information about the specified thread's
	stack and stack frame. This information is needed for executing
	a stack trace during automatic segment unloading in SegmentLib.c,
	which is part of Winter Shell. You should never need to call this
	function. This function will work correctly even if no threads exist. */
void ThreadStackFrame(ThreadType tsn, ThreadStackFrameType *frame)
{
	ThreadPtr thread;
	jmp_buf registers;
	Ptr current_sp;
	Ptr current_a6;

	/* initialize and convert serial number into a thread pointer */
	frame->stack_top = frame->stack_bottom = frame->register_a6 = NULL;
	thread = (tsn ? ThreadFromSN(tsn) : NULL); // Error code set by ThreadFromSN if tsn invalid

	/* Get the top of the thread's stack. All threads other than the main
		thread use their own private stacks. If no thread is specified
		then the top of the stack defaults to the top of the application's
		stack. */
	if (thread && thread != gThread.main && thread->stack) {
		// check(thread != gThread.main); // Removed assertion
		frame->stack_top = thread->stack + GetPtrSize(thread->stack);
	}
	else {
		// check(thread == gThread.main || thread == NULL); // Removed assertion
		frame->stack_top = LMGetCurStackBase();
	}

	if (thread == gThread.active) {
		/* Registers a6 and a7 point into the active thread's stack so we can
			use their current values. This is the default case if there are no
			threads (in which case 'thread' and gThread.active are both null). */
		// check(! thread || ThreadValid(thread)); // Removed assertion

		// Get current SP (A7) and A6 using inline assembly for GCC
		asm volatile ("move.l %%a7, %0" : "=a" (current_sp));
		asm volatile ("move.l %%a6, %0" : "=a" (current_a6));

		frame->stack_bottom = current_sp;
		frame->register_a6 = current_a6; // Get A6 directly
	}
	else if (thread) { // Handle inactive threads
		/* For inactive threads, we use the values of registers a6 and a7 that
			were saved when the thread was suspended. */
		// check(ThreadValid(thread)); // Removed assertion
		frame->stack_bottom = (Ptr) thread->jmpenv[a7];
		frame->register_a6 = (Ptr) thread->jmpenv[a6];
	} else {
        // If tsn was invalid or THREAD_NONE, frame remains initialized to NULLs
        // Get current A6/A7 anyway? Or leave as NULL? Leaving as NULL seems safer.
        // Let's get current A6/A7 like the active case if thread is NULL
		asm volatile ("move.l %%a7, %0" : "=a" (current_sp));
		asm volatile ("move.l %%a6, %0" : "=a" (current_a6));
        frame->stack_bottom = current_sp;
		frame->register_a6 = current_a6;
    }

	// ensure(! frame->register_a6 || (frame->stack_bottom <= frame->register_a6 && frame->register_a6 <= frame->stack_top)); // Removed assertion
	// ensure((Ptr) 0 < frame->stack_bottom && frame->stack_bottom <= frame->stack_top); // Removed assertion
}


/*----------------------------------------------------------------------------*/
/*	�Scheduling */
/*----------------------------------------------------------------------------*/

/*	�The three functions ThreadSchedule, ThreadActivate, and ThreadYield
	handle the scheduling and context switching of threads. These functions
	will be executed the most often of any of the functions in this file, and
	therefore will have the greatest impact on the efficiency of Thread
	Library. If you find Thread Library's context switches too slow, try
	improving the efficiency of these functions. */

/* ThreadSleepSetPtr is identical to ThreadSleepSet, but for greater
	efficiency it takes a pointer to a thread. */
static void ThreadSleepSetPtr(ThreadPtr thread, ThreadTicksType sleep)
{
	ThreadTicksType ticks;

	require(ThreadValid(thread));
	require(0 <= sleep && sleep <= THREAD_TICKS_MAX);
	/* Set the thread's wakeup time, being careful with overflow since the
		sleep parameter could be THREAD_TICKS_MAX. */
	ticks = LMGetTicks();
	// Check for overflow before adding
	if (sleep > 0 && (THREAD_TICKS_MAX - sleep) < ticks) {
		thread->wake = THREAD_TICKS_MAX;
	} else {
		thread->wake = ticks + sleep;
	}
}


/* �ThreadSleepSet sets the amount of time that the specified thread will
	remain inactive.  The 'sleep' parameter specifies the maximum amount
	of time that the thread can remain inactive. The larger the sleep value,
	the more time is available for execution of other threads. When called
	from the main thread, you can pass a sleep parameter equal to the maximum
	interval between null events; if no null events are needed, you can pass
	a sleep value of THREAD_TICKS_MAX. The main thread will continue to receive
	processing time whenever an event is pending and when no other threads are
	scheduled (see ThreadSchedule). If the thread is already active, the sleep
	time specified will be used when the thread is inactive and is thus eligible
	for scheduling by ThreadSchedule. ThreadSleepSet is normally called by
	ThreadYield, but you may need to use it if you call ThreadSchedule or
	ThreadActivate. */
void ThreadSleepSet(ThreadType tsn, ThreadTicksType sleep)
{
	ThreadPtr thread;

	thread = ThreadFromSN(tsn);
	if (thread)
		ThreadSleepSetPtr(thread, sleep);
	// Error code set by ThreadFromSN if tsn invalid
}

/*	EventPending returns true if an event is pending. Most events are posted
	to the event queue, so we can determine if an event is pending simply
	by examining the head of the event queue. Activate and update events,
	however, are not posted to the event queue, so we need to call EventAvail
	to detect them. Since EventAvail is a very slow trap, we make the interval
	between calls to EventAvail as large as possible without severely
	degrading response time. */
static Boolean EventPending(void)
{
	#define EVENT_PENDING_INTERVAL (15)
	static ThreadTicksType nextEventCheck = 0; // Initialize static variable
	EventRecord event;
	Boolean pending = false; // Initialize

	// Check event queue directly first (fast)
	if (LMGetEventQueue()->qHead != NULL) {
		pending = true;
	}
	// Periodically check EventAvail for non-queued events (slower)
	else if (LMGetTicks() >= nextEventCheck) {
		pending = EventAvail(everyEvent, &event); // Check all events
		nextEventCheck = LMGetTicks() + EVENT_PENDING_INTERVAL;
	}
	return(pending);
}


/* ThreadSchedulePtr is identical to ThreadSchedule, except it returns a pointer
	to a thread instead of a thread serial number. This makes context switches
	triggered via ThreadYield more efficient, since we already have direct
	access to the relavent thread pointers and so don't need to waste time
	converting to and from thread serial numbers. */
static ThreadPtr ThreadSchedulePtr(void)
{
	register ThreadPtr active;			/* active thread */
	register ThreadPtr newthread;		/* thread to switch to */
	register ThreadTicksType ticks;	/* current tick count */
	register short count;             /* loop counter */

	require(ThreadValid(gThread.active));
	gThread.error = noErr;

	if (EventPending()) {
		/* an event is pending, so return main thread */
		newthread = gThread.main;
	}
	else {
		/* round-robbin search for a thread that needs to be woken up */
		ticks = LMGetTicks();
		active = gThread.active;
		newthread = active->next;
		count = gThread.queue.nelem; // Limit search iterations

		// check(ThreadValid(newthread)); // Removed assertion

		// Search starting from the next thread
		while (count-- > 0) {
			if (newthread->wake <= ticks) {
				// Found a ready thread
				break;
			}
			newthread = newthread->next;
			// check(ThreadValid(newthread)); // Removed assertion
		}

		// If loop finished and newthread is still the active one,
		// and it's not ready, then no other thread is ready.
		if (newthread == active && newthread->wake > ticks) {
			/* no thread needs to be woken up, so return main thread */
			newthread = gThread.main;
		}
		// If loop finished and newthread is *not* the active one,
		// it means we wrapped around and didn't find a ready thread.
		else if (count < 0) {
			newthread = gThread.main;
		}
		// Otherwise, 'newthread' points to the first ready thread found.
	}
	ensure(ThreadValid(newthread));
	return(newthread);
}


/*	�ThreadSchedule returns the next thread to activate. Threads are maintained
	in a queue and are scheduled in a round-robbin fashion. Starting with the
	current thread, the queue of threads is searched for the next thread whose
	wake time has arrived. The first such thread found is returned.

	In addition to the round-robbin scheduling shared with all threads, the
	main thread will also be activated if any events are pending in the event
	queue. The application can then immediately handle the events, allowing
	the application to remain responsive to user actions such as mouse clicks.
	The main thread will also be activated if no other threads are scheduled
	for activation, which allows the application either to continue with
	its main processing or to call WaitNextEvent and sleep until a thread
	needs to be activated or some other task or event needs to be handled.

	Since ThreadSchedule calls EventAvail (via EventPending), background
	applications will continue to receive processing time, even if the main
	thread is never activated while some compute intensive thread is executing.
	But, since EventAvail can be a slow trap (especially when it yields the
	processor to another application), it is only executed every few ticks.

	Note: if I figure out a faster way to test for events then the call
	to EventAvail may be removed, and background applications won't
	get time when ThreadSchedule is called. (OSEventAvail won't work
	since it doesn't return update or activate events.) */
ThreadType ThreadSchedule(void)
{
	return(ThreadSN(ThreadSchedulePtr()));
}

/*	ThreadSave saves the context of the active thread. It is called before
	a thread is suspended, but after setjmp has saved the CPU's context for
	the thread. */
static void ThreadSave(void)
{
	require(ThreadValid(gThread.active));

	/* save exception state */
	ExceptionSave(&gThread.active->exception);

	/* Save low-memory globals. We bypass any traps (like GetApplLimit) to
		keep context switches fast. */
	gThread.active->heapEnd = LMGetHeapEnd();
	gThread.active->applLimit = LMGetApplLimit();
	gThread.active->hiHeapMark = LMGetHiHeapMark();

	/* call the application's suspend function */
	if (gThread.active->suspend)
		gThread.active->suspend(gThread.active->data);
}

/*	ThreadRestore restores the context of the active thread. It is called
	before a thread resumes execution, but after the thread's stack has
	been restored. */
static void ThreadRestore(void)
{
	register ThreadQueuePtr queue; /* for faster access to queue */
	register ThreadPtr thread;		 /* for faster access to thread */

	require(ThreadValid(gThread.active));

	/* put things into registers */
	thread = gThread.active;
	queue = &gThread.queue;

	/* activate the stack sniffer VBL for the new active thread */
	StackSnifferResume();

	/* Restore the exception state for the thread. The first
		time this is executed it clears and resets the exception
		state, since the exception field of the thread structure
		is initially filled with nulls. The exception state must
		be cleared for a new thread to prevent exceptions from
		propagating out of the thread's entry point. */
	ExceptionRestore(&gThread.active->exception);

	/* Set the low-memory globals for the thread. We bypass any
		traps or glue (like SetApplLimit) to keep the OS from
		preventing us from changing these globals and to keep
		context switches fast. */
	LMSetHeapEnd(thread->heapEnd);
	LMSetApplLimit(thread->applLimit);
	LMSetHiHeapMark(thread->hiHeapMark);

	/* dispose of the memory allocated for the previous thread (see ThreadEnd) */
	if (gThread.dispose) {
		if (gThread.dispose->stack)
			DisposePtr(gThread.dispose->stack);
		DisposePtr((Ptr) gThread.dispose);
		gThread.dispose = NULL;
	}

	/* Move the thread to the tail of the queue so that it is rescheduled
		to run after all other threads (though scheduling also depends on
		wake times and priority). Functionally, the move-to-tail is always
		equivalent to a dequeue followed by an enqueue, but it's optimized
		for the most common case where the thread is already at the front
		of the queue. Since the queue is circular, we can just advance the
		head and tail pointers to achieve the same result. We do the
		optimization in-line since the function call overhead could be
		significant over many context switches. */
	if (queue->nelem > 1) { // Only move if more than one thread
		if (thread == queue->head) {
			queue->head = thread->next;
			queue->tail = thread;
		}
		// No need to move if it's already the tail
		// else if (thread != queue->tail) {
		//	ThreadDequeue(queue, thread); // This is inefficient, avoid if possible
		//	ThreadEnqueue(queue, thread);
		//}
	}


	/* call the application's resume function */
	if (thread->resume)
		thread->resume(thread->data);
}


/* ThreadActivatePtr is identical to ThreadActivate, except it takes a pointer
	to a thread instead of a thread serial number. This makes context switches
	triggered via ThreadYield more efficient, since we already have direct
	access to the relavent thread pointers and so don't need to waste time
	converting to and from thread serial numbers. */
static void ThreadActivatePtr(ThreadPtr thread)
{
	require(ThreadValid(gThread.active));
	require(ThreadValid(thread));
	if (thread != gThread.active) {
		if (setjmp(gThread.active->jmpenv) == THREAD_SAVE) {

			/* the thread is being deactivated, so do whatever is needed to
				save the active thread's context */
			ThreadSave();

			/* Jump to the specified thread. This suspends the current thread
				and returns at the setjmp statement above (unless this is the
				first time the thread is being executed, in which case it
				returns at the setjmp statement in ThreadBegin). The contents
				of the stack will be correct as soon as the longjmp has
				completed, but ThreadRestore must be called before the
				thread can resume. */
			gThread.active = thread;
			longjmp(thread->jmpenv, THREAD_RUN);
			// check(false); /* doesn't return */ // Removed assertion
		}
		else {
			/* the thread is being activated, so restore the thread's context */
			ThreadRestore();
		}
	}
	/* ensure(gThread.active == thread); */ /* can't evaluate this postcondition */
}


/*	�ThreadActivate activates the specified thread. The context switch is
	accomplished by saving the CPU context with setjmp and then calling
	longjmp, which jumps to the environment saved with setjmp when the thread
	being activated was last suspended. We don't have to do any assembly
	language glue since setjmp saved the value of the stack pointer, which
	at the time of the call to setjmp pointed somewhere in the thread's stack.
	The longjmp instruction will restore the value of the stack pointer and
	will jump to the statement from which to resume the thread. Longjmp also
	handles the saving and restoring of all registers. */
void ThreadActivate(ThreadType tsn)
{
	ThreadPtr thread;

	require(ThreadValid(gThread.active));
	thread = ThreadFromSN(tsn);
	if (thread)
		ThreadActivatePtr(thread);
	// Error code set by ThreadFromSN if tsn invalid
	/* ensure(! thread || ThreadActive() == tsn); */ /* can't evaluate this postcondition */
}

/*	�ThreadYield activates the next scheduled thread as determined by
	ThreadSchedule. The 'sleep' parameter has the same meaning as the
	parameter to ThreadSleepSet. */
void ThreadYield(ThreadTicksType sleep)
{
	require(ThreadValid(gThread.active));
	/* We set the active thread's wakeup time before we run the scheduler and
		before the active thread is suspended. We need to set the wakeup time
		so that the thread scheduler will work correctly. Also, the thread
		scheduler could take several ticks to complete, while we want to
		calculate the wakeup time to be as close as possible to the wakeup
		time specified by the sleep parameter. */
	ThreadSleepSetPtr(gThread.active, sleep);
	ThreadActivatePtr(ThreadSchedulePtr());
}

/*	�ThreadYieldInterval returns the maximum time till the next call to
	ThreadYield. The interval is computed by subtracting the current time
	from each thread's wake time, giving the amount of time that each
	thread can remain inactive. The minimum of these times gives the
	maximum amount of time till the next call to ThreadYield. The wake
	time of the current thread is ignored, since the thread is already
	active. You can use the returned value to determine the maximum sleep
	value to pass to WaitNextEvent. */
ThreadTicksType ThreadYieldInterval(void)
{
	ThreadPtr thread;				/* for iterating through queue of threads */
	ThreadPtr active;				/* currently active thread */
	ThreadTicksType ticks;		/* current tick count */
	ThreadTicksType interval;	/* interval till next call to ThreadYield */
	ThreadTicksType current_interval; /* interval for current thread */
	short count;                  /* loop counter */

	if (!gThread.active) return THREAD_TICKS_MAX; // No active thread? Return max.

	require(ThreadValid(gThread.active));
	gThread.error = noErr;
	ticks = LMGetTicks();
	active = gThread.active;
	thread = active->next;
	interval = THREAD_TICKS_MAX;
	count = gThread.queue.nelem; // Limit iterations

	// check(ThreadValid(thread)); // Removed assertion

	while (count-- > 0 && interval > 0) { // Stop if interval is 0
		if (thread == active) { // Skip active thread
			thread = thread->next;
			continue;
		}

		if (thread->wake <= ticks) {
			interval = 0; // Found a ready thread, yield immediately
			break;
		} else {
			current_interval = thread->wake - ticks;
			if (current_interval < interval) {
				interval = current_interval;
			}
		}
		thread = thread->next;
		// check(ThreadValid(thread)); // Removed assertion
	}

	// ensure(interval >= 0); // Interval is unsigned, always >= 0
	return(interval);
}


/*----------------------------------------------------------------------------*/
/*	�Thread Creation and Destruction */
/*----------------------------------------------------------------------------*/

/* ThreadEndPtr is identical in function to ThreadEnd (see below), but it
	takes a pointer to a thread rather than a thread serial number. */
static void ThreadEndPtr(ThreadPtr thread)
{
	ThreadPtr newthread; /* thread to activate if disposing of the active thread */

	require(ThreadValid(thread));

	newthread = NULL;
	if (thread == gThread.main) {

		/* We're disposing of the main thread, so this is a good time
			to do any final cleanup of the thread library. */

		/* clear globals */
		check(gThread.active == thread);
		check(gThread.queue.nelem == 1);

		/* remove our stack sniffer VBL task */
		StackSnifferRemove();

		/* Dequeue before setting globals to NULL */
		ThreadDequeue(&gThread.queue, thread);
		gThread.main = gThread.active = NULL;


	}
	else if (thread == gThread.active) {

		/* We're disposing of the active thread, so activate the next
			scheduled thread, or the main thread if the next scheduled
			thread is the active thread. */
		newthread = ThreadSchedulePtr();
		if (newthread == gThread.active) // If scheduler picks self, pick main
			newthread = gThread.main;

		/* Dequeue the thread *before* switching context */
		ThreadDequeue(&gThread.queue, thread);

	} else {
		/* Disposing of an inactive thread */
		ThreadDequeue(&gThread.queue, thread);
	}


	// check(! newthread || newthread != gThread.active); // Removed assertion

	if (thread == gThread.active && newthread) {

		/* We're disposing of the active thread, but we can't dispose of
			the thread's stack since we're still using that stack. So
			we delay disposal of the thread until the next thread is
			activated; the thread will be disposed of in ThreadRestore,
			which is executed when the next scheduled thread is activated. */
		check(! gThread.dispose);
		gThread.dispose = thread;

		/* activate the next thread (we don't call ThreadActivate since
			there's no need to save the now-defunct thread's state) */
		gThread.active = newthread;
		longjmp(newthread->jmpenv, THREAD_RUN);
		// check(false); /* doesn't return */ // Removed assertion
	}
	else {
		/* dispose of the memory allocated for the thread (inactive or main) */
		if (thread->stack) // Only dispose stack if it exists (not for main)
			DisposePtr(thread->stack);
		DisposePtr((Ptr) thread);
	}
}


/*	�ThreadEnd removes the thread from the queue and disposes of the memory
	allocated for the thread. If the thread is the active thread then the
	next scheduled thread is activated. All threads (other than the main
	thread) must be disposed of before the main thread can be disposed of. */
void ThreadEnd(ThreadType tsn)
{
	ThreadPtr thread;

	thread = ThreadFromSN(tsn);
	if (thread)
		ThreadEndPtr(thread);
	// Error code set by ThreadFromSN if tsn invalid
	/* ensure(! ThreadValid(ThreadFromSN(tsn))); */ /* can't execute this */
}

/*	�ThreadBeginMain creates the main application thread and returns the main
	thread's serial number. You must call this function before creating any
	other threads with ThreadBegin. You must also call MaxApplZone before
	calling this function. The 'resume', 'suspend', and 'data' parameters
	have the same meanings as the parameters to ThreadBegin.

	There are several important differences between the main thread and
	all subsequently created threads.

	- The main thread is responsible for handling events sent to the
	application, and is therefore scheduled differently from other threads;
	see ThreadSchedule for details.

	- While other threads don't begin executing until they're scheduled to
	execute, the main thread is made the active thread and starts to run as
	soon as ThreadBeginMain returns.

	- Since other threads have a special entry point, they are automatically
	disposed of when that entry point returns. The main thread, lacking
	any special entry point, must be disposed of by the application. You
	should call ThreadEnd, passing it the thread returned by ThreadBeginMain,
	before exiting your application.

	- The main thread uses the application's stack and context; no private
	stack is allocated for the main thread. Initially, there is therefore
	no need to change the context to start executing the thread, and
	no special entry point is required. But, like all other threads, the main
	thread's context will be saved whenever it is suspended to allow another
	thread to execute, and its context will be restored when it is resumed.
*/
ThreadType ThreadBeginMain(ThreadProcType suspend, ThreadProcType resume,
	void *data)
{
	ThreadPtr thread = NULL; /* the new thread */

	require(! gThread.main);

	/* This is always the first routine called for the thread library
		(except, perhaps, for ThreadStackFrame, but that's a "private"
		routine) so this would be a good place to do any one-time
		initializations of the thread library. Conversely, when the
		main thread is disposed of would be a good time to do
		any final cleanup of the thread library. */

	gThread.error = noErr;

	/* allocate thread structure */
	thread = (ThreadPtr) NewPtrClear(sizeof(ThreadStructure));
	if (!thread) {
		gThread.error = MemError(); // Get actual error
		FailThreadError();
		return THREAD_NONE; // Return invalid thread
	}

	/* initialize thread structure */
	thread->suspend = suspend;
	thread->resume = resume;
	thread->data = data;
	thread->sn = ++gThread.lastsn;
	thread->stack = NULL; // Main thread has no separate stack ptr
	thread->entry = NULL; // Main thread has no entry point

	/* save values of low-memory globals */
	thread->heapEnd = LMGetHeapEnd();
	thread->applLimit = LMGetApplLimit();
	thread->hiHeapMark = LMGetHiHeapMark();

	/* make this thread the active and main thread */
	gThread.active = thread;
	gThread.main = thread;

	/* now that the thread is ready to use, append it to the queue of threads
		so that it can be scheduled for execution */
	ThreadEnqueue(&gThread.queue, thread);

	/* install and activate stack sniffer VBL task */
	StackSnifferInstall();
	StackSnifferResume(); // Resume for the main thread

	// FailThreadError(); // Already checked allocation error
	ensure(thread ? ThreadValid(thread) && ! ThreadError() : ThreadError());
	ensure(thread == gThread.main);
	ensure(gThread.active == gThread.main);
	return(ThreadSN(thread));
}


/*	�ThreadBegin creates a new thread and returns the thread's serial number.
	You must create the main thread with ThreadBeginMain before you can call
	ThreadBegin. The 'entry' parameter is a pointer to a function that is
	called to start executing the thread. The 'suspend' parameter is a pointer
	to a function called whenever the thread is suspended. You can use the
	'suspend' function to save additional application defined context for
	the thread. The 'resume' parameter is a pointer to a function called
	whenever the thread is resumed. You can use the 'resume' function to
	restore additional application defined context for the thread. The
	'data' parameter is passed to the 'entry', 'suspend', and 'resume'
	functions and may contain any application defined data.

	The 'stack_size' parameter specifies the size of the stack needed by
	the thread. The requested stack size should be large enough to contain
	all function calls, local variables and parameters, and any operating
	system routines that may be called while the thread is active (including
	interrupt driven routines). If 'stack_size' is zero then the default
	stack size returned by ThreadStackDefault is used. It is a good idea to
	set the stack size to at least the value returned by ThreadStackMinimum;
	otherwise, your application is likely to crash somewhere inside the
	operating system. If your thread crashes try increasing the thread's stack
	size. You can enable the stack sniffer VBL task (see Stack Sniffer above)
	to help detect insufficient stack space. In addition, a stack overflow
	will often result in a corrupted heap, since the stack is allocated as
	a nonrelocatable block in the heap and overflow usually overwrites the
	block's header. For this reason, you can often detect stack overflow by
	enabling a "heap check" option in a low-level debugger such as TMON or
	MacsBug.

	The new thread is appended to the end of the thread queue, making it
	eligible for scheduling whenever ThreadYield is called. ThreadBegin
	returns immediately after creating the new thread. The thread, however,
	is not executed immediately, but rather is executed whenever it is
	scheduled to start. At that time, the function specified in the 'entry'
	parameter is called. When the function has returned, the thread is removed
	from the queue of threads and its stack and any private storage allocated
	by ThreadBegin are disposed of. */
ThreadType ThreadBegin(ThreadProcType entry,
	ThreadProcType suspend, ThreadProcType resume,
	void *data, size_t stack_size)
{
	ThreadPtr thread = NULL; /* the new thread */

	require(ThreadValid(gThread.main));
	require(entry != NULL);
	require(stack_size >= 0); // size_t is unsigned

	gThread.error = noErr;

	/* allocate thread structure */
	thread = (ThreadPtr) NewPtrClear(sizeof(ThreadStructure));
	if (!thread) {
		gThread.error = MemError();
		FailThreadError();
		return THREAD_NONE;
	}

	/* initialize thread structure */
	thread->entry = entry;
	thread->suspend = suspend;
	thread->resume = resume;
	thread->data = data;
	thread->sn = ++gThread.lastsn;

	/* The main thread uses the application's regular stack, while
		nonrelocatable blocks are allocated to contain the stacks of
		all other threads. Since the stack persists until the thread
		that created it terminates, you can create any object you
		require on a thread's stack, including window records and parameter
		blocks. The main advantage of using separate stacks, however,
		is the speed of context switches. A context switch involves
		only a call to longjmp; no time consuming saving and restoring
		of stacks is necessary. */
	if (stack_size == 0)
		stack_size = ThreadStackDefault();

	// Ensure minimum stack size if THREAD_DEBUG is enabled
	#if THREAD_DEBUG
		if (stack_size < ThreadStackMinimum()) {
			// Optionally warn or increase size here
			// stack_size = ThreadStackMinimum();
		}
	#endif

	thread->stack = NewPtr(stack_size);
	if (!thread->stack) {
		gThread.error = MemError();
		DisposePtr((Ptr) thread); // Clean up allocated thread struct
		FailThreadError();
		return THREAD_NONE;
	}

	/* Since all threads other than the main thread use stacks
		allocated in the application's heap, we need to disable the
		system stack sniffer VBL task by setting the low-memory global
		variable StkLowPt to 0. Otherwise, the stack sniffer would
		generate system error #28. We do this only once, perhaps in BeginMain?
		NOTE: This might interfere with system stability if not done carefully.
		Consider if this is truly necessary or if relying on the custom
		stack sniffer is sufficient. For now, let's keep it. */
	LMSetStkLowPt(NULL);

	/* To help the stack sniffer catch stack overrun, we write a
		long-word at the bottom of the thread's stack. */
	#if THREAD_STACK_SNIFFER
		*(unsigned long *) thread->stack = STACK_SNIFFER_SENTINEL;
	#endif

	/* Certain low-memory globals divide the stack and heap.
		We change these globals when a thread other than the
		main thread is activated so that certain OS traps will
		work correctly. Store the values needed for this thread. */
	thread->heapEnd = thread->stack;
	thread->applLimit = thread->stack;
	thread->hiHeapMark = thread->stack;

	/* Set up the new thread's jump environment so that we'll jump
		here when the thread is first activated. */
	if (setjmp(thread->jmpenv) == THREAD_RUN) {

		/* We're now executing the *new* thread (I know, it doesn't
			look like it, but it's all due to the magic [hell?] of
			non-local gotos and global variables). At this point,
			the stack is empty, so we can't access any local variables.
			All subsequent executions of the thread will go through the
			setjmp call in ThreadActivate. */

		/* set up the thread's context */
		ThreadRestore();

		/* call the thread's entry point */
		gThread.active->entry(gThread.active->data);

		/* dispose of the thread and switch to the next scheduled thread */
		ThreadEndPtr(gThread.active);

		// check(false); /* never returns */ // Removed assertion
	}

	/* Munge the registers in the jump environment so that we start from
		the top of the thread's stack, and so that tracing function calls
		on the stack will stop when it reaches the first function executed
		in the new thread (which will always be ThreadBegin, ensuring that
		the thread library segment is not unloaded by my automatic segment
		unloading routines). */
	// Set Stack Pointer (A7) to top of allocated stack
	thread->jmpenv[a7] = (long) (thread->stack + stack_size);
	// Set Frame Pointer (A6) to 0 initially for stack traces
	thread->jmpenv[a6] = 0;

	/* now that the thread is ready to use, append it to the queue of
		threads so that it can be scheduled for execution */
	ThreadEnqueue(&gThread.queue, thread);

	/* We've now successfully created a new thread and set things up so
		that the first time the thread is invoked we'll call the thread's
		entry point. We let the application call ThreadYield in its own
		time to switch contexts. In other words, the new thread doesn't
		start executing until it has been scheduled to start. */

	// FailThreadError(); // Already checked allocation errors
	ensure(thread ? ThreadValid(thread) && ! ThreadError() : ThreadError());
	return(ThreadSN(thread));
}